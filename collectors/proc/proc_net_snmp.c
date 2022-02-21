/*
 * @Author: CALM.WU
 * @Date: 2022-02-21 11:10:03
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-02-21 17:15:43
 */

// https://www.codeleading.com/article/87784845826/

#include "prometheus-client-c/prom.h"

#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/strings.h"
#include "utils/clocks.h"

#include "appconfig/appconfig.h"

static const char       *__proc_net_snmp_filename = "/proc/net/snmp";
static struct proc_file *__pf_net_snmp = NULL;

int32_t init_collector_proc_net_snmp() {
    int32_t ret = 0;
    return ret;
}

int32_t collector_proc_net_snmp(int32_t update_every, usec_t dt, const char *config_path) {
    int32_t ret = 0;
    return ret;
}

void fini_collector_proc_net_snmp() {
}