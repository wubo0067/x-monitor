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

static const char *__name = "PLUGIN_APPSTAT";
static const char *__config_name = "collector_plugin_appstat";

struct collector_appstat {
    int32_t   exit_flag;
    pthread_t thread_id;                       // routine执行的线程ids
    int32_t   update_every;                    // 指标采集时间间隔
    int32_t   update_every_for_app;            // 应用更新时间间隔
    int32_t   update_every_for_filter_rules;   //
};

static struct collector_appstat __collector_appstat = {
    .exit_flag = 0,
    .thread_id = 0,
    .update_every = 1,
    .update_every_for_app = 10,
    .update_every_for_filter_rules = 60,
};

__attribute__((constructor)) static void collector_diskspace_register_routine() {
    fprintf(stderr, "---register_collector_appstat_routine---\n");
    struct xmonitor_static_routine *xsr =
        (struct xmonitor_static_routine *)calloc(1, sizeof(struct xmonitor_static_routine));
    xsr->name = __name;
    xsr->config_name = __config_name;   //配置文件中节点名
    xsr->enabled = 0;
    xsr->thread_id = &__collector_appstat.thread_id;
    xsr->init_routine = appstat_collector_routine_init;
    xsr->start_routine = appstat_collector_routine_start;
    xsr->stop_routine = appstat_collector_routine_stop;
    register_xmonitor_static_routine(xsr);
}

int32_t appstat_collector_routine_init() {
    // 读取配置文件
    debug("routine '%s' init successed", __name);
    return 0;
}

void *appstat_collector_routine_start(void *arg) {
    debug("routine '%s' start", __name);

    usec_t step_microseconds = __collector_appstat.update_every * USEC_PER_SEC;

    struct heartbeat hb;
    heartbeat_init(&hb);

    while (!__collector_appstat.exit_flag) {
        //等到下一个update周期
        heartbeat_next(&hb, step_microseconds);

        if (__collector_appstat.exit_flag) {
            break;
        }
    }

    debug("routine '%s' exit", __name);
    return NULL;
}

void appstat_collector_routine_stop() {
    __collector_appstat.exit_flag = 1;
    pthread_join(__collector_appstat.thread_id, NULL);

    debug("routine '%s' has completely stopped", __name);
    return;
}