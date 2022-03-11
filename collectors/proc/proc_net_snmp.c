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
// https://wiki.nix-pro.com/view/Linux_networking_stats
// 调优方式 https://ref.onixs.biz/lost-multicast-packets-troubleshooting.html

static const char       *__proc_net_snmp_filename = "/proc/net/snmp";
static struct proc_file *__pf_net_snmp = NULL;
static ARL_BASE         *__arl_ip = NULL, *__arl_tcp = NULL, *__arl_udp = NULL;

static ssize_t __tcp_MaxConn = 0;

static uint64_t
    __ip_DefaultTTL = 0,
    __ip_InReceives = 0, __ip_InHdrErrors = 0, __ip_InAddrErrors = 0, __ip_ForwDatagrams = 0,
    __ip_InUnknownProtos = 0, __ip_InDiscards = 0, __ip_InDelivers = 0, __ip_OutRequests = 0,
    __ip_OutDiscards = 0, __ip_OutNoRoutes = 0, __ip_ReasmTimeout = 0, __ip_ReasmReqds = 0,
    __ip_ReasmOKs = 0, __ip_ReasmFails = 0, __ip_FragOKs = 0, __ip_FragFails = 0,
    __ip_FragCreates = 0, __tcp_ActiveOpens = 0,
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
    // 发送的数据包个数 tcp_v4_send_reset(), tcp_v4_send_ack()，加１,
    // tcp_transmit_skb(),
    // tcp_make_synack()，加tcp_skb_pcount(skb)(见TCP_COOKIE_TRANSACTIONS)
    __tcp_OutSegs = 0,
    // 重传的包个数, 包括RTO
    // timer和常规重传，即tcp_retransmit_skb()中调用tcp_transmit_skb()，成功返回即＋１。
    __tcp_RetransSegs = 0,
    // 收到的有问题的包个数,
    // tcp_rcv_established()->tcp_validate_incoming()：如果有SYN且seq >= rcv_nxt，加１,
    // 以下函数内，如果checksum错误或者包长度小于TCP header，加１：tcp_v4_do_rcv()
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
    // 本机端口未监听之外的其他原因引起的UDP入包无法送达(应用层)目前主要包含如下几类原因:
    // 1.收包缓冲区满 2.入包校验失败 3.其他
    // Incremented when sock_queue_rcv_skb reports that no memory is available; this happens if
    // sk->sk_rmem_alloc is greater than or equal to sk->sk_rcvbuf.
    __udp_InErrors = 0,
    // udp發包量
    __udp_OutDatagrams = 0,
    // 接收緩衝區溢位的包量 Incremented when sock_queue_rcv_skb reports that no memory is available;
    // this happens if sk->sk_rmem_alloc is greater than or equal to sk->sk_rcvbuf.
    __udp_RcvbufErrors = 0,
    // 傳送緩衝區溢位的包
    __udp_SndbufErrors = 0,
    //
    __udp_InCsumErrors = 0,
    //
    __udp_IgnoredMulti = 0;

