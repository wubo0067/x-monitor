/*
 * @Author: CALM.WU
 * @Date: 2022-02-21 11:10:03
 * @Last Modified by: calmwu
 * @Last Modified time: 2022-02-28 16:48:46
 */

// https://www.codeleading.com/article/87784845826/

#include "prometheus-client-c/prom.h"

#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/strings.h"
#include "utils/clocks.h"
#include "utils/adaptive_resortable_list.h"

#include "appconfig/appconfig.h"

// https://www.rfc-editor.org/rfc/rfc1213
// https://www.ibm.com/docs/sl/zvm/7.1?topic=objects-ip-group
// https://www.cnblogs.com/lovemyspring/articles/5087895.html
// RtoAlgorithm: 默认为1，RTO算法与RFC2698一致
// netstat -st
// ip -s -s link

static const char       *__proc_net_snmp_filename = "/proc/net/snmp";
static struct proc_file *__pf_net_snmp = NULL;
static ARL_BASE         *__arl_ip = NULL, *__arl_tcp = NULL, *__arl_udp = NULL;

static uint64_t
    // "The default value inserted into the Time-To-Live field of the IP header of datagrams
    // originated at this entity, whenever a TTL value is not supplied by the transport layer
    // protocol."
    __ip_DefaultTTL = 0,
    // "The total number of input datagrams received from sinterfaces, including those received in
    // error."
    // IP层处理的数据包总数
    __ip_InReceives = 0,
    // "The number of input datagrams discarded due to errors in their IP headers, including bad
    // checksums, version number mismatch, other format errors, time-to-live exceeded, errors
    // discovered in processing their IP options, etc."
    __ip_InHdrErrors = 0,
    //  "The number of input datagrams discarded because the IP address in their IP header's
    //  destination field was not a valid address to be received at this entity.  This count
    //  includes invalid addresses (e.g., 0.0.0.0) and addresses of unsupported Classes (e.g., Class
    //  E).  For entities which are not IP Gateways and therefore do not forward datagrams, this
    //  counter includes datagrams discarded because the destination address was not a local
    //  address."
    __ip_InAddrErrors = 0,
    // "The number of input datagrams for which this
    //  entity was not their final IP destination, as a
    //  result of which an attempt was made to find a
    //  route to forward them to that final destination.
    //  In entities which do not act as IP Gateways, this
    //  counter will include only those packets which were
    //  Source-Routed via this entity, and the Source-
    //  Route option processing was successful."
    //  IP转发的数据包总数
    __ip_ForwDatagrams = 0,
    // "The number of locally-addressed datagrams
    //  received successfully but discarded because of an
    //  unknown or unsupported protocol."
    // IP头中的协议未知的数据包总数
    __ip_InUnknownProtos = 0,
    //  "The number of input IP datagrams for which no
    //  problems were encountered to prevent their
    //  continued processing, but which were discarded
    //  (e.g., for lack of buffer space).  Note that this
    //  counter does not include any datagrams discarded
    //  while awaiting re-assembly."
    __ip_InDiscards = 0,
    // "The total number of input datagrams successfully
    //  delivered to IP user-protocols (including ICMP)."
    __ip_InDelivers = 0,
    // "The total number of IP datagrams which local IP
    //  user-protocols (including ICMP) supplied to IP in
    //  requests for transmission.  Note that this counter
    //  does not include any datagrams counted in
    //  ipForwDatagrams."
    // IP层向外发送数据包请求的总数
    __ip_OutRequests = 0,
    // "The number of output IP datagrams for which no
    //  problem was encountered to prevent their
    //  transmission to their destination, but which were
    //  discarded (e.g., for lack of buffer space).  Note
    //  that this counter would include datagrams counted
    //  in ipForwDatagrams if any such packets met this
    //  (discretionary) discard criterion."
    // IP层接收数据时由于一些错误而丢弃的数据包总数
    __ip_OutDiscards = 0,
    // "The number of IP datagrams discarded because no
    //  route could be found to transmit them to their
    //  destination.  Note that this counter includes any
    //  packets counted in ipForwDatagrams which meet this
    //  `no-route' criterion.  Note that this includes any
    //  datagarms which a host cannot route because all of
    //  its default gateways are down."
    // IP层发送数据时没有找到路由的数据包总数
    __ip_OutNoRoutes = 0,
    // The maximum number of seconds that received fragments are held while awaiting reassembly at
    // this entry.
    // IP碎片队列超时次数
    __ip_ReasmTimeout = 0,
    // The number of IP fragments that are received and need to be reassembled at this entry.
    // IP碎片重组包数
    __ip_ReasmReqds = 0,
    // The number of IP datagrams reassembled without problems.
    // IP碎片重组成功次数
    __ip_ReasmOKs = 0,
    // The number of failures detected by the IP reassembly algorithm. This is not a count of
    // discarded IP fragments because some algorithms can lose track of the number of fragments by
    // combining them as they are received.
    // IP碎片重组失败次数
    __ip_ReasmFails = 0,
    // The number of IP datagrams that have fragmented at this entry without problems.
    __ip_FragOKs = 0,
    // !The number of IP datagrams that should have been fragmented at this entry, but were not
    // because their Don’t Fragment flag was set.
    __ip_FragFails = 0,
    // The number of IP datagram fragments that have been generated, because of fragmentation at
    // this entry.
    __ip_FragCreates = 0,
    // 协议栈本身并不会限制TCP连接总数，默认值为-1.
    __tcp_MaxConn = 0,
    // 主动建连次数，CLOSE => SYN-SENT次数 tcp_connect()，发送SYN时，加１
    __tcp_ActiveOpens = 0,
    // 被动建连次数，RFC原意是LISTEN => SYN-RECV次数，但Linux选择在三次握手成功后才加1
    // tcp_create_openreq_child(), 被动三路握手完成，加１
    __tcp_PassiveOpens = 0,
    // 建连失败次数 tcp_done():如果在SYN_SENT/SYN_RECV状态下结束一个连接，加１;
    // tcp_check_req():被动三路握手最后一个阶段中的输入包中如果有RST|SYN标志，加１
    __tcp_AttemptFails = 0,
    // tcp_set_state()，新状态为TCP_CLOSE，如果旧状态是ESTABLISHED／TCP_CLOSE_WAIT就加１,
    // 连接被reset次数，ESTABLISHED => CLOSE次数 + CLOSE-WAIT => CLOSE次数
    __tcp_EstabResets = 0,
    // 当前TCP连接数，ESTABLISHED个数 + CLOSE-WAIT个数,
    // tcp_set_state()，根据ESTABLISHED是新／旧状态，分别加减一。
    __tcp_CurrEstab = 0,
    // !tcp_v4_rcv(),收到一个skb，加１, 收到的数据包个数，包括有错误的包个数
    __tcp_InSegs = 0,
    // 发送的数据包个数 tcp_v4_send_reset(), tcp_v4_send_ack()，加１, tcp_transmit_skb(),
    // tcp_make_synack()，加tcp_skb_pcount(skb)(见TCP_COOKIE_TRANSACTIONS)
    __tcp_OutSegs = 0,
    // 重传的包个数, 包括RTO
    // timer和常规重传，即tcp_retransmit_skb()中调用tcp_transmit_skb()，成功返回即＋１。
    __tcp_RetransSegs = 0,
    // 收到的有问题的包个数, tcp_rcv_established()->tcp_validate_incoming()：如果有SYN且seq >=
    // rcv_nxt，加１, 以下函数内，如果checksum错误或者包长度小于TCP header，加１：tcp_v4_do_rcv()
    // tcp_rcv_established() tcp_v4_rcv()
    __tcp_InErrs = 0,
    // 发送的带reset标记的包个数 tcp_v4_send_reset(), tcp_send_active_reset()加１
    __tcp_OutRsts = 0,
    // 收到的checksum有问题的包个数，InErrs中应该只有*小部分*属于该类型
    __tcp_InCsumErrors = 0,
    // udp收包量
    __udp_InDatagrams = 0,
    // packets to unknown port received
    __udp_NoPorts = 0,
    // 本机端口未监听之外的其他原因引起的UDP入包无法送达(应用层)目前主要包含如下几类原因: 1.收包缓冲区满
    // 2.入包校验失败 3.其他
    __udp_InErrors = 0,
    // udp發包量
    __udp_OutDatagrams = 0,
    // 接收緩衝區溢位的包量
    __udp_RcvbufErrors = 0,
    // 傳送緩衝區溢位的包
    __udp_SndbufErrors = 0,
    //
    __udp_InCsumErrors = 0,
    //
    __udp_IgnoredMulti = 0;

