/*
 * @Author: CALM.WU
 * @Date: 2022-02-21 11:10:12
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-03-24 15:55:01
 */

// https://stackoverflow.com/questions/3521678/what-are-meanings-of-fields-in-proc-net-dev
// https://www.kernel.org/doc/html/latest/networking/statistics.html
// struct rtnl_link_stats64
// https://www.kernel.org/doc/Documentation/ABI/testing/sysfs-class-net
//
/*
/sys/class/net/<iface>/operstate
Indicates the interface RFC2863 operational state as a string.
        Possible values are:
        "unknown", "notpresent", "down", "lowerlayerdown", "testing",
        "dormant", "up".

/sys/class/net/<iface>/carrier
Indicates the current physical link state of the interface.
        Posssible values are:

        == =====================
        0  physical link is down
        1  physical link is up
        == =====================
*/

#include "prometheus-client-c/prom.h"
#include "prometheus-client-c/prom_map_i.h"
#include "prometheus-client-c/prom_metric_t.h"
#include "prometheus-client-c/prom_metric_i.h"
#include "prometheus-client-c/prom_collector_t.h"
#include "prometheus-client-c/prom_collector_registry_t.h"

#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/strings.h"
#include "utils/clocks.h"
#include "utils/files.h"
#include "utils/strings.h"

#include "appconfig/appconfig.h"

static const char       *__proc_net_dev_filename = "/proc/net/dev";
static struct proc_file *__pf_net_dev = NULL;

static const char *__metric_help_net_dev_rx_bytes =
    "Number of good received bytes, corresponding to rx_packets.";
static const char *__metric_help_net_dev_rx_packets =
    "Number of good packets received by the interface. For hardware interfaces counts all good "
    "packets received from the device by the host, including packets which host had to drop at "
    "various stages of processing (even in the driver).";
static const char *__metric_help_net_dev_rx_errors =
    "Total number of bad packets received on this network device. This counter must include events "
    "counted by rx_length_errors, rx_crc_errors, rx_frame_errors and other errors not otherwise "
    "counted.";
static const char *__metric_help_net_dev_rx_dropped =
    "Number of packets received but not processed, e.g. due to lack of resources or unsupported "
    "protocol. For hardware interfaces this counter may include packets discarded due to L2 "
    "address filtering but should not include packets dropped by the device due to buffer "
    "exhaustion which are counted separately in rx_missed_errors (since procfs folds those two "
    "counters together).";

// FIFO缓冲区错误的数量
static const char *__metric_help_net_dev_rx_fifo = "The number of receive FIFO buffer errors.";
static const char *__metric_help_net_dev_rx_frame = "The number of receive framing errors.";
static const char *__metric_help_net_dev_rx_compressed =
    "Number of correctly received compressed packets. This counters is only meaningful for "
    "interfaces which support packet compression (e.g. CSLIP, PPP).";
static const char *__metric_help_net_dev_rx_multicast =
    "Multicast packets received. For hardware interfaces this statistic is commonly calculated at "
    "the device level (unlike rx_packets) and therefore may include packets which did not reach "
    "the host.";
static const char *__metric_help_net_dev_tx_bytes =
    "Number of good transmitted bytes, corresponding to tx_packets.";
static const char *__metric_help_net_dev_tx_packets =
    "Number of packets successfully transmitted. For hardware interfaces counts packets which host "
    "was able to successfully hand over to the device, which does not necessarily mean that "
    "packets had been successfully transmitted out of the device, only that device acknowledged it "
    "copied them out of host memory.";
static const char *__metric_help_net_dev_tx_errors =
    "Total number of transmit problems. This counter must include events counter by "
    "tx_aborted_errors, tx_carrier_errors, tx_fifo_errors, tx_heartbeat_errors, tx_window_errors "
    "and other errors not otherwise counted.";
static const char *__metric_help_net_dev_tx_dropped =
    "Number of packets dropped on their way to transmission, e.g. due to lack of resources.";
static const char *__metric_help_net_dev_tx_fifo = "The number of transmit FIFO buffer errors.";
static const char *__metric_help_net_dev_tx_collisions =
    "Number of collisions during packet transmissions.";
static const char *__metric_help_net_dev_tx_carrier =
    "Number of frame transmission errors due to loss of carrier during transmission.";
static const char *__metric_help_net_dev_tx_compressed =
    "Number of transmitted compressed packets. This counters is only meaningful for interfaces "
    "which support packet compression (e.g. CSLIP, PPP).";

