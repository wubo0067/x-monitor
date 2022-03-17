/*
 * @Author: CALM.WU
 * @Date: 2022-02-21 11:13:19
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-02-24 11:25:53
 */

#include "prometheus-client-c/prom.h"

#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/strings.h"
#include "utils/clocks.h"

#include "appconfig/appconfig.h"

static const char       *__proc_net_stat_nf_conntrack_filename = "/proc/net/stat/nf_conntrack";
static struct proc_file *__pf_net_stat_nf_conntrack = NULL;

int32_t init_collector_proc_net_stat_conntrack() {
    debug("[PLUGIN_PROC:proc_net_stat_conntrack] init successed");
    return 0;
}

int32_t collector_proc_net_stat_conntrack(int32_t UNUSED(update_every), usec_t UNUSED(dt),
                                          const char *config_path) {
    debug("[PLUGIN_PROC:proc_net_stat_conntrack] config:%s running", config_path);

    return 0;
}

void fini_collector_proc_net_stat_conntrack() {
    if (likely(__pf_net_stat_nf_conntrack)) {
        procfile_close(__pf_net_stat_nf_conntrack);
        __pf_net_stat_nf_conntrack = NULL;
    }

    debug("[PLUGIN_PROC:proc_net_stat_conntrack] stopped");
}