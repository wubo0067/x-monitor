/*
 * @Author: CALM.WU
 * @Date: 2022-02-21 11:09:55
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-02-24 19:03:20
 */

// https://github.com/moooofly/MarkSomethingDown/blob/master/Linux/TCP%20%E7%9B%B8%E5%85%B3%E7%BB%9F%E8%AE%A1%E4%BF%A1%E6%81%AF%E8%AF%A6%E8%A7%A3.md
// https://perthcharles.github.io/2015/11/10/wiki-netstat-proc/
// http://blog.chinaunix.net/uid-20043340-id-3016560.html
// ECN https://blog.csdn.net/xxx_500/article/details/8584323

#include "prometheus-client-c/prom.h"

#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/strings.h"
#include "utils/clocks.h"
#include "utils/adaptive_resortable_list.h"

#include "appconfig/appconfig.h"

static const char       *__proc_netstat_filename = "/proc/net/netstat";
static struct proc_file *__pf_netstat = NULL;
static ARL_BASE         *__arl_ipext = NULL, *__arl_tcpext = NULL;

static uint64_t
    // IP bandwidth 系统收到IP数据报总字节数, 后面转换成kilobits/s
    __ipext_InOctets = 0,
    // IP bandwidth 系统发送的IP数据报总字节数
    __ipext_OutOctets = 0,
    // IP input errors 由于转发路径中没有路由而丢弃的 IP 数据报数量
    __ipext_InNoRoutes = 0,
    // 由于帧没有携带足够的数据而丢弃的 IP 数据报
    __ipext_InTruncatedPkts = 0,
    // 具有校验和错误的 IP 数据报
    __ipext_InCsumErrors = 0,
    // IP multicast bandwidth
    __ipext_InMcastOctets = 0,
    //
    __ipext_OutMcastOctets = 0,
    // IP multicast packets
    __ipext_InMcastPkts = 0,
    //
    __ipext_OutMcastPkts = 0,
    // IP broadcast bandwidth 接收的 IP 广播数据报 octet 数
    __ipext_InBcastOctets = 0,
    // 发送的 IP 广播数据报 octet 数
    __ipext_OutBcastOctets = 0,
    // IP broadcast packets 接收的 IP 广播数据报报文
    __ipext_InBcastPkts = 0,
    // 发送的 IP 广播数据报报文
    __ipext_OutBcastPkts = 0,
    /*
    Explicit Congestion Notification
    ECT   CE       [Obsolete] RFC 2481 names for the ECNbits.
    0    0       Not-ECT 00代表该报文并不支持ECN，所以路由器的将该报文按照原始非ECN报文处理即可
    0    1       ECT(1)
    1    0       ECT(0)
    1    1       CE 当拥塞发生时，针对ECN=11的报文，需要继续转发
    */
    // IP ECN 接收到的带有 NOECT 的 ip 数据报
    __ipext_InNoECTPkts = 0,
    // 接收到的带有 ECT(1) 代码点的 ip 数据报
    __ipext_InECT1Pkts = 0,
    // 接收到的带有 ECT(0) 代码点的 ip 数据报
    __ipext_InECT0Pkts = 0,
    // 拥塞转发的数据报
    __ipext_InCEPkts = 0,
    // <<<IP TCP Reordering>>> 当发现了需要更新某条 TCP 连接的 reordering
    // 值(乱序值)时，以下计数器可能被使用到, 不过下面四个计数器为互斥关系
    // We detected re-ordering using fast retransmit
    __tcpext_TCPRenoReorder = 0,
    // We detected re-ordering using FACK Forward ACK, the highest sequence number known to have
    // been received by the peer when using SACK. FACK is used during congestion control
    __tcpext_TCPFACKReorder = 0,
    // We detected re-ordering using SACK
    __tcpext_TCPSACKReorder = 0,
    // 	We detected re-ordering using the timestamp option
    __tcpext_TCPTSReorder = 0,
    // <<<IP TCP SYN Cookies>>> syncookies机制是为应对synflood攻击而被提出来的
    /*  syncookies一般不会被触发，只有在tcp_max_syn_backlog队列被占满时才会被触发
        因此SyncookiesSent和SyncookiesRecv一般应该是0。
        但是SyncookiesFailed值即使syncookies机制没有被触发，也很可能不为0。
        这是因为一个处于LISTEN状态的socket收到一个不带SYN标记的数据包时，就会调
        用cookie_v4_check()尝试验证cookie信息。而如果验证失败，Failed次数就加1。
    */
    // 使用syncookie技术发送的syn/ack包个数
    __tcpext_SyncookiesSent = 0,
    // 收到携带有效syncookie信息包个数
    __tcpext_SyncookiesRecv = 0,
    // 收到携带无效syncookie信息包个数
    __tcpext_SyncookiesFailed = 0,
    // <<<IP TCP Out Of Order Queue>>>  http://www.spinics.net/lists/netdev/msg204696.html
    // Number of packets queued in OFO queue
    // OFO 队列的数据包
    __tcpext_TCPOFOQueue = 0,
    // Number of packets meant to be queued in OFO but dropped because socket rcvbuf limit hit.
    // 在 OFO 中排队但由于达到了 socket rcvbuf 限制而丢弃的数据包
    __tcpext_TCPOFODrop = 0,
    // Number of packets in OFO that were merged with other packets. OFO 中与其他数据包合并的数据包
    __tcpext_TCPOFOMerge = 0,
    // 由于 socket 缓冲区溢出，从无序队列中删除的数据包数量
    /*
慢速路径中，如果不能将数据直接复制到用户态内存，需要加入到 sk_receive_queue 前，会检查 receiver side
memory 是否允许，如果 rcv_buf 不足就可能 prune ofo queue ，此计数器加1。
*/
    __tcpext_OfoPruned = 0,
    // <<<IP TCP connection resets>>> abort 本身是一种很严重的问题，因此有必要关心这些计数器
    // ! TCPAbortOnTimeout、TCPAbortOnLinger、TCPAbortFailed计数器如果不为 0
    // ! 则往往意味着系统发生了较为严重的问题，需要格外注意；
    // https://github.com/ecki/net-tools/blob/bd8bceaed2311651710331a7f8990c3e31be9840/statistics.c
    // 如果在 FIN_WAIT_1 和 FIN_WAIT_2 状态下收到后续数据，或 TCP_LINGER2 设置小于 0 ，则发送 RST
    // 给对端，计数器加 1, 对应设置了 SO_LINGER 且 lingertime 为 0 的情况下，关闭 socket
    // 的情况；此时发送 RST, 对应连接关闭中的情况
    __tcpext_TCPAbortOnData = 0,
    // 如果调用 tcp_close() 关闭 socket 时，recv buffer 中还有数据，则加 1 ，此时会主动发送一个 RST
    // 包给对端, 对应 socket 接收缓冲区尚有数据的情况下，关闭 socket 的的情况；此时发送 RST
    __tcpext_TCPAbortOnClose = 0,
    // 如果 orphan socket 数量或者 tcp_memory_allocated 超过上限，则加 1 ；一般情况下该值为 0
    __tcpext_TCPAbortOnMemory = 0,
    // 因各种计时器 (RTO/PTO/keepalive) 的重传次数超过上限，而关闭连接时，计数器加 1
    __tcpext_TCPAbortOnTimeout = 0,
    // tcp_close()中，因 tp->linger2 被设置小于 0 ，导致 FIN_WAIT_2 立即切换到 CLOSED
    // 状态的次数；一般值为 0
    __tcpext_TCPAbortOnLinger = 0,
    // 如果在准备发送 RST 时，分配 skb 或者发送 skb 失败，则加 1 ；一般值为 0
    __tcpext_TCPAbortFailed = 0,
    // https://perfchron.com/2015/12/26/investigating-linux-network-issues-with-netstat-and-nstat/

    // ! 三路握手最后一步完成之后，Accept queue 队列超过上限时加 1, 只要有数值就代表 accept queue
    // 发生过溢出, 只要有数值就代表 accept queue 发生过溢出
    __tcpext_ListenOverflows = 0,
    /* 任何原因导致的失败后加 1，包括：
        a) 无法找到指定应用（例如监听端口已经不存在）；
        b) 创建 socket 失败；
        c) 分配本地端口失败
        触发点：tcp_v4_syn_recv_sock()
    */
    // !
    __tcpext_ListenDrops = 0,
    // <<<IP TCP memory pressures>>>
    /*tcp_enter_memory_pressure() 在从“非压力状态”切换到“有压力状态”时计数器加 1 ；
    触发点：
        a) tcp_sendmsg()
        b) tcp_sendpage()
        c) tcp_fragment()
        d) tso_fragment()
        e) tcp_mtu_probe()
        f) tcp_data_queue()
     */
    __tcpext_TCPMemoryPressures = 0,
    // syn_table 过载，丢掉 SYN 的次数
    __tcpext_TCPReqQFullDrop = 0,
    // syn_table 过载，进行 SYN cookie 的次数（取决于是否打开 sysctl_tcp_syncookies ）
    __tcpext_TCPReqQFullDoCookies = 0;

