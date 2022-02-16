/*
 * @Author: CALM.WU
 * @Date: 2022-01-26 17:00:34
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-01-26 17:02:23
 */

// 取得虚拟内存统计信息（相关文件/proc/vmstat）

#include "plugin_proc.h"

#include "prometheus-client-c/prom.h"

#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/strings.h"

#include "appconfig/appconfig.h"

int32_t init_collector_proc_vmstat() {
    debug("[PLUGIN_PROC:proc_vmstat] init successed");
    return 0;
}

int32_t collector_proc_vmstat(int32_t update_every, usec_t dt, const char *config_path) {
    debug("[PLUGIN_PROC:proc_vmstat] config:%s running", config_path);
    return 0;
}

void fini_collector_proc_vmstat() {
    debug("[PLUGIN_PROC:proc_vmstat] stopped");
}