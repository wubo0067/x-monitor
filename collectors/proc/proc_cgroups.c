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

#define METRIC_UNIT(name)          \
    uint64_t      name;            \
    prom_gauge_t *__metric_##name; \
    char          __metric_##name##_name[PROM_METRIC_NAME_LEN];

struct cgroup_info {
    char subsys_name[MAX_NAME_LEN];

    METRIC_UNIT(hierarchy)
    METRIC_UNIT(num_cgroups)
    METRIC_UNIT(enabled)
};

static CC_Array *__cgroups_info_ary = NULL;

static struct cgroup_info *__get_cgroup_info(const char *subsys_name) {
    struct cgroup_info *ci = NULL;

    CC_ArrayIter iter;
    cc_array_iter_init(&iter, __cgroups_info_ary);
    while (cc_array_iter_next(&iter, (void *)&ci) != CC_ITER_END) {
        if (strcmp(ci->subsys_name, subsys_name) == 0) {
            // debug("[PLUGIN_PROC:proc_cgroups] found cgroup info for subsys: %s", subsys_name);
            return ci;
        }
    }

    ci = calloc(1, sizeof(struct cgroup_info));
    strncpy(ci->subsys_name, subsys_name, MAX_NAME_LEN - 1);

    snprintf(ci->__metric_hierarchy_name, PROM_METRIC_NAME_LEN - 1, "subsys_%s_hierarchy",
             subsys_name);
    ci->__metric_hierarchy = prom_collector_registry_must_register_metric(
        prom_gauge_new(ci->__metric_hierarchy_name, "cgroup subsystem hierarchy count", 1,
                       (const char *[]){ "proc.cgroups" }));

    snprintf(ci->__metric_num_cgroups_name, PROM_METRIC_NAME_LEN - 1, "subsys_%s_num_cgroups",
             subsys_name);
    ci->__metric_num_cgroups = prom_collector_registry_must_register_metric(
        prom_gauge_new(ci->__metric_num_cgroups_name, "cgroup subsystem cgroup count", 1,
                       (const char *[]){ "proc.cgroups" }));

    snprintf(ci->__metric_enabled_name, PROM_METRIC_NAME_LEN - 1, "subsys_%s_enabled", subsys_name);
    ci->__metric_enabled = prom_collector_registry_must_register_metric(
        prom_gauge_new(ci->__metric_enabled_name, "cgroup subsystem enabled", 1,
                       (const char *[]){ "proc.cgroups" }));

    // 加入数组
    cc_array_add(__cgroups_info_ary, ci);

    debug("[PLUGIN_PROC:proc_cgroups] add cgroup subsys: '%s'", subsys_name);

    return ci;
}

int32_t init_collector_proc_cgroups() {
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
            error("Cannot open %s", f_cgroups);
            return -1;
        }
    }

    __pf_cgroups = procfile_readall(__pf_cgroups);
    if (unlikely(!__pf_cgroups)) {
        error("Cannot read %s", f_cgroups);
        return -1;
    }

    size_t lines = procfile_lines(__pf_cgroups);
    size_t words = 0;

    // 分配数组，/proc/cgroups这是固定的
    if (unlikely(!__cgroups_info_ary)) {
        CC_ArrayConf ac;
        cc_array_conf_init(&ac);
        ac.capacity = lines - 1;
        cc_array_new_conf(&ac, &__cgroups_info_ary);
    }

    for (size_t l = 1; l < lines - 1; l++) {
        words = procfile_linewords(__pf_cgroups, l);
        if (unlikely(words < 4)) {
            error("[PLUGIN_PROC:proc_cgroups] '%s' line %lu has less than 4 words", f_cgroups, l);
            continue;
        }

        const char         *subsys_name = procfile_lineword(__pf_cgroups, l, 0);
        struct cgroup_info *ci = __get_cgroup_info(subsys_name);

        ci->hierarchy = strtoull(procfile_lineword(__pf_cgroups, l, 1), NULL, 10);
        ci->num_cgroups = strtoull(procfile_lineword(__pf_cgroups, l, 2), NULL, 10);
        ci->enabled = strtoull(procfile_lineword(__pf_cgroups, l, 3), NULL, 10);

        debug(
            "[PLUGIN_PROC:proc_cgroups] subsys: '%s' hierarchy: %lu num_cgroups: %lu enabled: %lu",
            subsys_name, ci->hierarchy, ci->num_cgroups, ci->enabled);

        // 设置指标
        prom_gauge_set(ci->__metric_hierarchy, ci->hierarchy, (const char *[]){ subsys_name });
        prom_gauge_set(ci->__metric_num_cgroups, ci->num_cgroups, (const char *[]){ subsys_name });
        prom_gauge_set(ci->__metric_enabled, ci->enabled, (const char *[]){ subsys_name });
    }

    return 0;
}

void fini_collector_proc_cgroups() {
    if (likely(__pf_cgroups)) {
        procfile_close(__pf_cgroups);
        __pf_cgroups = NULL;
    }

    CC_ARRAY_FOREACH(ci, __cgroups_info_ary, {
        free(ci);
        ci = NULL;
    })

    debug("[PLUGIN_PROC:proc_cgroups] fini successed");
}
