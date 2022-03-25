/*
 * @Author: CALM.WU
 * @Date: 2022-02-24 11:17:30
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-03-24 16:04:26
 */

#include "prometheus-client-c/prom.h"

#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/strings.h"
#include "utils/clocks.h"
#include "utils/adaptive_resortable_list.h"
#include "utils/os.h"

#include "appconfig/appconfig.h"

static const char       *__proc_net_socksat_filename = "/proc/net/sockstat";
static struct proc_file *__pf_net_sockstat = NULL;
static ARL_BASE *__arl_sockets = NULL, *__arl_tcp = NULL, *__arl_udp = NULL, *__arl_udplite = NULL,
                *__arl_raw = NULL, *__arl_frag = NULL;

/*
两种情况会出发 “Out of socket memory” 的信息：
1.有很多的孤儿套接字(orphan sockets)
2.tcp socket 用尽了给他分配的内存
*/

static struct proc_net_sockstat {
    uint64_t sockets_used;   // 已使用的所有协议套接字总量

    uint64_t tcp_inuse;   // 正在使用（正在侦听）的TCP套接字数量 netstat –lnt | grep ^tcp | wc –l
    uint64_t tcp_orphan;   // 无主（不属于任何进程）的TCP连接数（无用、待销毁的TCP socket数）
    uint64_t tcp_tw;   // 等待关闭的TCP连接数。其值等于netstat –ant | grep TIME_WAIT | wc –l
    uint64_t tcp_alloc;   // 已分配（已建立、已申请到sk_buff）的TCP套接字数量。其值等于netstat –ant
                          // | grep ^tcp | wc –l
    uint64_t tcp_mem;   // 套接字缓冲区使用量 套接字缓冲区使用量（单位页） the socket descriptors
                        // in-kernel send queues (stuff waiting to be sent out by the NIC) in-kernel
                        // receive queues (stuff that's been received, but hasn't yet been read by
                        // user space yet).

    uint64_t udp_inuse;   // 正在使用的UDP套接字数量
    uint64_t udp_mem;

    uint64_t raw_inuse;

    uint64_t frag_inuse;
    uint64_t frag_memory;

    uint64_t tcp_mem_low_threshold;   // proc/sys/net/ipv4/tcp_mem 这个单位是page，统一转换为kB
    uint64_t tcp_mem_pressure_threshold;
    uint64_t tcp_mem_high_threshold;
    uint64_t tcp_max_orphans;
} __proc_net_sockstat = { 0 };

static prom_gauge_t *__metric_sockstat_sockets_used = NULL, *__metric_sockstat_tcp_inuse = NULL,
                    *__metric_sockstat_tcp_orphan = NULL, *__metric_sockstat_tcp_tw = NULL,
                    *__metric_sockstat_tcp_alloc = NULL, *__metric_sockstat_tcp_mem = NULL,
                    *__metric_sockstat_udp_inuse = NULL, *__metric_sockstat_udp_mem = NULL,
                    *__metric_sockstat_raw_inuse = NULL, *__metric_sockstat_frag_inuse = NULL,
                    *__metric_sockstat_frag_memory = NULL,
                    *__metric_sockstat_tcp_mem_low_threshold = NULL,
                    *__metric_sockstat_tcp_mem_pressure_threshold = NULL,
                    *__metric_sockstat_tcp_mem_high_threshold = NULL,
                    *__metric_sockstat_tcp_max_orphans = NULL;