static prom_gauge_t *__metric_node_network_transmit_bytes_total = NULL,
                    *__metric_node_network_transmit_packets_total = NULL,
                    *__metric_node_network_transmit_errs_total = NULL,
                    *__metric_node_network_transmit_drop_total = NULL,
                    *__metric_node_network_transmit_fifo_total = NULL,
                    *__metric_node_network_transmit_colls_total = NULL,
                    *__metric_node_network_transmit_carrier_total = NULL,
                    *__metric_node_network_transmit_compressed_total = NULL,
                    *__metric_node_network_receive_bytes_total = NULL,
                    *__metric_node_network_receive_packets_total = NULL,
                    *__metric_node_network_receive_errs_total = NULL,
                    *__metric_node_network_receive_drop_total = NULL,
                    *__metric_node_network_receive_fifo_total = NULL,
                    *__metric_node_network_receive_frame_total = NULL,
                    *__metric_node_network_receive_compressed_total = NULL,
                    *__metric_node_network_receive_multicast_total = NULL,
                    *__metric_node_network_mtu_bytes = NULL,
                    *__metric_node_network_virtual_device = NULL;

static struct net_dev_metric {
    char     name[MAX_NAME_LEN];
    uint32_t hash;

    int32_t virtual;   // 是不是虚拟设备
    // int32_t enabled;
    int32_t updated;

    // The total number of bytes of data transmitted or received by the
    // interface.（接口发送或接收的数据的总字节数）
    uint64_t rx_bytes;

    // The total number of packets of data transmitted or received by the
    // interface.（接口发送或接收的数据包总数）
    uint64_t rx_packets;

    // The total number of transmit or receive errors detected by the device
    // driver.（由设备驱动程序检测到的发送或接收错误的总数）
    uint64_t rx_errs;

    // The total number of packets dropped by the device driver.（设备驱动程序丢弃的数据包总数）
    uint64_t rx_drop;

    // The number of FIFO buffer errors.（FIFO缓冲区错误的数量）
    uint64_t rx_fifo;

    // The number of packet framing errors.（分组帧错误的数量）
    uint64_t rx_frame;

    // The number of compressed packets transmitted or received by the device driver. (This appears
    // to be unused in the 2.2.15 kernel.)（设备驱动程序发送或接收的压缩数据包数）
    uint64_t rx_compressed;

    // The number of multicast frames transmitted or received by the device
    // driver.（设备驱动程序发送或接收的多播帧数）
    uint64_t rx_multicast;

    uint64_t tx_bytes;

    uint64_t tx_packets;

    uint64_t tx_errs;

    uint64_t tx_drop;

    uint64_t tx_fifo;

    // The number of collisions detected on the interface.（接口上检测到的冲突数）
    uint64_t tx_colls;

    // The number of carrier losses detected by the device
    // driver.（由设备驱动程序检测到的载波损耗的数量）
    uint64_t tx_carrier;

    uint64_t tx_compressed;

    // Indicates the interface currently configured MTU value, in bytes, and in decimal format.
    // Specific values depends on the lower-level interface protocol used. Ethernet devices will
    // show a 'mtu' attribute value of 1500 unless changed.
    int64_t mtu;

    // // Indicates the interface latest or current duplex value
    // int64_t       duplex;
    // // Indicates the interface latest or current speed value. Value is an integer representing
    // the
    // // link speed in Mbits/sec
    // int64_t       speed;

    struct net_dev_metric *next;
} *__net_dev_metric_root = NULL, *__net_dev_metric_last_used = NULL;

static uint32_t __net_dev_added = 0, __net_dev_found = 0;

static struct net_dev_metric *__get_net_dev_metric(const char *name, const char *config_path) {
    struct net_dev_metric *m = NULL;

    uint32_t hash = simple_hash(name);

    // search from last used
    for (m = __net_dev_metric_last_used; m; m = m->next) {
        if (hash == m->hash && strcmp(m->name, name) == 0) {
            __net_dev_metric_last_used = m->next;
            return m;
        }
    }

    // search from begin
    for (m = __net_dev_metric_root; m != __net_dev_metric_last_used; m = m->next) {
        if (hash == m->hash && strcmp(m->name, name) == 0) {
            __net_dev_metric_last_used = m->next;
            return m;
        }
    }

    // not found, create new
    m = (struct net_dev_metric *)calloc(1, sizeof(struct net_dev_metric));
    strncpy(m->name, name, MAX_NAME_LEN - 1);
    m->hash = simple_hash(m->name);
    debug("[PLUGIN_PROC:proc_net_dev] Adding network device metric: '%s' hash: %u", name, m->hash);

