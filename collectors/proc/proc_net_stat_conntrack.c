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
#include "utils/files.h"

#include "appconfig/appconfig.h"

static const char *__proc_net_stat_nf_conntrack_filename = "/proc/net/stat/nf_conntrack";
// static const char *__proc_sys_net_netfilter_nf_conntrack_count =
//     "/proc/sys/net/netfilter/nf_conntrack_count";
static const char *__proc_nf_conntrack_max = "/proc/sys/net/netfilter/nf_conntrack_max";

static struct proc_file *__pf_net_stat_nf_conntrack = NULL;

static struct nf_conntrack_metrics {
    uint64_t entries;
    uint64_t searched;
    uint64_t found;
    uint64_t new;
    uint64_t invalid;
    uint64_t ignore;
    uint64_t delete;
    uint64_t delete_list;
    uint64_t insert;
    uint64_t insert_failed;
    uint64_t drop;
    uint64_t early_drop;
    uint64_t icmp_error;
    uint64_t expect_new;
    uint64_t expect_create;
    uint64_t expect_delete;
    uint64_t search_restart;
    uint64_t max;
} __nf_conntrack_metrics = { 0 };

static prom_gauge_t *__metric_nf_conntrack_entries = NULL, *__metric_nf_conntrack_searched = NULL,
                    *__metric_nf_conntrack_found = NULL, *__metric_nf_conntrack_new = NULL,
                    *__metric_nf_conntrack_invalid = NULL, *__metric_nf_conntrack_ignore = NULL,
                    *__metric_nf_conntrack_delete = NULL, *__metric_nf_conntrack_delete_list = NULL,
                    *__metric_nf_conntrack_insert = NULL,
                    *__metric_nf_conntrack_insert_failed = NULL, *__metric_nf_conntrack_drop = NULL,
                    *__metric_nf_conntrack_early_drop = NULL,
                    *__metric_nf_conntrack_icmp_error = NULL,
                    *__metric_nf_conntrack_expect_new = NULL,
                    *__metric_nf_conntrack_expect_create = NULL,
                    *__metric_nf_conntrack_expect_delete = NULL,
                    *__metric_nf_conntrack_search_restart = NULL, *__metric_nf_conntrack_max = NULL;