static prom_gauge_t *__metric_snmp_ip_defaultttl = NULL, *__metric_snmp_ip_inreceives = NULL,
                    *__metric_snmp_ip_inhdrerrors = NULL, *__metric_snmp_ip_inaddrerrors = NULL,
                    *__metric_snmp_ip_forwdatagrams = NULL,
                    *__metric_snmp_ip_inunknownprotos = NULL, *__metric_snmp_ip_indiscards = NULL,
                    *__metric_snmp_ip_indelivers = NULL, *__metric_snmp_ip_outrequests = NULL,
                    *__metric_snmp_ip_outdiscards = NULL, *__metric_snmp_ip_outnoroutes = NULL,
                    *__metric_snmp_ip_reasmtimeout = NULL, *__metric_snmp_ip_reasmreqds = NULL,
                    *__metric_snmp_ip_reasmoks = NULL, *__metric_snmp_ip_reasmfails = NULL,
                    *__metric_snmp_ip_fragoks = NULL, *__metric_snmp_ip_fragfails = NULL,
                    *__metric_snmp_tcp_maxconn = NULL, *__metric_snmp_tcp_activeopens = NULL,
                    *__metric_snmp_tcp_passiveopens = NULL, *__metric_snmp_tcp_attemptfails = NULL,
                    *__metric_snmp_tcp_estabresets = NULL, *__metric_snmp_tcp_currestab = NULL,
                    *__metric_snmp_tcp_insegs = NULL, *__metric_snmp_tcp_outsegs = NULL,
                    *__metric_snmp_tcp_retranssegs = NULL, *__metric_snmp_tcp_inerrs = NULL,
                    *__metric_snmp_tcp_outrsts = NULL, *__metric_snmp_tcp_incsumerrors = NULL,
                    *__metric_snmp_udp_indatagrams = NULL, *__metric_snmp_udp_noports = NULL,
                    *__metric_snmp_udp_inerrors = NULL, *__metric_snmp_udp_outdatagrams = NULL,
                    *__metric_snmp_udp_rcvbuferrors = NULL, *__metric_snmp_udp_sndbuferrors = NULL,
                    *__metric_snmp_udp_incsumerrors = NULL, *__metric_snmp_udp_ignoredmulti = NULL;

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

    arl_expect(__arl_ip, "DefaultTTL", &__ip_DefaultTTL);
    arl_expect(__arl_ip, "InReceives", &__ip_InReceives);
    arl_expect(__arl_ip, "InHdrErrors", &__ip_InHdrErrors);
    arl_expect(__arl_ip, "InAddrErrors", &__ip_InAddrErrors);
    arl_expect(__arl_ip, "ForwDatagrams", &__ip_ForwDatagrams);
    arl_expect(__arl_ip, "InUnknownProtos", &__ip_InUnknownProtos);
    arl_expect(__arl_ip, "InDiscards", &__ip_InDiscards);
    arl_expect(__arl_ip, "InDelivers", &__ip_InDelivers);
    arl_expect(__arl_ip, "OutRequests", &__ip_OutRequests);
    arl_expect(__arl_ip, "OutDiscards", &__ip_OutDiscards);
    arl_expect(__arl_ip, "OutNoRoutes", &__ip_OutNoRoutes);
    arl_expect(__arl_ip, "ReasmTimeout", &__ip_ReasmTimeout);
    arl_expect(__arl_ip, "ReasmReqds", &__ip_ReasmReqds);
    arl_expect(__arl_ip, "ReasmOKs", &__ip_ReasmOKs);
    arl_expect(__arl_ip, "ReasmFails", &__ip_ReasmFails);
    arl_expect(__arl_ip, "FragOKs", &__ip_FragOKs);
    arl_expect(__arl_ip, "FragFails", &__ip_FragFails);
    arl_expect(__arl_ip, "FragCreates", &__ip_FragCreates);

    arl_expect_custom(__arl_tcp, "MaxConn", arl_callback_ssize_t, &__tcp_MaxConn);
    arl_expect(__arl_tcp, "ActiveOpens", &__tcp_ActiveOpens);
    arl_expect(__arl_tcp, "PassiveOpens", &__tcp_PassiveOpens);
    arl_expect(__arl_tcp, "AttemptFails", &__tcp_AttemptFails);
    arl_expect(__arl_tcp, "EstabResets", &__tcp_EstabResets);
    arl_expect(__arl_tcp, "CurrEstab", &__tcp_CurrEstab);
    arl_expect(__arl_tcp, "InSegs", &__tcp_InSegs);
    arl_expect(__arl_tcp, "OutSegs", &__tcp_OutSegs);
    arl_expect(__arl_tcp, "RetransSegs", &__tcp_RetransSegs);
    arl_expect(__arl_tcp, "InErrs", &__tcp_InErrs);
    arl_expect(__arl_tcp, "OutRsts", &__tcp_OutRsts);
    arl_expect(__arl_tcp, "InCsumErrors", &__tcp_InCsumErrors);

    arl_expect(__arl_udp, "InDatagrams", &__udp_InDatagrams);
    arl_expect(__arl_udp, "NoPorts", &__udp_NoPorts);
    arl_expect(__arl_udp, "InErrors", &__udp_InErrors);
    arl_expect(__arl_udp, "OutDatagrams", &__udp_OutDatagrams);
    arl_expect(__arl_udp, "RcvbufErrors", &__udp_RcvbufErrors);
    arl_expect(__arl_udp, "SndbufErrors", &__udp_SndbufErrors);
    arl_expect(__arl_udp, "InCsumErrors", &__udp_InCsumErrors);
    arl_expect(__arl_udp, "IgnoredMulti", &__udp_IgnoredMulti);

    __metric_snmp_ip_defaultttl = prom_collector_registry_must_register_metric(
        prom_gauge_new("ip_defaultttl",
                       "The default value inserted into the Time-To-Live field of the IP header of "
                       "datagrams originated at this entity, whenever a TTL value is not supplied "
                       "by the transport layer protocol.",
                       2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_ip_inreceives = prom_collector_registry_must_register_metric(
        prom_gauge_new("ip_inreceives",
                       "The total number of input datagrams received from sinterfaces, including "
                       "those received in error.",
                       2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_ip_inhdrerrors = prom_collector_registry_must_register_metric(prom_gauge_new(
        "ip_inhdrerrors",
        "The number of input datagrams discarded due to errors in their IP headers, including bad "
        "checksums, version number mismatch, other format errors, time - to - live exceeded, "
        "errors discovered in processing their IP options, etc.",
        2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_ip_inaddrerrors = prom_collector_registry_must_register_metric(prom_gauge_new(
        "ip_inaddrerrors",
        "The number of input datagrams discarded because the IP address in their IP header's "
        "destination field was not a valid address to be received at this entity.",
        2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_ip_forwdatagrams = prom_collector_registry_must_register_metric(
        prom_gauge_new("ip_forwdatagrams",
                       "The number of input datagrams for which this entity was not their final IP "
                       "destination, as a result of which an attempt was made to find a route to "
                       "forward them to that final destination.",
                       2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_ip_inunknownprotos = prom_collector_registry_must_register_metric(
        prom_gauge_new("ip_inunknownprotos",
                       "The number of locally-addressed datagrams received successfully but "
                       "discarded because of an unknown or unsupported protocol.",
                       2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_ip_indiscards = prom_collector_registry_must_register_metric(prom_gauge_new(
        "ip_indiscards",
        "The number of input IP datagrams for which no problems were encountered to prevent their "
        "continued processing, but which were discarded. Note that this counter does not include "
        "any datagrams discarded while awaiting re-assembly.",
        2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_ip_indelivers = prom_collector_registry_must_register_metric(
        prom_gauge_new("ip_indelivers",
                       "The total number of input datagrams successfully delivered to IP "
                       "user-protocols(including ICMP).",
                       2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_ip_outrequests = prom_collector_registry_must_register_metric(
        prom_gauge_new("ip_outrequests",
                       "The total number of IP datagrams which local IP user-protocols (including "
                       "ICMP) supplied to IP in requests for transmission.  Note that this counter "
                       "does not include any datagrams counted in ipForwDatagrams.",
                       2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_ip_outdiscards = prom_collector_registry_must_register_metric(
        prom_gauge_new("ip_outdiscards",
                       "The number of output IP datagrams for which no problem was encountered to "
                       "prevent their transmission to their destination, but which were discarded. "
                       "Note that this counter would include datagrams counted in ipForwDatagrams "
                       "if any such packets met this discard criterion.",
                       2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_ip_outnoroutes = prom_collector_registry_must_register_metric(
        prom_gauge_new("ip_outnoroutes",
                       "The number of IP datagrams discarded because no route could be found to "
                       "transmit them to their destination.",
                       2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_ip_reasmtimeout = prom_collector_registry_must_register_metric(
        prom_gauge_new("ip_reasmtimeout",
                       "The maximum number of seconds which received fragments are held while they "
                       "are awaiting reassembly at this entity.",
                       2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_ip_reasmreqds = prom_collector_registry_must_register_metric(prom_gauge_new(
        "ip_reasmreqds",
        "The number of IP fragments received which needed to be reassembled at this entity.", 2,
        (const char *[]){ "host", "snmp" }));
    __metric_snmp_ip_reasmoks = prom_collector_registry_must_register_metric(
        prom_gauge_new("ip_reasmoks", "The number of IP datagrams successfully re-assembled.", 2,
                       (const char *[]){ "host", "snmp" }));
    __metric_snmp_ip_reasmfails = prom_collector_registry_must_register_metric(prom_gauge_new(
        "ip_reasmfails", "The number of failures detected by the IP re-assembly algorithm.", 2,
        (const char *[]){ "host", "snmp" }));
    __metric_snmp_ip_fragoks = prom_collector_registry_must_register_metric(prom_gauge_new(
        "ip_fragoks",
        "The number of IP datagrams that have been successfully fragmented at this entity.", 2,
        (const char *[]){ "host", "snmp" }));
    __metric_snmp_ip_fragfails = prom_collector_registry_must_register_metric(prom_gauge_new(
        "ip_fragfails",
        "The number of IP datagrams that have been discarded because they needed to be fragmented "
        "at this entity but could not be, e.g., because their Don't Fragment flag was set.",
        2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_tcp_maxconn = prom_collector_registry_must_register_metric(prom_gauge_new(
        "tcp_maxconn", "The limit on the total number of TCP connections the entity can support.",
        2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_tcp_activeopens = prom_collector_registry_must_register_metric(
        prom_gauge_new("tcp_activeopens",
                       "The number of times TCP connections have made a direct transition to the "
                       "SYN-SENT state from the CLOSED state.",
                       2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_tcp_passiveopens = prom_collector_registry_must_register_metric(
        prom_gauge_new("tcp_passiveopens",
                       "The number of times TCP connections have made a direct transition to the "
                       "SYN-RCVD state from the LISTEN state.",
                       2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_tcp_attemptfails = prom_collector_registry_must_register_metric(prom_gauge_new(
        "tcp_attemptfails",
        "The number of times TCP connections have made a direct transition to the "
        "CLOSED state from either the SYN-SENT state or the SYN-RCVD state, plus the "
        "number of times TCP connections have made a direct transition to the LISTEN "
        "state from the SYN-RCVD state.",
        2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_tcp_estabresets = prom_collector_registry_must_register_metric(
        prom_gauge_new("tcp_estabresets",
                       "The number of times TCP connections have made a direct transition to the "
                       "CLOSED state from either the ESTABLISHED state or the CLOSE-WAIT state.",
                       2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_tcp_currestab = prom_collector_registry_must_register_metric(prom_gauge_new(
        "tcp_currEstab",
        "The number of TCP connections for which the current state is either ESTABLISHED or "
        "CLOSE-WAIT.",
        2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_tcp_insegs = prom_collector_registry_must_register_metric(prom_gauge_new(
        "tcp_insegs",
        "The total number of segments received, including those received in error.  This count "
        "includes segments received on currently established connections.",
        2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_tcp_outsegs = prom_collector_registry_must_register_metric(
        prom_gauge_new("tcp_outsegs",
                       "The total number of segments sent, including those on current connections "
                       "but excluding those containing only retransmitted octets.",
                       2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_tcp_retranssegs = prom_collector_registry_must_register_metric(
        prom_gauge_new("tcp_retranssegs",
                       "The total number of segments retransmitted - that is, the number of TCP "
                       "segments transmitted containing one or more previously transmitted octets.",
                       2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_tcp_inerrs = prom_collector_registry_must_register_metric(
        prom_gauge_new("tcp_inerrs", "The total number of segments received in error.", 2,
                       (const char *[]){ "host", "tcp" }));
    __metric_snmp_tcp_outrsts = prom_collector_registry_must_register_metric(
        prom_gauge_new("tcp_outrsts", "The number of segments sent containing the RST flag.", 2,
                       (const char *[]){ "host", "snmp" }));
    __metric_snmp_tcp_incsumerrors = prom_collector_registry_must_register_metric(
        prom_gauge_new("tcp_incsumerrors", "Number of packets received with checksum problems.", 2,
                       (const char *[]){ "host", "snmp" }));

    __metric_snmp_udp_indatagrams = prom_collector_registry_must_register_metric(prom_gauge_new(
        "udp_indatagrams", "The total number of UDP datagrams delivered to UDP users.", 2,
        (const char *[]){ "host", "snmp" }));
    __metric_snmp_udp_noports = prom_collector_registry_must_register_metric(
        prom_gauge_new("udp_noports",
                       "The total number of received UDP datagrams for which there was no "
                       "application at the destination port.",
                       2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_udp_inerrors = prom_collector_registry_must_register_metric(
        prom_gauge_new("udp_inerrors",
                       "The number of received UDP datagrams that could not be delivered for "
                       "reasons other than the lack of an application at the destination port.",
                       2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_udp_outdatagrams = prom_collector_registry_must_register_metric(prom_gauge_new(
        "udp_outdatagrams", "The total number of UDP datagrams sent from this entity.", 2,
        (const char *[]){ "host", "snmp" }));
    __metric_snmp_udp_rcvbuferrors = prom_collector_registry_must_register_metric(prom_gauge_new(
        "udp_rcvbuferrors",
        "number of UDP packets that are dropped when the application socket buffer is overflowed.",
        2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_udp_sndbuferrors = prom_collector_registry_must_register_metric(prom_gauge_new(
        "udp_sndbuferrors",
        "The number of UDP datagrams that could not be transmitted because the data did not "
        "fit in the application socket buffer.",
        2, (const char *[]){ "host", "snmp" }));
    __metric_snmp_udp_incsumerrors = prom_collector_registry_must_register_metric(
        prom_gauge_new("udp_incsumerrors", "Number of packets received with checksum problems.", 2,
                       (const char *[]){ "host", "snmp" }));
    __metric_snmp_udp_ignoredmulti = prom_collector_registry_must_register_metric(
        prom_gauge_new("udp_ignoredmulti",
                       "The number of received UDP datagrams for which there was no "
                       "application at the destination port and the destination address was a "
                       "multicast address.",
                       2, (const char *[]){ "host", "snmp" }));

    debug("[PLUGIN_PROC:proc_net_snmp] init successed");
    return 0;
}

int32_t collector_proc_net_snmp(int32_t UNUSED(update_every), usec_t UNUSED(dt),
                                const char *config_path) {
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
    size_t words = 0, w = 0;

    for (size_t l = 0; l < lines; l++) {
        const char *key = procfile_lineword(__pf_net_snmp, l, 0);

        if (0 == strcmp(key, "Ip")) {
            size_t h = l++;

            words = procfile_linewords(__pf_net_snmp, l);
            if (unlikely(words < 3)) {
                error("Cannot read %s Ip line, Expected 3+ columns, read %zu.", f_netsnmp, words);
                continue;
            }

            arl_begin(__arl_ip);

            for (w = 1; w < words; w++) {
                if (unlikely(arl_check(__arl_ip, procfile_lineword(__pf_net_snmp, h, w),
                                       procfile_lineword(__pf_net_snmp, l, w)))) {
                    break;
                }
            }

            //设置指标值
            // ip
            prom_gauge_set(__metric_snmp_ip_defaultttl, __ip_DefaultTTL,
                           (const char *[]){ premetheus_instance_label, "Ip" });
            prom_gauge_set(__metric_snmp_ip_inreceives, __ip_InReceives,
                           (const char *[]){ premetheus_instance_label, "Ip" });
            prom_gauge_set(__metric_snmp_ip_inhdrerrors, __ip_InHdrErrors,
                           (const char *[]){ premetheus_instance_label, "Ip" });
            prom_gauge_set(__metric_snmp_ip_inaddrerrors, __ip_InAddrErrors,
                           (const char *[]){ premetheus_instance_label, "Ip" });
            prom_gauge_set(__metric_snmp_ip_forwdatagrams, __ip_ForwDatagrams,
                           (const char *[]){ premetheus_instance_label, "Ip" });
            prom_gauge_set(__metric_snmp_ip_inunknownprotos, __ip_InUnknownProtos,
                           (const char *[]){ premetheus_instance_label, "Ip" });
            prom_gauge_set(__metric_snmp_ip_indiscards, __ip_InDiscards,
                           (const char *[]){ premetheus_instance_label, "Ip" });
            prom_gauge_set(__metric_snmp_ip_indelivers, __ip_InDelivers,
                           (const char *[]){ premetheus_instance_label, "Ip" });
            prom_gauge_set(__metric_snmp_ip_outrequests, __ip_OutRequests,
                           (const char *[]){ premetheus_instance_label, "Ip" });
            prom_gauge_set(__metric_snmp_ip_outdiscards, __ip_OutDiscards,
                           (const char *[]){ premetheus_instance_label, "Ip" });
            prom_gauge_set(__metric_snmp_ip_outnoroutes, __ip_OutNoRoutes,
                           (const char *[]){ premetheus_instance_label, "Ip" });
            prom_gauge_set(__metric_snmp_ip_reasmtimeout, __ip_ReasmTimeout,
                           (const char *[]){ premetheus_instance_label, "Ip" });
            prom_gauge_set(__metric_snmp_ip_reasmreqds, __ip_ReasmReqds,
                           (const char *[]){ premetheus_instance_label, "Ip" });
            prom_gauge_set(__metric_snmp_ip_reasmoks, __ip_ReasmOKs,
                           (const char *[]){ premetheus_instance_label, "Ip" });
            prom_gauge_set(__metric_snmp_ip_reasmfails, __ip_ReasmFails,
                           (const char *[]){ premetheus_instance_label, "Ip" });
            prom_gauge_set(__metric_snmp_ip_fragoks, __ip_FragOKs,
                           (const char *[]){ premetheus_instance_label, "Ip" });
            prom_gauge_set(__metric_snmp_ip_fragfails, __ip_FragFails,
                           (const char *[]){ premetheus_instance_label, "Ip" });

            debug(
                "[PLUGIN_PROC:proc_net_snmp] ip_DefaultTTl:%lu ip_InReceives:%lu, "
                "ip_InHdrErrors:%lu, ip_InAddrErrors:%lu, ip_ForwDatagrams:%lu, "
                "ip_InUnknownProtos:%lu, ip_InDiscards:%lu, ip_InDelivers:%lu, ip_OutRequests:%lu, "
                "ip_OutDiscards:%lu, ip_OutNoRoutes:%lu, ip_ReasmTimeout:%lu, ip_ReasmReqds:%lu, "
                "ip_ReasmOKs:%lu, ip_ReasmFails:%lu, ip_FragOKs:%lu, ip_FragFails:%lu",
                __ip_DefaultTTL, __ip_InReceives, __ip_InHdrErrors, __ip_InAddrErrors,
                __ip_ForwDatagrams, __ip_InUnknownProtos, __ip_InDiscards, __ip_InDelivers,
                __ip_OutRequests, __ip_OutDiscards, __ip_OutNoRoutes, __ip_ReasmTimeout,
                __ip_ReasmReqds, __ip_ReasmOKs, __ip_ReasmFails, __ip_FragOKs, __ip_FragFails);
        } else if (0 == strcmp(key, "Tcp")) {
            size_t h = l++;

            words = procfile_linewords(__pf_net_snmp, l);
            if (unlikely(words < 3)) {
                error("Cannot read %s Tcp line, Expected 3+ columns, read %zu.", f_netsnmp, words);
                continue;
            }

            arl_begin(__arl_tcp);
            for (w = 1; w < words; w++) {
                if (unlikely(arl_check(__arl_tcp, procfile_lineword(__pf_net_snmp, h, w),
                                       procfile_lineword(__pf_net_snmp, l, w)))) {
                    break;
                }
            }

            // tcp
            prom_gauge_set(__metric_snmp_tcp_maxconn, __tcp_MaxConn,
                           (const char *[]){ premetheus_instance_label, "Tcp" });
            prom_gauge_set(__metric_snmp_tcp_activeopens, __tcp_ActiveOpens,
                           (const char *[]){ premetheus_instance_label, "Tcp" });
            prom_gauge_set(__metric_snmp_tcp_passiveopens, __tcp_PassiveOpens,
                           (const char *[]){ premetheus_instance_label, "Tcp" });
            prom_gauge_set(__metric_snmp_tcp_attemptfails, __tcp_AttemptFails,
                           (const char *[]){ premetheus_instance_label, "Tcp" });
            prom_gauge_set(__metric_snmp_tcp_estabresets, __tcp_EstabResets,
                           (const char *[]){ premetheus_instance_label, "Tcp" });
            prom_gauge_set(__metric_snmp_tcp_currestab, __tcp_CurrEstab,
                           (const char *[]){ premetheus_instance_label, "Tcp" });
            prom_gauge_set(__metric_snmp_tcp_insegs, __tcp_InSegs,
                           (const char *[]){ premetheus_instance_label, "Tcp" });
            prom_gauge_set(__metric_snmp_tcp_outsegs, __tcp_OutSegs,
                           (const char *[]){ premetheus_instance_label, "Tcp" });
            prom_gauge_set(__metric_snmp_tcp_retranssegs, __tcp_RetransSegs,
                           (const char *[]){ premetheus_instance_label, "Tcp" });
            prom_gauge_set(__metric_snmp_tcp_inerrs, __tcp_InErrs,
                           (const char *[]){ premetheus_instance_label, "Tcp" });
            prom_gauge_set(__metric_snmp_tcp_outrsts, __tcp_OutRsts,
                           (const char *[]){ premetheus_instance_label, "Tcp" });
            prom_gauge_set(__metric_snmp_tcp_incsumerrors, __tcp_InCsumErrors,
                           (const char *[]){ premetheus_instance_label, "Tcp" });

            debug("[PLUGIN_PROC:proc_net_snmp] tcp_MaxConn:%ld tcp_ActiveOpens:%lu, "
                  "tcp_PassiveOpens:%lu, tcp_AttemptFails:%lu, tcp_EstabResets:%lu, "
                  "tcp_CurrEstab:%lu, tcp_InSegs:%lu, tcp_OutSegs:%lu, tcp_RetransSegs:%lu, "
                  "tcp_InErrs:%lu, tcp_OutRsts:%lu, tcp_InCsumErrors:%lu",
                  __tcp_MaxConn, __tcp_ActiveOpens, __tcp_PassiveOpens, __tcp_AttemptFails,
                  __tcp_EstabResets, __tcp_CurrEstab, __tcp_InSegs, __tcp_OutSegs,
                  __tcp_RetransSegs, __tcp_InErrs, __tcp_OutRsts, __tcp_InCsumErrors);
        } else if (0 == strcmp(key, "Udp")) {
            size_t h = l++;

            words = procfile_linewords(__pf_net_snmp, l);
            if (unlikely(words < 3)) {
                error("Cannot read %s Udp line, Expected 3+ columns, read %zu.", f_netsnmp, words);
                continue;
            }

            arl_begin(__arl_udp);
            for (w = 1; w < words; w++) {
                if (unlikely(arl_check(__arl_udp, procfile_lineword(__pf_net_snmp, h, w),
                                       procfile_lineword(__pf_net_snmp, l, w)))) {
                    break;
                }
            }

            // udp
            prom_gauge_set(__metric_snmp_udp_indatagrams, __udp_InDatagrams,
                           (const char *[]){ premetheus_instance_label, "Udp" });
            prom_gauge_set(__metric_snmp_udp_noports, __udp_NoPorts,
                           (const char *[]){ premetheus_instance_label, "Udp" });
            prom_gauge_set(__metric_snmp_udp_inerrors, __udp_InErrors,
                           (const char *[]){ premetheus_instance_label, "Udp" });
            prom_gauge_set(__metric_snmp_udp_outdatagrams, __udp_OutDatagrams,
                           (const char *[]){ premetheus_instance_label, "Udp" });
            prom_gauge_set(__metric_snmp_udp_rcvbuferrors, __udp_RcvbufErrors,
                           (const char *[]){ premetheus_instance_label, "Udp" });
            prom_gauge_set(__metric_snmp_udp_sndbuferrors, __udp_SndbufErrors,
                           (const char *[]){ premetheus_instance_label, "Udp" });
            prom_gauge_set(__metric_snmp_udp_incsumerrors, __udp_InCsumErrors,
                           (const char *[]){ premetheus_instance_label, "Udp" });
            prom_gauge_set(__metric_snmp_udp_ignoredmulti, __udp_IgnoredMulti,
                           (const char *[]){ premetheus_instance_label, "Udp" });

            debug("[PLUGIN_PROC:proc_net_snmp] udp_InDatagrams:%lu, udp_NoPorts:%lu, "
                  "udp_InErrors:%lu, udp_OutDatagrams:%lu, udp_RcvbufErrors:%lu, "
                  "udp_SndbufErrors:%lu, udp_InCsumErrors:%lu, udp_IgnoredMulti:%lu",
                  __udp_InDatagrams, __udp_NoPorts, __udp_InErrors, __udp_OutDatagrams,
                  __udp_RcvbufErrors, __udp_SndbufErrors, __udp_InCsumErrors, __udp_IgnoredMulti);
        } else {
            continue;
        }
    }

    return 0;
}

void fini_collector_proc_net_snmp() {
    if (likely(__arl_ip)) {
        arl_free(__arl_ip);
        __arl_ip = NULL;
    }

    if (likely(__arl_tcp)) {
        arl_free(__arl_tcp);
        __arl_tcp = NULL;
    }

    if (likely(__arl_udp)) {
        arl_free(__arl_udp);
        __arl_udp = NULL;
    }

    if (likely(__pf_net_snmp)) {
        procfile_close(__pf_net_snmp);
        __pf_net_snmp = NULL;
    }

    debug("[PLUGIN_PROC:proc_net_snmp] fini successed");
}