int32_t init_collector_proc_net_sockstat() {
    __arl_sockets = arl_create("sockstat/sockets", NULL, 3);
    if (unlikely(NULL == __arl_sockets)) {
        return -1;
    }

    __arl_tcp = arl_create("sockstat/tcp", NULL, 3);
    if (unlikely(NULL == __arl_tcp)) {
        return -1;
    }

    __arl_udp = arl_create("sockstat/udp", NULL, 3);
    if (unlikely(NULL == __arl_udp)) {
        return -1;
    }

    __arl_udplite = arl_create("sockstat/udplite", NULL, 3);
    if (unlikely(NULL == __arl_udplite)) {
        return -1;
    }

    __arl_raw = arl_create("sockstat/raw", NULL, 3);
    if (unlikely(NULL == __arl_raw)) {
        return -1;
    }

    __arl_frag = arl_create("sockstat/frag", NULL, 3);
    if (unlikely(NULL == __arl_frag)) {
        return -1;
    }

    // arl list绑定
    arl_expect(__arl_sockets, "used", &__proc_net_sockstat.sockets_used);
    arl_expect(__arl_tcp, "inuse", &__proc_net_sockstat.tcp_inuse);
    arl_expect(__arl_tcp, "orphan", &__proc_net_sockstat.tcp_orphan);
    arl_expect(__arl_tcp, "tw", &__proc_net_sockstat.tcp_tw);
    arl_expect(__arl_tcp, "alloc", &__proc_net_sockstat.tcp_alloc);
    arl_expect(__arl_tcp, "mem", &__proc_net_sockstat.tcp_mem);
    arl_expect(__arl_udp, "inuse", &__proc_net_sockstat.udp_inuse);
    arl_expect(__arl_udp, "mem", &__proc_net_sockstat.udp_mem);
    arl_expect(__arl_raw, "inuse", &__proc_net_sockstat.raw_inuse);
    arl_expect(__arl_frag, "inuse", &__proc_net_sockstat.frag_inuse);
    arl_expect(__arl_frag, "memory", &__proc_net_sockstat.frag_memory);

    // 初始化指标
    __metric_sockstat_sockets_used = prom_collector_registry_must_register_metric(prom_gauge_new(
        "node_sockstat_sockets_used", "IPv4 Sockets Used", 1, (const char *[]){ "sockstat" }));
    __metric_sockstat_tcp_inuse = prom_collector_registry_must_register_metric(prom_gauge_new(
        "node_sockstat_TCP_inuse", "IPv4 TCP Sockets inuse", 1, (const char *[]){ "sockstat" }));
    __metric_sockstat_tcp_orphan = prom_collector_registry_must_register_metric(prom_gauge_new(
        "node_sockstat_TCP_orphan", "IPv4 TCP Sockets orphan", 1, (const char *[]){ "sockstat" }));
    __metric_sockstat_tcp_tw = prom_collector_registry_must_register_metric(prom_gauge_new(
        "node_sockstat_TCP_tw", "IPv4 TCP Sockets timewait", 1, (const char *[]){ "sockstat" }));
    __metric_sockstat_tcp_alloc = prom_collector_registry_must_register_metric(prom_gauge_new(
        "node_sockstat_TCP_alloc", "IPv4 TCP Sockets alloc", 1, (const char *[]){ "sockstat" }));
    __metric_sockstat_tcp_mem = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_sockstat_TCP_mem", "IPv4 TCP Sockets Memory, KiB", 1,
                       (const char *[]){ "sockstat" }));
    __metric_sockstat_udp_inuse = prom_collector_registry_must_register_metric(prom_gauge_new(
        "node_sockstat_UDP_inuse", "IPv4 UDP Sockets inuse", 1, (const char *[]){ "sockstat" }));
    __metric_sockstat_udp_mem = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_sockstat_UDP_mem", "IPv4 UDP Sockets Memory, KiB", 1,
                       (const char *[]){ "sockstat" }));
    __metric_sockstat_raw_inuse = prom_collector_registry_must_register_metric(prom_gauge_new(
        "node_sockstat_RAW_inuse", "IPv4 RAW Sockets inuse", 1, (const char *[]){ "sockstat" }));
    __metric_sockstat_frag_memory = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_sockstat_FRAG_memory", "IPv4 FRAG Sockets Memory, KiB", 1,
                       (const char *[]){ "sockstat" }));
    __metric_sockstat_frag_inuse = prom_collector_registry_must_register_metric(prom_gauge_new(
        "node_sockstat_FRAG_inuse", "IPv4 FRAG Sockets inuse", 1, (const char *[]){ "sockstat" }));

    __metric_sockstat_tcp_mem_low_threshold = prom_collector_registry_must_register_metric(
        prom_gauge_new("tcp_mem_low_threshold", "IPv4 TCP Sockets Memory Low Threshold, KiB", 1,
                       (const char *[]){ "sockstat" }));
    __metric_sockstat_tcp_mem_high_threshold = prom_collector_registry_must_register_metric(
        prom_gauge_new("tcp_mem_high_threshold", "IPv4 TCP Sockets Memory High Threshold, KiB", 1,
                       (const char *[]){ "sockstat" }));
    __metric_sockstat_tcp_mem_pressure_threshold =
        prom_collector_registry_must_register_metric(prom_gauge_new(
            "node_tcp_mem_pressure_threshold", "IPv4 TCP Sockets Memory Pressure Threshold, KiB", 1,
            (const char *[]){ "sockstat" }));
    __metric_sockstat_tcp_max_orphans = prom_collector_registry_must_register_metric(prom_gauge_new(
        "node_tcp_max_orphans", "IPv4 TCP Sockets Max Orphans", 1, (const char *[]){ "sockstat" }));

    // 直接设置指标的值
    int32_t pg_size_kb = get_pgsize_kb();
    read_tcp_mem(&__proc_net_sockstat.tcp_mem_low_threshold,
                 &__proc_net_sockstat.tcp_mem_pressure_threshold,
                 &__proc_net_sockstat.tcp_mem_high_threshold);
    read_tcp_max_orphans(&__proc_net_sockstat.tcp_max_orphans);

    prom_gauge_set(__metric_sockstat_tcp_mem_low_threshold,
                   __proc_net_sockstat.tcp_mem_low_threshold * pg_size_kb,
                   (const char *[]){ "tcp" });
    prom_gauge_set(__metric_sockstat_tcp_mem_pressure_threshold,
                   __proc_net_sockstat.tcp_mem_pressure_threshold * pg_size_kb,
                   (const char *[]){ "tcp" });
    prom_gauge_set(__metric_sockstat_tcp_mem_high_threshold,
                   __proc_net_sockstat.tcp_mem_high_threshold * pg_size_kb,
                   (const char *[]){ "tcp" });
    prom_gauge_set(__metric_sockstat_tcp_max_orphans, __proc_net_sockstat.tcp_max_orphans,
                   (const char *[]){ "tcp" });

    debug("[PLUGIN_PROC:proc_net_sockstat] init successed");
    return 0;
}