int32_t init_collector_proc_net_stat_conntrack() {
    // 构建指标对象
    __metric_nf_conntrack_entries = prom_collector_registry_must_register_metric(
        prom_gauge_new("nf_conntrack_entries", "Number of entries in conntrack table.", 1,
                       (const char *[]){ "nf_conntrack" }));
    __metric_nf_conntrack_searched = prom_collector_registry_must_register_metric(
        prom_gauge_new("nf_conntrack_searched", "Number of conntrack table lookups performed.", 1,
                       (const char *[]){ "nf_conntrack" }));
    __metric_nf_conntrack_found = prom_collector_registry_must_register_metric(
        prom_gauge_new("nf_conntrack_found", "Number of searched entries which were successful.", 1,
                       (const char *[]){ "nf_conntrack" }));
    __metric_nf_conntrack_new = prom_collector_registry_must_register_metric(prom_gauge_new(
        "nf_conntrack_new", "Number of conntrack entries added which were not expected before.", 1,
        (const char *[]){ "nf_conntrack" }));
    __metric_nf_conntrack_invalid = prom_collector_registry_must_register_metric(
        prom_gauge_new("nf_conntrack_invalid", "Number of packets seen which can not be tracked.",
                       1, (const char *[]){ "nf_conntrack" }));
    __metric_nf_conntrack_ignore = prom_collector_registry_must_register_metric(
        prom_gauge_new("nf_conntrack_ignore",
                       "Number of packets seen which are already connected to a conntrack entry.",
                       1, (const char *[]){ "nf_conntrack" }));
    __metric_nf_conntrack_delete = prom_collector_registry_must_register_metric(
        prom_gauge_new("nf_conntrack_delete", "Number of conntrack entries which were removed.", 1,
                       (const char *[]){ "nf_conntrack" }));
    __metric_nf_conntrack_delete_list = prom_collector_registry_must_register_metric(prom_gauge_new(
        "nf_conntrack_delete_list", "Number of conntrack entries which were put to dying list.", 1,
        (const char *[]){ "nf_conntrack" }));
    __metric_nf_conntrack_insert = prom_collector_registry_must_register_metric(
        prom_gauge_new("nf_conntrack_insert", "Number of entries inserted into the list.", 1,
                       (const char *[]){ "nf_conntrack" }));

    __metric_nf_conntrack_insert_failed = prom_collector_registry_must_register_metric(
        prom_gauge_new("nf_conntrack_insert_failed",
                       "Number of entries for which list insertion was attempted but failed "
                       "(happens if the same entry is already present).",
                       1, (const char *[]){ "nf_conntrack" }));
    __metric_nf_conntrack_drop = prom_collector_registry_must_register_metric(
        prom_gauge_new("nf_conntrack_drop",
                       "Number of packets dropped due to conntrack failure. Either new conntrack "
                       "entry allocation failed, or protocol helper dropped the packet.",
                       1, (const char *[]){ "nf_conntrack" }));

    __metric_nf_conntrack_early_drop = prom_collector_registry_must_register_metric(
        prom_gauge_new("nf_conntrack_early_drop",
                       "Number of dropped conntrack entries to make room for new ones, if maximum "
                       "table size was reached.",
                       1, (const char *[]){ "nf_conntrack" }));
    __metric_nf_conntrack_icmp_error = prom_collector_registry_must_register_metric(
        prom_gauge_new("nf_conntrack_icmp_error",
                       "Number of packets which could not be tracked due to error situation. This "
                       "is a subset of invalid.",
                       1, (const char *[]){ "nf_conntrack" }));
    __metric_nf_conntrack_expect_new = prom_collector_registry_must_register_metric(prom_gauge_new(
        "nf_conntrack_expect_new",
        "Number of conntrack entries added after an expectation for them was already present.", 1,
        (const char *[]){ "nf_conntrack" }));
    __metric_nf_conntrack_expect_create = prom_collector_registry_must_register_metric(
        prom_gauge_new("nf_conntrack_expect_create", "Number of expectations added.", 1,
                       (const char *[]){ "nf_conntrack" }));
    __metric_nf_conntrack_expect_delete = prom_collector_registry_must_register_metric(
        prom_gauge_new("nf_conntrack_expect_delete", "Number of expectations deleted.", 1,
                       (const char *[]){ "nf_conntrack" }));
    __metric_nf_conntrack_search_restart =
        prom_collector_registry_must_register_metric(prom_gauge_new(
            "nf_conntrack_search_restart",
            "Number of conntrack table lookups which had to be restarted due to hashtable resizes.",
            1, (const char *[]){ "nf_conntrack" }));
    __metric_nf_conntrack_max = prom_collector_registry_must_register_metric(
        prom_gauge_new("nf_conntrack_max", "Maximum number of entries in conntrack table.", 1,
                       (const char *[]){ "nf_conntrack" }));
    debug("[PLUGIN_PROC:proc_net_stat_nf_conntrack] init successed");
    return 0;
}