static prom_counter_t *__metric_ipext_InOctets = NULL, *__metric_ipext_OutOctets = NULL,
                      *__metric_ipext_InNoRoutes = NULL, *__metric_ipext_inTruncatedPkts = NULL,
                      *__metric_ipext_InCsumErrors = NULL, *__metric_ipext_InMcastOctets = NULL,
                      *__metric_ipext_OutMcastOctets = NULL, *__metric_ipext_InMcastPkts = NULL,
                      *__metric_ipext_OutMcastPkts = NULL, *__metric_ipext_InBcastOctets = NULL,
                      *__metric_ipext_OutBcastOctets = NULL, *__metric_ipext_InBcastPkts = NULL,
                      *__metric_ipext_OutBcastPkts = NULL, *__metric_InNoECTPkts = NULL,
                      *__metric_InECT1Pkts = NULL, *__metric_InECT0Pkts = NULL,
                      *__metric_InCEPkts = NULL, *__metric_tcpext_TCPRenoReorder = NULL,
                      *__metric_tcpext_TCPFACKReorder = NULL,
                      *__metric_tcpext_TCPSACKReorder = NULL, *__metric_tcpext_TCPTSReorder = NULL,
                      *__metric_tcpext_SyncookiesSent = NULL,
                      *__metric_tcpext_SyncookiesRecv = NULL,
                      *__metric_tcpext_SyncookiesFailed = NULL, *__metric_tcpext_TCPOFOQueue = NULL,
                      *__metric_tcpext_TCPOFODrop = NULL, *__metric_tcpext_TCPOFOMerge = NULL,
                      *__metric_tcpext_OfoPruned = NULL, *__metric_tcpext_TCPAbortOnData = NULL,
                      *__metric_tcpext_TCPAbortOnClose = NULL,
                      *__metric_tcpext_TCPAbortOnMemory = NULL,
                      *__metric_tcpext_TCPAbortOnTimeout = NULL,
                      *__metric_tcpext_TCPAbortOnLinger = NULL,
                      *__metric_tcpext_TCPAbortFailed = NULL,
                      *__metric_tcpext_ListenOverflows = NULL, *__metric_tcpext_ListenDrops = NULL,
                      *__metric_tcpext_TCPMemoryPressures = NULL,
                      *__metric_tcpext_TCPReqQFullDrop = NULL,
                      *__metric_tcpext_TCPReqQFullDoCookies = NULL;

