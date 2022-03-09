/*
 * @Author: CALM.WU
 * @Date: 2022-02-21 11:10:12
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-02-21 17:15:48
 */

// https://stackoverflow.com/questions/3521678/what-are-meanings-of-fields-in-proc-net-dev

#include "prometheus-client-c/prom.h"

#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/strings.h"
#include "utils/clocks.h"

#include "appconfig/appconfig.h"

static const char       *__proc_net_dev_filename = "/proc/net/dev";
static struct proc_file *__pf_net_dev = NULL;

static const char *__metric_help_net_dev_rx_bytes =
    "The total number of bytes of data received by the interface.";
static const char *__metric_help_net_dev_rx_packets =
    "The total number of packets of data received by the interface";
static const char *__metric_help_net_dev_rx_errors =
    "The total number of receive errors detected by the device driver.";
static const char *__metric_help_net_dev_rx_dropped =
    "The total number of receive packets dropped by the device driver.";
static const char *__metric_help_net_dev_rx_fifo_errors =
    "The number of receive FIFO buffer errors.";
static const char *__metric_help_net_dev_rx_frame_errors = "The number of receive framing errors.";
static const char *__metric_help_net_dev_rx_compressed =
    "The number of compressed packets received by the device driver. ";
static const char *__metric_help_net_dev_rx_multicast =
    "The number of multicast frames received by the device driver.";
static const char *__metric_help_net_dev_tx_bytes =
    "The total number of bytes of data transmitted by the interface.";
static const char *__metric_help_net_dev_tx_packets =
    "The total number of packets of data transmitted by the interface.";
static const char *__metric_help_net_dev_tx_errors =
    "The total number of transmit errors detected by the device driver.";
static const char *__metric_help_net_dev_tx_dropped =
    "The total number of transmit packets dropped by the device driver.";
static const char *__metric_help_net_dev_tx_fifo_errors =
    "The number of transmit FIFO buffer errors.";
static const char *__metric_help_net_dev_tx_collisions =
    "The number of collisions detected on the interface.";
static const char *__metric_help_net_dev_tx_carrier_errors =
    "The number of carrier losses detected by the device driver.";
static const char *__metric_help_net_dev_tx_compressed =
    "The number of compressed packets transmitted by the device driver.";

static struct net_dev_metric {
    char name[MAX_NAME_LEN];

    int32_t virtual;
    int32_t configured;
    int32_t enabled;
    int32_t updated;

    // The total number of bytes of data transmitted or received by the
    // interface.（接口发送或接收的数据的总字节数）
    uint64_t      rx_bytes;
    prom_gauge_t *metric_rbytes;
    char          metric_rbytes_name[PROM_METRIC_NAME_LEN];

    // The total number of packets of data transmitted or received by the
    // interface.（接口发送或接收的数据包总数）
    uint64_t      rx_packets;
    prom_gauge_t *metric_rpackets;
    char          metric_rpackets_name[PROM_METRIC_NAME_LEN];

    // The total number of transmit or receive errors detected by the device
    // driver.（由设备驱动程序检测到的发送或接收错误的总数）
    uint64_t      rx_errs;
    prom_gauge_t *metric_rerrs;
    char          metric_rerrs_name[PROM_METRIC_NAME_LEN];

    // The total number of packets dropped by the device driver.（设备驱动程序丢弃的数据包总数）
    uint64_t      rx_drop;
    prom_gauge_t *metric_rdrop;
    char          metric_rdrop_name[PROM_METRIC_NAME_LEN];

    // The number of FIFO buffer errors.（FIFO缓冲区错误的数量）
    uint64_t      rx_fifo;
    prom_gauge_t *metric_rfifo;
    char          metric_rfifo_name[PROM_METRIC_NAME_LEN];

    // The number of packet framing errors.（分组帧错误的数量）
    uint64_t      rx_frame;
    prom_gauge_t *metric_rframe;
    char          metric_rframe_name[PROM_METRIC_NAME_LEN];

    // The number of compressed packets transmitted or received by the device driver. (This appears
    // to be unused in the 2.2.15 kernel.)（设备驱动程序发送或接收的压缩数据包数）
    uint64_t      rx_compressed;
    prom_gauge_t *metric_rcompressed;
    char          metric_rcompressed_name[PROM_METRIC_NAME_LEN];

    // The number of multicast frames transmitted or received by the device
    // driver.（设备驱动程序发送或接收的多播帧数）
    uint64_t      rx_multicast;
    prom_gauge_t *metric_rmulticast;
    char          metric_rmulticast_name[PROM_METRIC_NAME_LEN];

    uint64_t      tx_bytes;
    prom_gauge_t *metric_tbytes;
    char          metric_tbytes_name[PROM_METRIC_NAME_LEN];

    uint64_t      tx_packets;
    prom_gauge_t *metric_tpackets;
    char          metric_tpackets_name[PROM_METRIC_NAME_LEN];

    uint64_t      tx_errs;
    prom_gauge_t *metric_terrs;
    char          metric_terrs_name[PROM_METRIC_NAME_LEN];

    uint64_t      tx_drop;
    prom_gauge_t *metric_tdrop;
    char          metric_tdrop_name[PROM_METRIC_NAME_LEN];

    uint64_t      tx_fifo;
    prom_gauge_t *metric_tfifo;
    char          metric_tfifo_name[PROM_METRIC_NAME_LEN];