    // 获取mtu
    char filename[FILENAME_MAX] = { 0 };

    const char *cfg_net_dev_mtu =
        appconfig_get_member_str(config_path, "path_get_net_dev_mtu", "/sys/class/net/%s/mtu");
    snprintf(filename, FILENAME_MAX - 1, cfg_net_dev_mtu, name);
    read_file_to_int64(filename, &m->mtu);
    debug("[PLUGIN_PROC:proc_net_dev] network interface '%s' mtu file: '%s' mtu: %ld", name,
          filename, m->mtu);

    prom_gauge_set(__metric_node_network_mtu_bytes, m->mtu, (const char *[]){ name });

    // 判断是否是虚拟网卡
    const char *cfg_net_dev_is_virtual = appconfig_get_member_str(
        config_path, "path_get_net_dev_is_virtual", "/sys/devices/virtual/net/%s");
    snprintf(filename, FILENAME_MAX - 1, cfg_net_dev_is_virtual, name);
    // 判断目录是否存在
    if (likely(access(filename, F_OK) == 0)) {
        m->virtual = 1;
    } else {
        m->virtual = 0;
    }
    prom_counter_add(__metric_node_network_virtual_device, m->virtual, (const char *[]){ name });
    debug("[PLUGIN_PROC:proc_net_dev] network interface '%s' is_virtual file: '%s' is_virtual: %d",
          name, filename, m->virtual);

    // 插入链表
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

static void __free_net_dev_metric(struct net_dev_metric *d) {
    if (likely(d)) {

        debug("[PLUGIN_PROC:proc_net_dev] free network interface metric '%s'", d->name);

        // prom_collector_t *default_collector = (prom_collector_t *)prom_map_get(
        //     PROM_COLLECTOR_REGISTRY_DEFAULT->collectors, "default");
        // prom_map_delete(default_collector->metrics, ((prom_metric_t *)d->metric_duplex)->name);
        // prom_map_delete(default_collector->metrics, ((prom_metric_t *)d->metric_speed)->name);

        free(d);
        d = NULL;
    }

    __net_dev_added--;
}

static void __cleanup_net_dev_metric() {
    if (likely(__net_dev_added == __net_dev_found)) {
        return;
    }

    __net_dev_added = 0;
    struct net_dev_metric *d = __net_dev_metric_root, *last = NULL;
    while (d) {
        if (unlikely(!d->updated)) {
            // 如果这个设备没有被检测到
            debug("[PLUGIN_PROC:proc_net_dev] Removing network device metric: '%s', linked after "
                  "'%s'",
                  d->name, last ? last->name : "ROOT");

            if (__net_dev_metric_last_used == d)
                __net_dev_metric_last_used = last;

            struct net_dev_metric *t = d;
            if (d == __net_dev_metric_root && !last) {
                __net_dev_metric_root = d;
            } else {
                last->next = d;
            }
            d = d->next;

            t->next = NULL;
            __free_net_dev_metric(t);
        } else {
            __net_dev_added++;
            last = d;
            d->updated = 0;
            d = d->next;
        }
    }
}

int32_t init_collector_proc_net_dev() {

    // 构造指标并注册
    __metric_node_network_receive_bytes_total = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_network_receive_bytes_total", __metric_help_net_dev_rx_bytes, 1,
                       (const char *[]){ "device" }));
    __metric_node_network_receive_packets_total = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_network_receive_packets_total", __metric_help_net_dev_rx_packets, 1,
                       (const char *[]){ "device" }));
    __metric_node_network_receive_errs_total = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_network_receive_errs_total", __metric_help_net_dev_rx_errors, 1,
                       (const char *[]){ "device" }));
    __metric_node_network_receive_drop_total = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_network_receive_drop_total", __metric_help_net_dev_rx_dropped, 1,
                       (const char *[]){ "device" }));
    __metric_node_network_receive_fifo_total = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_network_receive_fifo_total", __metric_help_net_dev_rx_fifo, 1,
                       (const char *[]){ "device" }));
    __metric_node_network_receive_frame_total = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_network_receive_frame_total", __metric_help_net_dev_rx_frame, 1,
                       (const char *[]){ "device" }));
    __metric_node_network_receive_compressed_total = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_network_receive_compressed_total", __metric_help_net_dev_rx_compressed,
                       1, (const char *[]){ "device" }));
    __metric_node_network_receive_multicast_total = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_network_receive_multicast_total", __metric_help_net_dev_rx_multicast,
                       1, (const char *[]){ "device" }));
    __metric_node_network_transmit_bytes_total = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_network_transmit_bytes_total", __metric_help_net_dev_tx_bytes, 1,
                       (const char *[]){ "device" }));
    __metric_node_network_transmit_packets_total = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_network_transmit_packets_total", __metric_help_net_dev_tx_packets, 1,
                       (const char *[]){ "device" }));
    __metric_node_network_transmit_errs_total = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_network_transmit_errs_total", __metric_help_net_dev_tx_errors, 1,
                       (const char *[]){ "device" }));
    __metric_node_network_transmit_drop_total = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_network_transmit_drop_total", __metric_help_net_dev_tx_dropped, 1,
                       (const char *[]){ "device" }));
    __metric_node_network_transmit_fifo_total = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_network_transmit_fifo_total", __metric_help_net_dev_tx_fifo, 1,
                       (const char *[]){ "device" }));
    __metric_node_network_transmit_colls_total = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_network_transmit_colls_total", __metric_help_net_dev_tx_collisions, 1,
                       (const char *[]){ "device" }));
    __metric_node_network_transmit_carrier_total = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_network_transmit_carrier_total", __metric_help_net_dev_tx_carrier, 1,
                       (const char *[]){ "device" }));
    __metric_node_network_transmit_compressed_total = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_network_transmit_compressed_total",
                       __metric_help_net_dev_tx_compressed, 1, (const char *[]){ "device" }));

    __metric_node_network_mtu_bytes = prom_collector_registry_must_register_metric(prom_gauge_new(
        "node_network_mtu_bytes",
        "Indicates the interface currently configured MTU value, in bytes, and in decimal format.",
        1, (const char *[]){ "device" }));
    __metric_node_network_virtual_device =
        prom_collector_registry_must_register_metric(prom_counter_new(
            "node_network_virtual_device", "Indicates whether the interface is a virtual device.",
            1, (const char *[]){ "device" }));

    debug("[PLUGIN_PROC:proc_net_dev] init successed");
    return 0;
}