int32_t collector_proc_net_stat_conntrack(int32_t UNUSED(update_every), usec_t UNUSED(dt),
                                          const char *config_path) {
    debug("[PLUGIN_PROC:proc_net_stat_nf_conntrack] config:%s running", config_path);

    const char *f_nf_conntrack = appconfig_get_member_str(config_path, "monitor_file",
                                                          __proc_net_stat_nf_conntrack_filename);

    if (unlikely(!__pf_net_stat_nf_conntrack)) {
        __pf_net_stat_nf_conntrack = procfile_open(f_nf_conntrack, " \t:", PROCFILE_FLAG_DEFAULT);
        if (unlikely(!__pf_net_stat_nf_conntrack)) {
            error("Cannot open %s", f_nf_conntrack);
            return -1;
        }
    }

    __pf_net_stat_nf_conntrack = procfile_readall(__pf_net_stat_nf_conntrack);
    if (unlikely(!__pf_net_stat_nf_conntrack)) {
        error("Cannot read %s", f_nf_conntrack);
        return -1;
    }

    size_t lines = procfile_lines(__pf_net_stat_nf_conntrack);
    size_t words = 0;

    memset(&__nf_conntrack_metrics, 0, sizeof(struct nf_conntrack_metrics));
    // debug("[PLUGIN_PROC:proc_net_stat_nf_conntrack] file: '%s' have lines:%zu", f_nf_conntrack,
    //       lines);

    // 每一行是一个cpu统计信息
    for (size_t l = 1; l < lines - 1; l++) {
        words = procfile_linewords(__pf_net_stat_nf_conntrack, l);
        if (unlikely(words < 17)) {
            error("Cannot read %s: line %lu have %lu words which is too short, expected 17 words.",
                  f_nf_conntrack, l, words);
            continue;
        }

        // 表内连接追踪数量所有cpu都是一样的
        __nf_conntrack_metrics.entries =
            strtoull(procfile_lineword(__pf_net_stat_nf_conntrack, l, 0), NULL, 16);
        // 这是每个cpu的，所以要累计
        __nf_conntrack_metrics.searched +=
            strtoull(procfile_lineword(__pf_net_stat_nf_conntrack, l, 1), NULL, 16);
        __nf_conntrack_metrics.found +=
            strtoull(procfile_lineword(__pf_net_stat_nf_conntrack, l, 2), NULL, 16);
        __nf_conntrack_metrics.new +=
            strtoull(procfile_lineword(__pf_net_stat_nf_conntrack, l, 3), NULL, 16);
        __nf_conntrack_metrics.invalid +=
            strtoull(procfile_lineword(__pf_net_stat_nf_conntrack, l, 4), NULL, 16);
        __nf_conntrack_metrics.ignore +=
            strtoull(procfile_lineword(__pf_net_stat_nf_conntrack, l, 5), NULL, 16);
        __nf_conntrack_metrics.delete +=
            strtoull(procfile_lineword(__pf_net_stat_nf_conntrack, l, 6), NULL, 16);
        __nf_conntrack_metrics.delete_list +=
            strtoull(procfile_lineword(__pf_net_stat_nf_conntrack, l, 7), NULL, 16);
        __nf_conntrack_metrics.insert +=
            strtoull(procfile_lineword(__pf_net_stat_nf_conntrack, l, 8), NULL, 16);
        __nf_conntrack_metrics.insert_failed +=
            strtoull(procfile_lineword(__pf_net_stat_nf_conntrack, l, 9), NULL, 16);
        __nf_conntrack_metrics.drop +=
            strtoull(procfile_lineword(__pf_net_stat_nf_conntrack, l, 10), NULL, 16);
        __nf_conntrack_metrics.early_drop +=
            strtoull(procfile_lineword(__pf_net_stat_nf_conntrack, l, 11), NULL, 16);
        __nf_conntrack_metrics.icmp_error +=
            strtoull(procfile_lineword(__pf_net_stat_nf_conntrack, l, 12), NULL, 16);
        __nf_conntrack_metrics.expect_new +=
            strtoull(procfile_lineword(__pf_net_stat_nf_conntrack, l, 13), NULL, 16);
        __nf_conntrack_metrics.expect_create +=
            strtoull(procfile_lineword(__pf_net_stat_nf_conntrack, l, 14), NULL, 16);
        __nf_conntrack_metrics.expect_delete +=
            strtoull(procfile_lineword(__pf_net_stat_nf_conntrack, l, 15), NULL, 16);
        __nf_conntrack_metrics.search_restart +=
            strtoull(procfile_lineword(__pf_net_stat_nf_conntrack, l, 16), NULL, 16);
    }

    read_file_to_uint64(__proc_nf_conntrack_max, &__nf_conntrack_metrics.max);

    debug("[PLUGIN_PROC:proc_net_stat_nf_conntrack] entries: %lu, searched: %lu, found: %lu, new: "
          "%lu, invalid: %lu, ignore: %lu, delete: %lu, delete_list: %lu, insert: %lu, "
          "insert_failed: %lu, drop: %lu, early_drop: %lu, icmp_error: %lu, expect_new: %lu, "
          "expect_create: %lu, expect_delete: %lu, search_restart: %lu max: %lu",
          __nf_conntrack_metrics.entries, __nf_conntrack_metrics.searched,
          __nf_conntrack_metrics.found, __nf_conntrack_metrics.new, __nf_conntrack_metrics.invalid,
          __nf_conntrack_metrics.ignore, __nf_conntrack_metrics.delete,
          __nf_conntrack_metrics.delete_list, __nf_conntrack_metrics.insert,
          __nf_conntrack_metrics.insert_failed, __nf_conntrack_metrics.drop,
          __nf_conntrack_metrics.early_drop, __nf_conntrack_metrics.icmp_error,
          __nf_conntrack_metrics.expect_new, __nf_conntrack_metrics.expect_create,
          __nf_conntrack_metrics.expect_delete, __nf_conntrack_metrics.search_restart,
          __nf_conntrack_metrics.max);

    // 设置指标
    prom_gauge_set(__metric_nf_conntrack_entries, __nf_conntrack_metrics.entries,
                   (const char *[]){ "connections" });

    prom_gauge_set(__metric_nf_conntrack_searched, __nf_conntrack_metrics.searched,
                   (const char *[]){ "Connection Tracker Searches" });
    prom_gauge_set(__metric_nf_conntrack_found, __nf_conntrack_metrics.found,
                   (const char *[]){ "Connection Tracker Searches" });
    prom_gauge_set(__metric_nf_conntrack_search_restart, __nf_conntrack_metrics.search_restart,
                   (const char *[]){ "Connection Tracker Searches" });

    prom_gauge_set(__metric_nf_conntrack_icmp_error, __nf_conntrack_metrics.icmp_error,
                   (const char *[]){ "Connection Tracker Errors" });
    prom_gauge_set(__metric_nf_conntrack_insert_failed, __nf_conntrack_metrics.insert_failed,
                   (const char *[]){ "Connection Tracker Errors" });
    prom_gauge_set(__metric_nf_conntrack_drop, __nf_conntrack_metrics.drop,
                   (const char *[]){ "Connection Tracker Errors" });
    prom_gauge_set(__metric_nf_conntrack_early_drop, __nf_conntrack_metrics.early_drop,
                   (const char *[]){ "Connection Tracker Errors" });

    prom_gauge_set(__metric_nf_conntrack_expect_create, __nf_conntrack_metrics.expect_create,
                   (const char *[]){ "Connection Tracker Expectations" });
    prom_gauge_set(__metric_nf_conntrack_expect_delete, __nf_conntrack_metrics.expect_delete,
                   (const char *[]){ "Connection Tracker Expectations" });
    prom_gauge_set(__metric_nf_conntrack_expect_new, __nf_conntrack_metrics.expect_new,
                   (const char *[]){ "Connection Tracker Expectations" });

    prom_gauge_set(__metric_nf_conntrack_insert, __nf_conntrack_metrics.insert,
                   (const char *[]){ "Connection Tracker Changes" });
    prom_gauge_set(__metric_nf_conntrack_delete, __nf_conntrack_metrics.delete,
                   (const char *[]){ "Connection Tracker Changes" });
    prom_gauge_set(__metric_nf_conntrack_delete_list, __nf_conntrack_metrics.delete_list,
                   (const char *[]){ "Connection Tracker Changes" });

    prom_gauge_set(__metric_nf_conntrack_new, __nf_conntrack_metrics.new,
                   (const char *[]){ "Connection Tracker New Connections" });
    prom_gauge_set(__metric_nf_conntrack_ignore, __nf_conntrack_metrics.ignore,
                   (const char *[]){ "Connection Tracker New Connections" });
    prom_gauge_set(__metric_nf_conntrack_invalid, __nf_conntrack_metrics.invalid,
                   (const char *[]){ "Connection Tracker New Connections" });

    prom_gauge_set(__metric_nf_conntrack_max, __nf_conntrack_metrics.max,
                   (const char *[]){ "Connection Tracker Max Connections" });

    return 0;
}

void fini_collector_proc_net_stat_conntrack() {
    if (likely(__pf_net_stat_nf_conntrack)) {
        procfile_close(__pf_net_stat_nf_conntrack);
        __pf_net_stat_nf_conntrack = NULL;
    }

    debug("[PLUGIN_PROC:proc_net_stat_nf_conntrack] stopped");
}