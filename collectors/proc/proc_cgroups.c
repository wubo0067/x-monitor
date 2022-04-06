/*
 * @Author: calmwu
 * @Date: 2022-03-10 16:23:49
 * @Last Modified by: calmwu
 * @Last Modified time: 2022-03-10 16:24:21
 */

#include "plugin_proc.h"

#include "prometheus-client-c/prom.h"
#include "collectc/cc_array.h"

#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/strings.h"

#include "appconfig/appconfig.h"

static const char       *__proc_cgroups_filename = "/proc/cgroups";
static struct proc_file *__pf_cgroups = NULL;

static prom_gauge_t *__metric_cgroup_subsys_hierarchy_count = NULL,
                    *__metric_cgroup_subsys_num_cgroups_count = NULL,
                    *__metric_cgroup_subsys_enabled = NULL;

/**
 * It registers metrics to the prometheus collector registry.
 *
 * @return The return value is the exit code of the plugin.
 */
int32_t init_collector_proc_cgroups() {
    __metric_cgroup_subsys_hierarchy_count = prom_collector_registry_must_register_metric(
        prom_gauge_new("cgroup_subsys_hierarchy_count", "cgroup subsystem hierarchy count", 1,
                       (const char *[]){ "subsys_name" }));

    __metric_cgroup_subsys_num_cgroups_count = prom_collector_registry_must_register_metric(
        prom_gauge_new("cgroup_subsys_num_cgroups", "cgroup subsystem cgroup count", 1,
                       (const char *[]){ "subsys_name" }));

    __metric_cgroup_subsys_enabled = prom_collector_registry_must_register_metric(prom_gauge_new(
        "cgroup_subsys_enabled", "cgroup subsystem enabled", 1, (const char *[]){ "subsys_name" }));

    debug("[PLUGIN_PROC:proc_cgroups] init successed");
    return 0;
}

int32_t collector_proc_cgroups(int32_t UNUSED(update_every), usec_t UNUSED(dt),
                               const char *config_path) {
    debug("[PLUGIN_PROC:proc_cgroups] config:%s running", config_path);

    const char *f_cgroups =
        appconfig_get_member_str(config_path, "monitor_file", __proc_cgroups_filename);

    if (unlikely(!__pf_cgroups)) {
        __pf_cgroups = procfile_open(f_cgroups, " \t", PROCFILE_FLAG_DEFAULT);
        if (unlikely(!__pf_cgroups)) {
            error("[PLUGIN_PROC:proc_cgroups] Cannot open %s", f_cgroups);
            return -1;
        }
        debug("[PLUGIN_PROC:proc_cgroups] opened '%s'", f_cgroups);
    }

    __pf_cgroups = procfile_readall(__pf_cgroups);
    if (unlikely(!__pf_cgroups)) {
        error("Cannot read %s", f_cgroups);
        return -1;
    }

    size_t lines = procfile_lines(__pf_cgroups);
    size_t words = 0;

    // 分配数组，/proc/cgroups这是固定的
    // if (unlikely(!__cgroups_info_ary)) {
    //     CC_ArrayConf ac;
    //     cc_array_conf_init(&ac);
    //     ac.capacity = lines - 1;
    //     cc_array_new_conf(&ac, &__cgroups_info_ary);
    // }

    uint64_t hierarchy = 0, num_cgroups = 0, enabled = 0;

    for (size_t l = 1; l < lines - 1; l++) {
        words = procfile_linewords(__pf_cgroups, l);
        if (unlikely(words < 4)) {
            error("[PLUGIN_PROC:proc_cgroups] '%s' line %lu has less than 4 words", f_cgroups, l);
            continue;
        }

        const char *subsys_name = procfile_lineword(__pf_cgroups, l, 0);

        hierarchy = strtoull(procfile_lineword(__pf_cgroups, l, 1), NULL, 10);
        num_cgroups = strtoull(procfile_lineword(__pf_cgroups, l, 2), NULL, 10);
        enabled = strtoull(procfile_lineword(__pf_cgroups, l, 3), NULL, 10);

        debug("[PLUGIN_PROC:proc_cgroups] subsys: '%s' hierarchy: %lu, num_cgroups: %lu, enabled: "
              "%lu",
              subsys_name, hierarchy, num_cgroups, enabled);

        // 设置指标
        prom_gauge_set(__metric_cgroup_subsys_hierarchy_count, hierarchy,
                       (const char *[]){ subsys_name });
        prom_gauge_set(__metric_cgroup_subsys_num_cgroups_count, num_cgroups,
                       (const char *[]){ subsys_name });
        prom_gauge_set(__metric_cgroup_subsys_enabled, enabled, (const char *[]){ subsys_name });
    }

    return 0;
}

void fini_collector_proc_cgroups() {
    if (likely(__pf_cgroups)) {
        procfile_close(__pf_cgroups);
        __pf_cgroups = NULL;
    }

    // CC_ARRAY_FOREACH(ci, __cgroups_info_ary, {
    //     free(ci);
    //     ci = NULL;
    // })

    debug("[PLUGIN_PROC:proc_cgroups] fini successed");
}