int32_t collector_proc_net_dev(int32_t UNUSED(update_every), usec_t UNUSED(dt),
                               const char *config_path) {
    debug("[PLUGIN_PROC:proc_net_dev] config:%s running", config_path);

    const char *f_netdev =
        appconfig_get_member_str(config_path, "monitor_file", __proc_net_dev_filename);

    if (unlikely(!__pf_net_dev)) {
        __pf_net_dev = procfile_open(f_netdev, " \t,|", PROCFILE_FLAG_DEFAULT);
        if (unlikely(!__pf_net_dev)) {
            error("Cannot open %s", f_netdev);
            return -1;
        }
    }

    __pf_net_dev = procfile_readall(__pf_net_dev);
    if (unlikely(!__pf_net_dev)) {
        error("Cannot read %s", f_netdev);
        return -1;
    }

    size_t lines = procfile_lines(__pf_net_dev);
    for (size_t l = 2; l < lines; l++) {
        // 从第二行开始
        if (unlikely(procfile_linewords(__pf_net_dev, l) < 17)) {
            warn("[PLUGIN_PROC:proc_net_dev] require 17 words on line: %lu", l);
            continue;
        }

        char  *dev_name = procfile_lineword(__pf_net_dev, l, 0);
        size_t dev_name_len = strlen(dev_name);
        if (dev_name[dev_name_len - 1] == ':') {
            dev_name[dev_name_len - 1] = 0;
        }
        // prometheus的指标名不支持中杠字符，在此进行替换
        strreplace(dev_name, '-', '_');

        struct net_dev_metric *d = __get_net_dev_metric(dev_name, config_path);

        d->updated = 1;

        d->rx_bytes = str2uint64_t(procfile_lineword(__pf_net_dev, l, 1));
        d->rx_packets = str2uint64_t(procfile_lineword(__pf_net_dev, l, 2));
        d->rx_errs = str2uint64_t(procfile_lineword(__pf_net_dev, l, 3));
        d->rx_drop = str2uint64_t(procfile_lineword(__pf_net_dev, l, 4));
        d->rx_fifo = str2uint64_t(procfile_lineword(__pf_net_dev, l, 5));
        d->rx_frame = str2uint64_t(procfile_lineword(__pf_net_dev, l, 6));
        d->rx_compressed = str2uint64_t(procfile_lineword(__pf_net_dev, l, 7));
        d->rx_multicast = str2uint64_t(procfile_lineword(__pf_net_dev, l, 8));
        d->tx_bytes = str2uint64_t(procfile_lineword(__pf_net_dev, l, 9));
        d->tx_packets = str2uint64_t(procfile_lineword(__pf_net_dev, l, 10));
        d->tx_errs = str2uint64_t(procfile_lineword(__pf_net_dev, l, 11));
        d->tx_drop = str2uint64_t(procfile_lineword(__pf_net_dev, l, 12));
        d->tx_fifo = str2uint64_t(procfile_lineword(__pf_net_dev, l, 13));
        d->tx_colls = str2uint64_t(procfile_lineword(__pf_net_dev, l, 14));
        d->tx_carrier = str2uint64_t(procfile_lineword(__pf_net_dev, l, 15));
        d->tx_compressed = str2uint64_t(procfile_lineword(__pf_net_dev, l, 16));

        // 设置指标值
        prom_gauge_set(__metric_node_network_receive_bytes_total, d->rx_bytes,
                       (const char *[]){ dev_name });
        prom_gauge_set(__metric_node_network_receive_packets_total, d->rx_packets,
                       (const char *[]){ dev_name });
        prom_gauge_set(__metric_node_network_receive_errs_total, d->rx_errs,
                       (const char *[]){ dev_name });
        prom_gauge_set(__metric_node_network_receive_drop_total, d->rx_drop,
                       (const char *[]){ dev_name });
        prom_gauge_set(__metric_node_network_receive_fifo_total, d->rx_fifo,
                       (const char *[]){ dev_name });
        prom_gauge_set(__metric_node_network_receive_frame_total, d->rx_frame,
                       (const char *[]){ dev_name });
        prom_gauge_set(__metric_node_network_receive_compressed_total, d->rx_compressed,
                       (const char *[]){ dev_name });
        prom_gauge_set(__metric_node_network_receive_multicast_total, d->rx_multicast,
                       (const char *[]){ dev_name });
        prom_gauge_set(__metric_node_network_transmit_bytes_total, d->tx_bytes,
                       (const char *[]){ dev_name });
        prom_gauge_set(__metric_node_network_transmit_packets_total, d->tx_packets,
                       (const char *[]){ dev_name });
        prom_gauge_set(__metric_node_network_transmit_errs_total, d->tx_errs,
                       (const char *[]){ dev_name });
        prom_gauge_set(__metric_node_network_transmit_drop_total, d->tx_drop,
                       (const char *[]){ dev_name });
        prom_gauge_set(__metric_node_network_transmit_fifo_total, d->tx_fifo,
                       (const char *[]){ dev_name });
        prom_gauge_set(__metric_node_network_transmit_colls_total, d->tx_colls,
                       (const char *[]){ dev_name });
        prom_gauge_set(__metric_node_network_transmit_carrier_total, d->tx_carrier,
                       (const char *[]){ dev_name });
        prom_gauge_set(__metric_node_network_transmit_compressed_total, d->tx_compressed,
                       (const char *[]){ dev_name });

        debug("[PLUGIN_PROC:proc_net_dev] '%s' rx_bytes: %lu, rx_packets: %lu, rx_errs: %lu, "
              "rx_drop: %lu, rx_fifo: %lu, rx_frame: %lu, rx_compressed: %lu, rx_multicast: %lu, "
              "tx_bytes: %lu, tx_packets: %lu, tx_errs: %lu, tx_drop: %lu, tx_fifo: %lu, tx_colls: "
              "%lu, tx_carrier: %lu, tx_compressed: %lu",
              dev_name, d->rx_bytes, d->rx_packets, d->rx_errs, d->rx_drop, d->rx_fifo, d->rx_frame,
              d->rx_compressed, d->rx_multicast, d->tx_bytes, d->tx_packets, d->tx_errs, d->tx_drop,
              d->tx_fifo, d->tx_colls, d->tx_carrier, d->tx_compressed);
    }

    __cleanup_net_dev_metric();

    return 0;
}

void fini_collector_proc_net_dev() {
    // 清理所有dev_metric
    struct net_dev_metric *free_d = __net_dev_metric_root;
    while (free_d) {
        struct net_dev_metric *next = free_d->next;
        free_d->next = NULL;
        __free_net_dev_metric(free_d);
        free_d = next;
    }

    if (likely(__pf_net_dev)) {
        procfile_close(__pf_net_dev);
        __pf_net_dev = NULL;
    }

    debug("[PLUGIN_PROC:proc_net_dev] fini successed");
}