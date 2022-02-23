/*
 * @Author: CALM.WU
 * @Date: 2022-02-21 11:10:12
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-02-21 17:15:48
 */

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

struct net_dev_metric {
    // The total number of bytes of data transmitted or received by the
    // interface.（接口发送或接收的数据的总字节数）
    prom_gauge_t *metric_rbytes;
    char          metric_rbytes_name[PROM_METRIC_NAME_LEN];

    // The total number of packets of data transmitted or received by the
    // interface.（接口发送或接收的数据包总数）
    prom_gauge_t *metric_rpackets;
    char          metric_rpackets_name[PROM_METRIC_NAME_LEN];

    // The total number of transmit or receive errors detected by the device
    // driver.（由设备驱动程序检测到的发送或接收错误的总数）
    prom_gauge_t *metric_rerrs;
    char          metric_rerrs_name[PROM_METRIC_NAME_LEN];

    // The total number of packets dropped by the device driver.（设备驱动程序丢弃的数据包总数）
    prom_gauge_t *metric_rdrop;
    char          metric_rdrop_name[PROM_METRIC_NAME_LEN];

    // The number of FIFO buffer errors.（FIFO缓冲区错误的数量）
    prom_gauge_t *metric_rfifo;
    char          metric_rfifo_name[PROM_METRIC_NAME_LEN];

    // The number of packet framing errors.（分组帧错误的数量）
    prom_gauge_t *metric_rframe;
    char          metric_rframe_name[PROM_METRIC_NAME_LEN];

    // The number of compressed packets transmitted or received by the device driver. (This appears
    // to be unused in the 2.2.15 kernel.)（设备驱动程序发送或接收的压缩数据包数）
    prom_gauge_t *metric_rcompressed;
    char          metric_rcompressed_name[PROM_METRIC_NAME_LEN];

    // The number of multicast frames transmitted or received by the device
    // driver.（设备驱动程序发送或接收的多播帧数）
    prom_gauge_t *metric_rmulticast;
    char          metric_rmulticast_name[PROM_METRIC_NAME_LEN];

    prom_gauge_t *metric_tbytes;
    char          metric_tbytes_name[PROM_METRIC_NAME_LEN];

    prom_gauge_t *metric_tpackets;
    char          metric_tpackets_name[PROM_METRIC_NAME_LEN];

    prom_gauge_t *metric_terrs;
    char          metric_terrs_name[PROM_METRIC_NAME_LEN];

    prom_gauge_t *metric_tdrop;
    char          metric_tdrop_name[PROM_METRIC_NAME_LEN];

    prom_gauge_t *metric_tfifo;
    char          metric_tfifo_name[PROM_METRIC_NAME_LEN];

    // The number of collisions detected on the interface.（接口上检测到的冲突数）
    prom_gauge_t *metric_tcolls;
    char          metric_tcolls_name[PROM_METRIC_NAME_LEN];

    // The number of carrier losses detected by the device
    // driver.（由设备驱动程序检测到的载波损耗的数量）
    prom_gauge_t *metric_tcarrier;
    char          metric_tcarrier_name[PROM_METRIC_NAME_LEN];

    prom_gauge_t *metric_tcompressed;
    char          metric_tcompressed_name[PROM_METRIC_NAME_LEN];

    prom_gauge_t *metric_mtu;
    char          metric_mtu_name[PROM_METRIC_NAME_LEN];
};

int32_t init_collector_proc_net_dev() {
    int32_t ret = 0;
    return ret;
}

int32_t collector_proc_net_dev(int32_t update_every, usec_t dt, const char *config_path) {
    int32_t ret = 0;
    return ret;
}

void fini_collector_proc_net_dev() {
}