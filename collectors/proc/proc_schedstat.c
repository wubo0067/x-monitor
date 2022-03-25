/*
 * @Author: CALM.WU
 * @Date: 2022-03-24 14:05:14
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-03-24 16:46:54
 */
#include "plugin_proc.h"

#include "prometheus-client-c/prom.h"

#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/os.h"
#include "utils/strings.h"

#include "appconfig/appconfig.h"

// https://www.kernel.org/doc/html/latest/scheduler/sched-stats.html

static const char       *__proc_schedstat_filename = "/proc/schedstat";
static struct proc_file *__pf_schedstat = NULL;

static prom_gauge_t *__metric_node_schedstat_running_seconds_total = NULL,
                    *__metric_node_schedstat_timeslices_total = NULL,
                    *__metric_node_schedstat_waiting_seconds_total = NULL;

static long __Hz = 100;

int32_t init_collector_proc_schedstat() {
    __metric_node_schedstat_running_seconds_total = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_schedstat_running_seconds_total",
                       "sum of all time spent running by tasks on this processor", 1,
                       (const char *[]){ "cpu" }));
    __metric_node_schedstat_waiting_seconds_total = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_schedstat_waiting_seconds_total",
                       "sum of all time spent waiting to run by tasks on this processor", 1,
                       (const char *[]){ "cpu" }));
    __metric_node_schedstat_timeslices_total =
        prom_collector_registry_must_register_metric(prom_gauge_new(
            "node_schedstat_timeslices_total", "sum of all timeslices run on this processor", 1,
            (const char *[]){ "cpu" }));

    __Hz = sysconf(_SC_CLK_TCK);

    debug("[PLUGIN_PROC:proc_schedstat] init successed");
    return 0;
}

int32_t collector_proc_schedstat(int32_t UNUSED(update_every), usec_t UNUSED(dt),
                                 const char *config_path) {
    debug("[PLUGIN_PROC:proc_schedstat] config:%s running", config_path);

    const char *f_schedstat =
        appconfig_get_member_str(config_path, "monitor_file", __proc_schedstat_filename);

    if (unlikely(!__pf_schedstat)) {
        __pf_schedstat = procfile_open(f_schedstat, " \t:", PROCFILE_FLAG_DEFAULT);
        if (unlikely(!__pf_schedstat)) {
            error("Cannot open %s", f_schedstat);
            return -1;
        }
    }

    __pf_schedstat = procfile_readall(__pf_schedstat);
    if (unlikely(!__pf_schedstat)) {
        error("Cannot read %s", f_schedstat);
        return -1;
    }

    size_t lines = procfile_lines(__pf_schedstat);
    size_t words = 0;

    double   schedstat_running_seconds_total = 0.0, schedstat_waiting_seconds_total = 0.0;
    uint64_t schedstat_timeslices_total = 0;

    for (size_t l = 0; l < lines - 1; l++) {
        const char *row_key = procfile_lineword(__pf_schedstat, l, 0);

        // 判断是否是 cpu 的数据
        if (row_key[0] == 'c' && row_key[1] == 'p' && row_key[2] == 'u') {
            words = procfile_linewords(__pf_schedstat, l);
            if (unlikely(words < 10)) {
                error("Cannot read /proc/schedstat cpu line. Expected 9 params, read %lu.", words);
                continue;
            }

            const char *core_id = &row_key[3];

            schedstat_running_seconds_total =
                (double)str2uint64_t(procfile_lineword(__pf_schedstat, l, 7)) / (double)__Hz
                / 10000000.0;
            schedstat_waiting_seconds_total =
                (double)str2uint64_t(procfile_lineword(__pf_schedstat, l, 8)) / (double)__Hz
                / 10000000.0;
            schedstat_timeslices_total = str2uint64_t(procfile_lineword(__pf_schedstat, l, 9));

            prom_gauge_set(__metric_node_schedstat_running_seconds_total,
                           schedstat_running_seconds_total, (const char *[]){ core_id });
            prom_gauge_set(__metric_node_schedstat_waiting_seconds_total,
                           schedstat_waiting_seconds_total, (const char *[]){ core_id });
            prom_gauge_set(__metric_node_schedstat_timeslices_total, schedstat_timeslices_total,
                           (const char *[]){ core_id });

            debug("[PLUGIN_PROC:proc_schedstat] cpu:%s running:%lf waiting:%lf timeslices:%lu",
                  core_id, schedstat_running_seconds_total, schedstat_waiting_seconds_total,
                  schedstat_timeslices_total);
        }
    }

    return 0;
}

void fini_collector_proc_schedstat() {
    if (likely(__pf_schedstat)) {
        procfile_close(__pf_schedstat);
        __pf_schedstat = NULL;
    }

    debug("[PLUGIN_PROC:proc_schedstat] stopped");
}