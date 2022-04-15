/*
 * @Author: CALM.WU
 * @Date: 2022-04-13 15:18:43
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-04-13 16:39:37
 */

#include "plugin_app.h"

#include "routine.h"
#include "utils/clocks.h"
#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"

#include "appconfig/appconfig.h"

static const char *__name = "PLUGIN_APP";
static const char *__config_name = "collector_plugin_appstat";

struct collector_appstat {
    int32_t   exit_flag;
    pthread_t thread_id;          // routine执行的线程ids
    int32_t   update_every;       // 指标采集时间间隔
    int32_t   update_app_every;   // 应用更新时间间隔
};

static struct collector_appstat __collector_appstat = {
    .exit_flag = 0,
    .thread_id = 0,
    .update_every = 1,
    .update_app_every = 10,
};

int32_t appstat_collector_routine_init() {
    return 0;
}

void *appstat_collector_routine_start(void *arg) {
    return NULL;
}

void appstat_collector_routine_stop() {
}