static void row_matching_analysis(size_t header_line, size_t values_line, ARL_BASE *base) {
    size_t header_words = procfile_linewords(__pf_netstat, header_line);
    size_t value_words = procfile_linewords(__pf_netstat, values_line);

    if (unlikely(value_words > header_words)) {
        error("File /proc/net/netstat on header line %lu has %lu words, but on value line %lu has "
              "%lu words.",
              header_line, header_words, values_line, value_words);
        value_words = header_words;
    }

    for (size_t w = 1; w < value_words; w++) {
        if (unlikely(arl_check(base, procfile_lineword(__pf_netstat, header_line, w),
                               procfile_lineword(__pf_netstat, values_line, w)))) {
            break;
        }
    }
}

int32_t init_collector_proc_netstat() {
    __arl_ipext = arl_create("proc_ipext", NULL, 3);
    if (unlikely(NULL == __arl_ipext)) {
        return -1;
    }

    __arl_tcpext = arl_create("proc_tcpext", NULL, 3);
    if (unlikely(NULL == __arl_tcpext)) {
        return -1;
    }

    arl_expect(__arl_ipext, "InOctets", &__ipext_InOctets);
    arl_expect(__arl_ipext, "OutOctets", &__ipext_OutOctets);
    arl_expect(__arl_ipext, "InNoRoutes", &__ipext_InNoRoutes);
    arl_expect(__arl_ipext, "InTruncatedPkts", &__ipext_InTruncatedPkts);
    arl_expect(__arl_ipext, "InCsumErrors", &__ipext_InCsumErrors);
    arl_expect(__arl_ipext, "InMcastOctets", &__ipext_InMcastOctets);
    arl_expect(__arl_ipext, "OutMcastOctets", &__ipext_OutMcastOctets);
    arl_expect(__arl_ipext, "InMcastPkts", &__ipext_InMcastPkts);
    arl_expect(__arl_ipext, "OutMcastPkts", &__ipext_OutMcastPkts);
    arl_expect(__arl_ipext, "InBcastOctets", &__ipext_InBcastOctets);
    arl_expect(__arl_ipext, "OutBcastOctets", &__ipext_OutBcastOctets);
    arl_expect(__arl_ipext, "InBcastPkts", &__ipext_InBcastPkts);
    arl_expect(__arl_ipext, "OutBcastPkts", &__ipext_OutBcastPkts);
    arl_expect(__arl_ipext, "InNoECTPkts", &__ipext_InNoECTPkts);
    arl_expect(__arl_ipext, "InECT1Pkts", &__ipext_InECT1Pkts);
    arl_expect(__arl_ipext, "InECT0Pkts", &__ipext_InECT0Pkts);
    arl_expect(__arl_ipext, "InCEPkts", &__ipext_InCEPkts);

    arl_expect(__arl_tcpext, "TCPFACKReorder", &__tcpext_TCPFACKReorder);
    arl_expect(__arl_tcpext, "TCPRenoReorder", &__tcpext_TCPRenoReorder);
    arl_expect(__arl_tcpext, "TCPSACKReorder", &__tcpext_TCPSACKReorder);
    arl_expect(__arl_tcpext, "TCPTSReorder", &__tcpext_TCPTSReorder);
    arl_expect(__arl_tcpext, "SyncookiesSent", &__tcpext_SyncookiesSent);
    arl_expect(__arl_tcpext, "SyncookiesRecv", &__tcpext_SyncookiesRecv);
    arl_expect(__arl_tcpext, "SyncookiesFailed", &__tcpext_SyncookiesFailed);
    arl_expect(__arl_tcpext, "TCPOFOQueue", &__tcpext_TCPOFOQueue);
    arl_expect(__arl_tcpext, "TCPOFODrop", &__tcpext_TCPOFODrop);
    arl_expect(__arl_tcpext, "TCPOFOMerge", &__tcpext_TCPOFOMerge);
    arl_expect(__arl_tcpext, "OfoPruned", &__tcpext_OfoPruned);
    arl_expect(__arl_tcpext, "TCPAbortOnData", &__tcpext_TCPAbortOnData);
    arl_expect(__arl_tcpext, "TCPAbortOnClose", &__tcpext_TCPAbortOnClose);
    arl_expect(__arl_tcpext, "TCPAbortOnMemory", &__tcpext_TCPAbortOnMemory);
    arl_expect(__arl_tcpext, "TCPAbortOnTimeout", &__tcpext_TCPAbortOnTimeout);
    arl_expect(__arl_tcpext, "TCPAbortOnLinger", &__tcpext_TCPAbortOnLinger);
    arl_expect(__arl_tcpext, "TCPAbortFailed", &__tcpext_TCPAbortFailed);
    arl_expect(__arl_tcpext, "ListenOverflows", &__tcpext_ListenOverflows);
    arl_expect(__arl_tcpext, "ListenDrops", &__tcpext_ListenDrops);
    arl_expect(__arl_tcpext, "TCPMemoryPressures", &__tcpext_TCPMemoryPressures);
    arl_expect(__arl_tcpext, "TCPReqQFullDrop", &__tcpext_TCPReqQFullDrop);
    arl_expect(__arl_tcpext, "TCPReqQFullDoCookies", &__tcpext_TCPReqQFullDoCookies);

    // 初始化指标
    // IP Bandwidth
    __metric_ipext_InOctets = prom_collector_registry_must_register_metric(
        prom_counter_new("InOctets", "IP Bandwidth, Number of received octets", 2,
                         (const char *[]){ "host", "netstat" }));
    __metric_ipext_OutOctets = prom_collector_registry_must_register_metric(prom_counter_new(
        "OutOctets", "IP Bandwidth, send IP bytes", 2, (const char *[]){ "host", "netstat" }));

    // IP Input Errors
    __metric_ipext_InNoRoutes = prom_collector_registry_must_register_metric(prom_counter_new(
        "InNoRoutes",
        "IP Input Errors, Number of IP datagrams discarded due to no routes in forwarding path", 2,
        (const char *[]){ "host", "netstat" }));
    __metric_ipext_inTruncatedPkts = prom_collector_registry_must_register_metric(prom_counter_new(
        "inTruncatedPkts",
        "IP Input Errors, Number of IP datagrams discarded due to frame not carrying enough data",
        2, (const char *[]){ "host", "netstat" }));
    __metric_ipext_InCsumErrors = prom_collector_registry_must_register_metric(prom_counter_new(
        "InCsumErrors", "IP Input Errors, Number of IP datagrams discarded due to checksum error",
        2, (const char *[]){ "host", "netstat" }));

    // IP Multicast Bandwidth
    __metric_ipext_InMcastOctets = prom_collector_registry_must_register_metric(
        prom_counter_new("InMcastOctets", "Number of received multicast octets", 2,
                         (const char *[]){ "host", "netstat" }));
    __metric_ipext_OutMcastOctets = prom_collector_registry_must_register_metric(
        prom_counter_new("OutMcastOctets", "Number of sent IP multicast octets", 2,
                         (const char *[]){ "host", "netstat" }));
    __metric_ipext_InMcastPkts = prom_collector_registry_must_register_metric(
        prom_counter_new("InMcastPkts", "Number of received IP multicast datagrams", 2,
                         (const char *[]){ "host", "netstat" }));
    __metric_ipext_OutMcastPkts = prom_collector_registry_must_register_metric(
        prom_counter_new("OutMcastPkts", "Number of sent IP multicast datagrams", 2,
                         (const char *[]){ "host", "netstat" }));

    // broadcast IP Broadcast Bandwidth kilobits/s
    __metric_ipext_InBcastOctets = prom_collector_registry_must_register_metric(
        prom_counter_new("InBcastOctets", "Number of received IP broadcast octets", 2,
                         (const char *[]){ "host", "netstat" }));
    __metric_ipext_OutBcastOctets = prom_collector_registry_must_register_metric(
        prom_counter_new("OutBcastOctets", "Number of sent IP broadcast octets", 2,
                         (const char *[]){ "host", "netstat" }));
    // bcastpkts IP Broadcast Packets packets/s
    __metric_ipext_InBcastPkts = prom_collector_registry_must_register_metric(
        prom_counter_new("InBcastPkts", "Number of received IP broadcast datagrams", 2,
                         (const char *[]){ "host", "netstat" }));
    __metric_ipext_OutBcastPkts = prom_collector_registry_must_register_metric(
        prom_counter_new("OutBcastPkts", "Number of sent IP broadcast datagrams", 2,
                         (const char *[]){ "host", "netstat" }));

    // IP ECN Statistics
    __metric_InNoECTPkts = prom_collector_registry_must_register_metric(
        prom_counter_new("InNoECTPkts", "Number of received IP datagrams discarded due to no ECT",
                         2, (const char *[]){ "host", "netstat" }));
    __metric_InECT1Pkts = prom_collector_registry_must_register_metric(
        prom_counter_new("InECT1Pkts", "Number of received IP datagrams discarded due to ECT=1", 2,
                         (const char *[]){ "host", "netstat" }));
    __metric_InECT0Pkts = prom_collector_registry_must_register_metric(
        prom_counter_new("InECT0Pkts", "Number of received IP datagrams discarded due to ECT=0", 2,
                         (const char *[]){ "host", "netstat" }));
    __metric_InCEPkts = prom_collector_registry_must_register_metric(
        prom_counter_new("InCEPkts", "Number of received IP datagrams discarded due to CE", 2,
                         (const char *[]){ "host", "netstat" }));

    // tcpreorders tcp乱序 counter类型
    __metric_tcpext_TCPRenoReorder = prom_collector_registry_must_register_metric(prom_counter_new(
        "TCPRenoReorder", "TCP Reordering, reno", 2, (const char *[]){ "host", "netstat" }));
    __metric_tcpext_TCPFACKReorder = prom_collector_registry_must_register_metric(prom_counter_new(
        "TCPFACKReorder", "TCP Reordering, fack", 2, (const char *[]){ "host", "netstat" }));
    __metric_tcpext_TCPSACKReorder = prom_collector_registry_must_register_metric(prom_counter_new(
        "TCPSACKReorder", "TCP Reordering, sack", 2, (const char *[]){ "host", "netstat" }));
    __metric_tcpext_TCPTSReorder = prom_collector_registry_must_register_metric(prom_counter_new(
        "TCPTSReorder", "TCP Reordering, timestamp", 2, (const char *[]){ "host", "netstat" }));

    // tcpsyncookies TCP SYN Cookies packets/s
    __metric_tcpext_SyncookiesSent = prom_collector_registry_must_register_metric(prom_counter_new(
        "SyncookiesSent", "TCP Syncookies sent", 2, (const char *[]){ "host", "netstat" }));
    __metric_tcpext_SyncookiesRecv = prom_collector_registry_must_register_metric(prom_counter_new(
        "SyncookiesRecv", "TCP Syncookies received", 2, (const char *[]){ "host", "netstat" }));
    __metric_tcpext_SyncookiesFailed =
        prom_collector_registry_must_register_metric(prom_counter_new(
            "SyncookiesFailed", "TCP Syncookies failed", 2, (const char *[]){ "host", "netstat" }));

    // tcpofo tcp out of order
    __metric_tcpext_TCPOFOQueue = prom_collector_registry_must_register_metric(
        prom_counter_new("TCPOFOQueue", "TCP Out of Order queue inqueue packets/s", 2,
                         (const char *[]){ "host", "netstat" }));
    __metric_tcpext_TCPOFODrop = prom_collector_registry_must_register_metric(
        prom_counter_new("TCPOFODrop", "TCP Out of Order queue drop packets/s", 2,
                         (const char *[]){ "host", "netstat" }));
    __metric_tcpext_TCPOFOMerge = prom_collector_registry_must_register_metric(
        prom_counter_new("TCPOFOMerge", "TCP Out of Order queue merge packets/s", 2,
                         (const char *[]){ "host", "netstat" }));
    __metric_tcpext_OfoPruned = prom_collector_registry_must_register_metric(
        prom_counter_new("OfoPruned", "TCP Out of Order queue pruned packets/s", 2,
                         (const char *[]){ "host", "netstat" }));

    // tcpconnaborts 对应连接关闭情况
    // https://satori-monitoring.readthedocs.io/zh/latest/builtin-metrics/tcpext.html?highlight=TCPAbortOnData#id6
    __metric_tcpext_TCPAbortOnData = prom_collector_registry_must_register_metric(prom_counter_new(
        "TCPAbortOnData",
        "TCP Connection Aborts, Number of times the socket was closed due to unknown data received",
        2, (const char *[]){ "host", "netstat" }));
    // 用户态程序在缓冲区内还有数据时关闭 socket 的次数
    __metric_tcpext_TCPAbortOnClose = prom_collector_registry_must_register_metric(
        prom_counter_new("TCPAbortOnClose",
                         "TCP Connection Aborts, The number of times the socket is closed when the "
                         "user state program still has data in the buffer",
                         2, (const char *[]){ "host", "netstat" }));
    __metric_tcpext_TCPAbortOnMemory = prom_collector_registry_must_register_metric(
        prom_counter_new("TCPAbortOnMemory",
                         "TCP Connection Aborts, The number of times the connection was closed due "
                         "to memory problems",
                         2, (const char *[]){ "host", "netstat" }));
    __metric_tcpext_TCPAbortOnTimeout = prom_collector_registry_must_register_metric(
        prom_counter_new("TCPAbortOnTimeout",
                         "TCP Connection Aborts, The number of times the connection is closed "
                         "because the number of retransmissions of various timers (RTO / PTO / "
                         "keepalive) exceeds the upper limit",
                         2, (const char *[]){ "host", "netstat" }));
    __metric_tcpext_TCPAbortOnLinger = prom_collector_registry_must_register_metric(
        prom_counter_new("TCPAbortOnLinger", "TCP Connection Aborts, linger", 2,
                         (const char *[]){ "host", "netstat" }));
    __metric_tcpext_TCPAbortFailed = prom_collector_registry_must_register_metric(prom_counter_new(
        "TCPAbortFailed", "TCP Connection Aborts, Number of failed attempts to end the connection",
        2, (const char *[]){ "host", "netstat" }));

    // tcp_accept_queue
    __metric_tcpext_ListenOverflows = prom_collector_registry_must_register_metric(
        prom_counter_new("TCPAcceptQueue", "TCP Accept Queue Issues, overflows packets/s", 2,
                         (const char *[]){ "host", "netstat" }));
    __metric_tcpext_ListenDrops = prom_collector_registry_must_register_metric(
        prom_counter_new("TCPAcceptQueueDrop", "TCP Accept Queue Issues, drops packets/s", 2,
                         (const char *[]){ "host", "netstat" }));

    // tcpmemorypressures
    __metric_tcpext_TCPMemoryPressures = prom_collector_registry_must_register_metric(
        prom_counter_new("TCPMemoryPressures",
                         "TCP Memory Pressures, Number of times a socket was put in \"memory "
                         "pressure\" due to a non fatal memory allocation failure, events/s",
                         2, (const char *[]){ "host", "netstat" }));

    // tcp_syn_queue
    __metric_tcpext_TCPReqQFullDrop = prom_collector_registry_must_register_metric(prom_counter_new(
        "TCPBacklogDrop",
        "TCP SYN Queue Issues, syn_table overload, number of times SYN is lost, packets/s", 2,
        (const char *[]){ "host", "netstat" }));
    __metric_tcpext_TCPReqQFullDoCookies =
        prom_collector_registry_must_register_metric(prom_counter_new(
            "TCPBacklogDoCookies",
            "TCP SYN Queue Issues, syn_table overload, number of syn cookies, packets/s", 2,
            (const char *[]){ "host", "netstat" }));

    debug("[PLUGIN_PROC:proc_netstat] init successed");
    return 0;
}