int32_t collector_proc_net_sockstat(int32_t UNUSED(update_every), usec_t UNUSED(dt),
                                    const char *config_path) {
    debug("[PLUGIN_PROC:proc_net_sockstat] config:%s running", config_path);

    const char *f_sockstat =
        appconfig_get_member_str(config_path, "monitor_file", __proc_net_socksat_filename);

    if (unlikely(!__pf_net_sockstat)) {
        __pf_net_sockstat = procfile_open(f_sockstat, " \t:", PROCFILE_FLAG_DEFAULT);
        if (unlikely(!__pf_net_sockstat)) {
            error("Cannot open %s", f_sockstat);
            return -1;
        }
    }

    __pf_net_sockstat = procfile_readall(__pf_net_sockstat);
    if (unlikely(!__pf_net_sockstat)) {
        error("Cannot read %s", f_sockstat);
        return -1;
    }

    size_t lines = procfile_lines(__pf_net_sockstat);

    arl_begin(__arl_sockets);
    arl_begin(__arl_tcp);
    arl_begin(__arl_udp);
    arl_begin(__arl_raw);
    arl_begin(__arl_frag);
    arl_begin(__arl_udplite);

    for (size_t l = 0; l < lines; l++) {
        size_t      words = procfile_linewords(__pf_net_sockstat, l);
        const char *key = procfile_lineword(__pf_net_sockstat, l, 0);

        size_t    w = 1;
        ARL_BASE *base = NULL;
        if (strncmp("sockets", key, 7) == 0) {
            base = __arl_sockets;
        } else if (strncmp("TCP", key, 3) == 0) {
            base = __arl_tcp;
        } else if (strncmp("UDP", key, 3) == 0) {
            base = __arl_udp;
        } else if (strncmp("RAW", key, 3) == 0) {
            base = __arl_raw;
        } else if (strncmp("FRAG", key, 4) == 0) {
            base = __arl_frag;
        } else if (strncmp("UDPLITE", key, 7) == 0) {
            base = __arl_udplite;
        } else {
            continue;
        }

        while (w + 1 < words) {
            const char *name = procfile_lineword(__pf_net_sockstat, l, w);
            w++;
            const char *value = procfile_lineword(__pf_net_sockstat, l, w);
            w++;
            if (unlikely(0 != arl_check(base, name, value))) {
                break;
            }
        }
    }

    // 设置指标值
    int32_t pg_size_kb = get_pgsize_kb();
    prom_gauge_set(__metric_sockstat_sockets_used, __proc_net_sockstat.sockets_used,
                   (const char *[]){ "sockets" });

    prom_gauge_set(__metric_sockstat_tcp_inuse, __proc_net_sockstat.tcp_inuse,
                   (const char *[]){ "tcp" });
    prom_gauge_set(__metric_sockstat_tcp_orphan, __proc_net_sockstat.tcp_orphan,
                   (const char *[]){ "tcp" });
    prom_gauge_set(__metric_sockstat_tcp_tw, __proc_net_sockstat.tcp_tw, (const char *[]){ "tcp" });
    prom_gauge_set(__metric_sockstat_tcp_alloc, __proc_net_sockstat.tcp_alloc,
                   (const char *[]){ "tcp" });
    prom_gauge_set(__metric_sockstat_tcp_mem, __proc_net_sockstat.tcp_mem * pg_size_kb,
                   (const char *[]){ "tcp" });

    prom_gauge_set(__metric_sockstat_udp_inuse, __proc_net_sockstat.udp_inuse,
                   (const char *[]){ "udp" });
    prom_gauge_set(__metric_sockstat_udp_mem, __proc_net_sockstat.udp_mem * pg_size_kb,
                   (const char *[]){ "udp" });

    prom_gauge_set(__metric_sockstat_raw_inuse, __proc_net_sockstat.raw_inuse,
                   (const char *[]){ "raw_sockets" });

    prom_gauge_set(__metric_sockstat_frag_inuse, __proc_net_sockstat.frag_inuse,
                   (const char *[]){ "frag_sockets" });
    prom_gauge_set(__metric_sockstat_frag_memory, __proc_net_sockstat.frag_memory * pg_size_kb,
                   (const char *[]){ "frag_sockets" });

    debug("[PLUGIN_PROC:proc_net_sockstat] socket_used: %lu, tcp_inuse: %lu, tcp_orphan: %lu, "
          "tcp_tw: %lu, tcp_alloc: %lu, tcp_mem: %lu, udp_inuse: %lu, udp_mem: %lu, raw_inuse: "
          "%lu, frag_inuse: %lu, frag_memory: %lu",
          __proc_net_sockstat.sockets_used, __proc_net_sockstat.tcp_inuse,
          __proc_net_sockstat.tcp_orphan, __proc_net_sockstat.tcp_tw, __proc_net_sockstat.tcp_alloc,
          __proc_net_sockstat.tcp_mem, __proc_net_sockstat.udp_inuse, __proc_net_sockstat.udp_mem,
          __proc_net_sockstat.raw_inuse, __proc_net_sockstat.frag_inuse,
          __proc_net_sockstat.frag_memory);

    return 0;
}

void fini_collector_proc_net_sockstat() {
    if (likely(__arl_sockets)) {
        arl_free(__arl_sockets);
        __arl_sockets = NULL;
    }

    if (likely(__arl_tcp)) {
        arl_free(__arl_tcp);
        __arl_tcp = NULL;
    }

    if (likely(__arl_udp)) {
        arl_free(__arl_udp);
        __arl_udp = NULL;
    }

    if (likely(__arl_raw)) {
        arl_free(__arl_raw);
        __arl_raw = NULL;
    }

    if (likely(__arl_frag)) {
        arl_free(__arl_frag);
        __arl_frag = NULL;
    }

    if (likely(__arl_udplite)) {
        arl_free(__arl_udplite);
        __arl_udplite = NULL;
    }

    if (likely(__pf_net_sockstat)) {
        procfile_close(__pf_net_sockstat);
        __pf_net_sockstat = NULL;
    }

    debug("[PLUGIN_PROC:proc_net_sockstat] stopped");
}