int32_t init_collector_proc_net_snmp() {
    __arl_ip = arl_create("proc_snmp_ip", NULL, 3);
    if (unlikely(NULL == __arl_ip)) {
        return -1;
    }

    __arl_tcp = arl_create("proc_snmp_tcp", NULL, 3);
    if (unlikely(NULL == __arl_tcp)) {
        return -1;
    }

    __arl_udp = arl_create("proc_snmp_udp", NULL, 3);
    if (unlikely(NULL == __arl_udp)) {
        return -1;
    }

    debug("[PLUGIN_PROC:proc_net_snmp] init successed");
    return 0;
}

int32_t collector_proc_net_snmp(int32_t update_every, usec_t dt, const char *config_path) {
    debug("[PLUGIN_PROC:proc_net_snmp] config:%s running", config_path);

    const char *f_netsnmp =
        appconfig_get_member_str(config_path, "monitor_file", __proc_net_snmp_filename);

    if (unlikely(!__pf_net_snmp)) {
        __pf_net_snmp = procfile_open(f_netsnmp, " \t:", PROCFILE_FLAG_DEFAULT);
        if (unlikely(!__pf_net_snmp)) {
            error("Cannot open %s", f_netsnmp);
            return -1;
        }
    }

    __pf_net_snmp = procfile_readall(__pf_net_snmp);
    if (unlikely(!__pf_net_snmp)) {
        error("Cannot read %s", f_netsnmp);
        return -1;
    }

    size_t lines = procfile_lines(__pf_net_snmp);
    size_t words = 0;

    for (size_t l = 0; l < lines; l++) {
        size_t h = l++;
    }

    return 0;
}

void fini_collector_proc_net_snmp() {
    if (likely(!__arl_ip)) {
        arl_free(__arl_ip);
        __arl_ip = NULL;
    }

    if (likely(!__arl_tcp)) {
        arl_free(__arl_tcp);
        __arl_tcp = NULL;
    }

    if (likely(!__arl_udp)) {
        arl_free(__arl_udp);
        __arl_udp = NULL;
    }

    if (likely(!__pf_net_snmp)) {
        procfile_close(__pf_net_snmp);
        __pf_net_snmp = NULL;
    }

    debug("[PLUGIN_PROC:proc_net_snmp] fini successed");
}