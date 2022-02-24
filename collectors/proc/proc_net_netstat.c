/*
 * @Author: CALM.WU
 * @Date: 2022-02-21 11:09:55
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-02-24 19:03:20
 */

// https://github.com/moooofly/MarkSomethingDown/blob/master/Linux/TCP%20%E7%9B%B8%E5%85%B3%E7%BB%9F%E8%AE%A1%E4%BF%A1%E6%81%AF%E8%AF%A6%E8%A7%A3.md
// https://perthcharles.github.io/2015/11/10/wiki-netstat-proc/
// http://blog.chinaunix.net/uid-20043340-id-3016560.html

#include "prometheus-client-c/prom.h"

#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/strings.h"
#include "utils/clocks.h"

#include "appconfig/appconfig.h"

static const char       *__proc_netstat_filename = "/proc/net/netstat";
static struct proc_file *__pf_netstat = NULL;

static uint64_t
    // IP bandwidth 系统收到IP数据报总字节数
    __ipext_InOctets = 0,
    // 系统发送的IP数据报总字节数
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
    __tcpext_TCPOFOQueue = 0,
    //
    __tcpext_TCPOFODrop = 0,
    //
    __tcpext_TCPOFOMerge = 0,
    //
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

static prom_gauge_t *__metric_ipext_InOctets = NULL, *__metric_ipext_OutOctets = NULL,
                    *__metric_ipext_InNoRoutes = NULL, *__metric_ipext_inTruncatedPkts = NULL,
                    *__metric_ipext_InCsumErrors = NULL, *__metric_ipext_InMcastOctets = NULL,
                    *__metric_ipext_OutMcastOctets = NULL, *__metric_ipext_InMcastPkts = NULL,
                    *__metric_ipext_OutMcastPkts = NULL, *__metric_ipext_InBcastOctets = NULL,
                    *__metric_ipext_OutBcastOctets = NULL, *__metric_ipext_InBcastPkts = NULL,
                    *__metric_ipext_OutBcastPkts = NULL, *__metric_InNoECTPkts = NULL,
                    *__metric_InECT1Pkts = NULL, *__metric_InECT0Pkts = NULL,
                    *__metric_InCEPkts = NULL, *__metric_tcpext_TCPRenoReorder = NULL,
                    *__metric_tcpext_TCPFACKReorder = NULL, *__metric_tcpext_TCPSACKReorder = NULL,
                    *__metric_tcpext_TCPTSReorder = NULL, *__metric_tcpext_SyncookiesSent = NULL,
                    *__metric_tcpext_SyncookiesRecv = NULL,
                    *__metric_tcpext_SyncookiesFailed = NULL, *__metric_tcpext_TCPOFOQueue = NULL,
                    *__metric_tcpext_TCPOFODrop = NULL, *__metric_tcpext_TCPOFOMerge = NULL,
                    *__metric_tcpext_OfoPruned = NULL, *__metric_tcpext_TCPAbortOnData = NULL,
                    *__metric_tcpext_TCPAbortOnClose = NULL,
                    *__metric_tcpext_TCPAbortOnMemory = NULL,
                    *__metric_tcpext_TCPAbortOnTimeout = NULL,
                    *__metric_tcpext_TCPAbortOnLinger = NULL,
                    *__metric_tcpext_TCPAbortFailed = NULL, *__metric_tcpext_ListenOverflows = NULL,
                    *__metric_tcpext_ListenDrops = NULL, *__metric_tcpext_TCPMemoryPressures = NULL,
                    *__metric_tcpext_TCPReqQFullDrop = NULL,
                    *__metric_tcpext_TCPReqQFullDoCookies = NULL;

int32_t init_collector_proc_netstat() {
    // 初始化指标
    __metric_ipext_InOctets = prom_collector_registry_must_register_metric(
        prom_gauge_new("InOctets", "IP Bandwidth, Number of received octets"， 2,
                       (const char *[]){ "host", "netstat" }));
    __metric_ipext_OutOctets = prom_collector_registry_must_register_metric(prom_gauge_new(
        "OutOctets", "IP Bandwidth, send IP bytes", 2, (const char *[]){ "host", "netstat" }));

    __metric_ipext_InNoRoutes = prom_collector_registry_must_register_metric(prom_gauge_new(
        "InNoRoutes",
        "IP Input Errors, Number of IP datagrams discarded due to no routes in forwarding path", 2,
        (const char *[]){ "host", "netstat" }));
    __metric_ipext_inTruncatedPkts = prom_collector_registry_must_register_metric(prom_gauge_new(
        "inTruncatedPkts",
        "IP Input Errors, Number of IP datagrams discarded due to frame not carrying enough data",
        2, (const char *[]){ "host", "netstat" }));
    __metric_ipext_InCsumErrors = prom_collector_registry_must_register_metric(prom_gauge_new(
        "InCsumErrors", "IP Input Errors, Number of IP datagrams discarded due to checksum error",
        2, (const char *[]){ "host", "netstat" }));

    __metric_ipext_InMcastOctets = prom_collector_registry_must_register_metric(
        prom_gauge_new("InMcastOctets", "Number of received multicast octets", 2,
                       (const char *[]){ "host", "netstat" }));
    __metric_ipext_OutMcastOctets = prom_collector_registry_must_register_metric(
        prom_gauge_new("OutMcastOctets", "Number of sent IP multicast octets", 2,
                       (const char *[]){ "host", "netstat" }));
    __metric_ipext_InMcastPkts = prom_collector_registry_must_register_metric(
        prom_gauge_new("InMcastPkts", "Number of received IP multicast datagrams", 2,
                       (const char *[]){ "host", "netstat" }));
    __metric_ipext_OutMcastPkts = prom_collector_registry_must_register_metric(
        prom_gauge_new("OutMcastPkts", "Number of sent IP multicast datagrams", 2,
                       (const char *[]){ "host", "netstat" }));

    __metric_ipext_InBcastOctets = prom_collector_registry_must_register_metric(
        prom_gauge_new("InBcastOctets", "Number of received IP broadcast octets", 2,
                       (const char *[]){ "host", "netstat" }));
    __metric_ipext_OutBcastOctets = prom_collector_registry_must_register_metric(
        prom_gauge_new("OutBcastOctets", "Number of sent IP broadcast octets", 2,
                       (const char *[]){ "host", "netstat" }));
    __metric_ipext_InBcastPkts = prom_collector_registry_must_register_metric(
        prom_gauge_new("InBcastPkts", "Number of received IP broadcast datagrams", 2,
                       (const char *[]){ "host", "netstat" }));
    __metric_ipext_OutBcastPkts = prom_collector_registry_must_register_metric(
        prom_gauge_new("OutBcastPkts", "Number of sent IP broadcast datagrams", 2,
                       (const char *[]){ "host", "netstat" }));

    __metric_InNoECTPkts = prom_collector_registry_must_register_metric(
        prom_gauge_new("InNoECTPkts", "Number of received IP datagrams discarded due to no ECT", 2,
                       (const char *[]){ "host", "netstat" }));

    debug("[PLUGIN_PROC:proc_netstat] init successed");
    return 0;
}

int32_t collector_proc_netstat(int32_t update_every, usec_t dt, const char *config_path) {
    int32_t ret = 0;
    return ret;
}

void fini_collector_proc_netstat() {
}
