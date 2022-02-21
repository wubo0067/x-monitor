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