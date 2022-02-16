/*
 * @Author: CALM.WU
 * @Date: 2021-11-30 14:59:18
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-01-18 15:31:17
 */

#include "plugin_proc.h"

#include "routine.h"
#include "utils/clocks.h"
#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"

#include "appconfig/appconfig.h"

static const char *__name        = "PLUGIN_PROC";
static const char *__config_name = "collector_plugin_proc";

struct proc_metric_collector {
    const char *name;
    int32_t     enabled;
    int32_t (*init_func)();                             // 初始化函数
    int32_t (*do_func)(int32_t, usec_t, const char *);  // 执行函数
    void (*fini_func)();                                //
    usec_t duration;                                    // 执行的耗时
};

struct proc_metrics_module {
    int32_t                      exit_flag;
    pthread_t                    thread_id;  // routine执行的线程id
    struct proc_metric_collector collectors[];
};

static struct proc_metrics_module
    __proc_metrics_module = { .exit_flag  = 0,
                              .thread_id  = 0,
                              .collectors = {
                                  // disk metrics
                                  {
                                      .name      = "disk_stats",
                                      .enabled   = 1,
                                      .init_func = init_collector_proc_diskstats,
                                      .do_func   = collector_proc_diskstats,
                                      .fini_func = fini_collector_proc_diskstats,
                                  },
                                  {
                                      .name      = "load_avg",
                                      .enabled   = 1,
                                      .init_func = init_collector_proc_loadavg,
                                      .do_func   = collector_proc_loadavg,
                                      .fini_func = fini_collector_proc_loadavg,
                                  },
                                  {
                                      .name      = "proc_stat",
                                      .enabled   = 1,
                                      .init_func = init_collector_proc_cpustat,
                                      .do_func   = collector_proc_cpustat,
                                      .fini_func = fini_collector_proc_cpustat,
                                  },
                                  {
                                      .name      = "proc_pressure",
                                      .enabled   = 1,
                                      .init_func = init_collector_proc_pressure,
                                      .do_func   = collector_proc_pressure,
                                      .fini_func = fini_collector_proc_pressure,
                                  },
                                  {
                                      .name      = "proc_meminfo",
                                      .enabled   = 1,
                                      .init_func = init_collector_proc_meminfo,
                                      .do_func   = collector_proc_meminfo,
                                      .fini_func = fini_collector_proc_meminfo,
                                  },
                                  {
                                      .name      = "proc_vmstat",
                                      .enabled   = 1,
                                      .init_func = init_collector_proc_vmstat,
                                      .do_func   = collector_proc_vmstat,
                                      .fini_func = fini_collector_proc_vmstat,
                                  },
                                  // the terminator of this array
                                  { .name = NULL, .do_func = NULL, .fini_func = NULL },
                              } };

__attribute__((constructor)) static void collector_proc_register_routine() {
    fprintf(stderr, "---register_collector_proc_routine---\n");
    struct xmonitor_static_routine *xsr =
        (struct xmonitor_static_routine *)calloc(1, sizeof(struct xmonitor_static_routine));
    xsr->name          = __name;
    xsr->config_name   = __config_name;  //配置文件中节点名
    xsr->enabled       = 0;
    xsr->thread_id     = &__proc_metrics_module.thread_id;
    xsr->init_routine  = proc_routine_init;
    xsr->start_routine = proc_routine_start;
    xsr->stop_routine  = proc_routine_stop;
    register_xmonitor_static_routine(xsr);
}

int32_t proc_routine_init() {
    debug("[%s] routine init successed", __name);
    char proc_module_cfgname[CONFIG_NAME_MAX + 1];

    // check the enabled status for each module
    for (int32_t i = 0; __proc_metrics_module.collectors[i].name; i++) {
        //
        struct proc_metric_collector *pmc = &__proc_metrics_module.collectors[i];

        snprintf(proc_module_cfgname, CONFIG_NAME_MAX, "collector_plugin_proc.%s", pmc->name);

        pmc->enabled = appconfig_get_member_bool(proc_module_cfgname, "enable", 1);

        debug("[%s] module %s is %s", __name, pmc->name, pmc->enabled ? "enabled" : "disabled");

        if (likely(pmc->enabled && pmc->init_func)) {
            if (pmc->init_func() != 0) {
                error("[%s] module %s init failed", __name, pmc->name);
                return -1;
            }
        }
    }
    return 0;
}

void *proc_routine_start(void *arg) {
    debug("[%s] routine start", __name);

    int32_t index        = 0;
    int32_t update_every = appconfig_get_int("collector_plugin_proc.update_every", 1);

    static char module_config_path[CONFIG_NAME_MAX + 1] = { 0 };

    // 每次更新的时间间隔，单位微秒
    // rrd_update_every单位秒
    usec_t step_microseconds = update_every * USEC_PER_SEC;

    struct heartbeat hb;
    heartbeat_init(&hb);

    while (!__proc_metrics_module.exit_flag) {
        usec_t dt = heartbeat_next(&hb, step_microseconds);

        if (unlikely(__proc_metrics_module.exit_flag)) {
            break;
        }

        for (index = 0; __proc_metrics_module.collectors[index].name; index++) {
            struct proc_metric_collector *pmc = &__proc_metrics_module.collectors[index];
            if (unlikely(!pmc->enabled)) {
                continue;
            }

            snprintf(module_config_path, CONFIG_NAME_MAX, "collector_plugin_proc.%s", pmc->name);

            pmc->do_func(update_every, dt, module_config_path);

            if (unlikely(__proc_metrics_module.exit_flag)) {
                break;
            }
        }
    }

    debug("[%s] routine exit", __name);
    return NULL;
}

void proc_routine_stop() {
    __proc_metrics_module.exit_flag = 1;
    pthread_join(__proc_metrics_module.thread_id, NULL);

    for (int32_t index = 0; __proc_metrics_module.collectors[index].name; index++) {

        struct proc_metric_collector *pmc = &__proc_metrics_module.collectors[index];

        if (likely(pmc->enabled && pmc->fini_func)) {
            pmc->fini_func();
        }
    }

    debug("[%s] has completely stopped", __name);
    return;
}