    // The number of collisions detected on the interface.（接口上检测到的冲突数）
    uint64_t      tx_colls;
    prom_gauge_t *metric_tcolls;
    char          metric_tcolls_name[PROM_METRIC_NAME_LEN];

    // The number of carrier losses detected by the device
    // driver.（由设备驱动程序检测到的载波损耗的数量）
    uint64_t      tx_carrier;
    prom_gauge_t *metric_tcarrier;
    char          metric_tcarrier_name[PROM_METRIC_NAME_LEN];

    uint64_t      tx_compressed;
    prom_gauge_t *metric_tcompressed;
    char          metric_tcompressed_name[PROM_METRIC_NAME_LEN];

    uint64_t      mtu;
    prom_gauge_t *metric_mtu;
    char          metric_mtu_name[PROM_METRIC_NAME_LEN];

    uint64_t      duplex;
    prom_gauge_t *metric_duplex;
    char          metric_duplex_name[PROM_METRIC_NAME_LEN];

    struct net_dev_metric *next;
} *__net_dev_metric_root = NULL, *__net_dev_metric_last_used = NULL;

static uint32_t __net_dev_added = 0, __net_dev_found = 0;

static struct net_dev_metric *__get_net_dev_metric(const char *name) {
    struct net_dev_metric *m = NULL;

    // search from last used
    for (m = __net_dev_metric_last_used; m; m = m->next) {
        if (strcmp(m->name, name) == 0) {
            __net_dev_metric_last_used = m->next;
            return m;
        }
    }

    // search from begin
    for (m = __net_dev_metric_root; m; m = m->next) {
        if (strcmp(m->name, name) == 0) {
            __net_dev_metric_last_used = m->next;
            return m;
        }
    }

    // not found, create new
    debug("[PLUGIN_PROC:proc_net_dev] Adding network device metric: '%s'", name);
    m = (struct net_dev_metric *)calloc(1, sizeof(struct net_dev_metric));
    strncpy(m->name, name, MAX_NAME_LEN - 1);

    snprintf(m->metric_rbytes_name, PROM_METRIC_NAME_LEN - 1, "net_dev_%s_rbytes", name);
    m->metric_rbytes = prom_collector_registry_must_register_metric(
        prom_gauge_new(m->metric_rbytes_name, __metric_help_net_dev_rx_bytes, 2,
                       (const char *[]){ "host", "netdev" }));
    snprintf(m->metric_rpackets_name, PROM_METRIC_NAME_LEN - 1, "net_dev_%s_rpackets", name);
    m->metric_rpackets = prom_collector_registry_must_register_metric(
        prom_gauge_new(m->metric_rpackets_name, __metric_help_net_dev_rx_packets, 2,
                       (const char *[]){ "host", "netdev" }));

    if (__net_dev_metric_root) {
        struct net_dev_metric *last = NULL;
        for (last = __net_dev_metric_root; last->next; last = last->next)
            ;
        last->next = m;
    } else {
        __net_dev_metric_root = m;
    }
    __net_dev_added++;

    return m;
}

static void __freeup_net_dev_metric(struct net_dev_metric *d) {
    if (d) {
        if (d->metric_rbytes) {
            prom_collector_registry_unregister_metric(d->metric_rbytes);
            d->metric_rbytes = NULL;
        }
        if (d->metric_rpackets) {
            prom_collector_registry_unregister_metric(d->metric_rpackets);
            d->metric_rpackets = NULL;
        }
        if (d->metric_rbytes_name) {
            memset(d->metric_rbytes_name, 0, PROM_METRIC_NAME_LEN);
        }
        if (d->metric_rpackets_name) {
            memset(d->metric_rpackets_name, 0, PROM_METRIC_NAME_LEN);
        }
        if (d->name) {
            memset(d->name, 0, MAX_NAME_LEN);
        }
        free(d);
    }
    free(d);
    d = NULL;
    __net_dev_added--;
}

static void __cleanup_net_dev_metric() {
    if (likeley(__net_dev_added == __net_dev_found)) {
        return;
    }

    __net_dev_added = 0;
    struct net_dev_metric *d = __net_dev_metric_root, *last = NULL;
    while (d) {
        if (unlikely(!d->updated)) {
            // 如果这个设备没有被检测到
            debug("[PLUGIN_PROC:proc_net_dev] Removing network device metric: '%s', linked after "
                  "'%s'",
                  m->name, last ? last->name : "ROOT");

            if (__net_dev_metric_last_used == m)
                __net_dev_metric_last_used = last;

            struct net_dev_metric *t = d;
            if (d == __net_dev_metric_root && !last) {
                __net_dev_metric_root = d;
            } else {
                last->next = d;
            }
            d = d->next;

            t->next = NULL;
            __freeup_net_dev_metric(t);
        } else {
            __net_dev_added++;
            last = d;
            d->updated = 0;
            d = d->next;
        }
    }
}

int32_t init_collector_proc_net_dev() {
    debug("[PLUGIN_PROC:proc_net_dev] init successed");
    return 0;
}

int32_t collector_proc_net_dev(int32_t update_every, usec_t dt, const char *config_path) {
    debug("[PLUGIN_PROC:proc_net_dev] config:%s running", config_path);

    int32_t ret = 0;
    return ret;
}

void fini_collector_proc_net_dev() {
    debug("[PLUGIN_PROC:proc_net_dev] fini successed");
}