int32_t collector_proc_netstat(int32_t update_every, usec_t dt, const char *config_path) {
    debug("[PLUGIN_PROC:proc_netstat] config:%s running", config_path);

    const char *f_netstat =
        appconfig_get_member_str(config_path, "monitor_file", __proc_netstat_filename);

    if (unlikely(!__pf_netstat)) {
        __pf_netstat = procfile_open(f_netstat, " \t:", PROCFILE_FLAG_DEFAULT);
        if (unlikely(!__pf_netstat)) {
            error("Cannot open %s", f_netstat);
            return -1;
        }
    }

    __pf_netstat = procfile_readall(__pf_netstat);
    if (unlikely(!__pf_netstat)) {
        error("Cannot read %s", f_netstat);
        return -1;
    }

    size_t lines = procfile_lines(__pf_netstat);

    arl_begin(__arl_ipext);
    arl_begin(__arl_tcpext);

    return 0;
}

void fini_collector_proc_netstat() {
    if (likely(!__arl_ipext)) {
        arl_free(__arl_ipext);
        __arl_ipext = NULL;
    }

    if (likely(!__arl_tcpext)) {
        arl_free(__arl_tcpext);
        __arl_tcpext = NULL;
    }

    if (likely(!__pf_netstat)) {
        procfile_close(__pf_netstat);
        __pf_netstat = NULL;
    }
    debug("[PLUGIN_PROC:proc_netstat] stopped");
}
