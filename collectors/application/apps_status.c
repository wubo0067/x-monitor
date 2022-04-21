/*
 * @Author: CALM.WU
 * @Date: 2022-04-20 15:00:02
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-04-20 17:52:26
 */

#include "apps_status.h"
#include "collectc/cc_hashtable.h"

#include "utils/compiler.h"
#include "utils/log.h"
#include "utils/regex.h"
#include "utils/strings.h"
#include "utils/mempool.h"

#include "collectors/process/process_status.h"

// app_stat列表
static LIST_HEAD(__app_stat_list);
// app关联的进程列表
static CC_HashTable *__app_process_table = NULL;
//
struct xm_mempool_s *__process_status_xmp = NULL;
struct xm_mempool_s *__app_status_xmp = NULL;
struct xm_mempool_s *__app_assoc_process_xml = NULL;

/**
 * It compares two pid_t values and returns 0 if they are equal, and 1 if they are not equal.
 *
 * @param key1 The first key to compare.
 * @param key2 The key to compare against.
 *
 * @return The return value is the difference between the two values.
 */
static int32_t __app_process_compare(const void *key1, const void *key2) {
    pid_t *pid_1 = (pid_t *)key1;
    pid_t *pid_2 = (pid_t *)key2;
    return !(*pid_1 == *pid_2);
}

/**
 * It iterates over a list of app_status structs and sets all of their fields to zero
 */
static void __zero_all_appstatus() {
    struct list_head  *iter = NULL;
    struct app_status *as = NULL;

    __list_for_each(iter, &__app_stat_list) {
        as = list_entry(iter, struct app_status, l_member);
        as->minflt_raw = 0;
        as->cmajflt_raw = 0;
        as->majflt_raw = 0;
        as->cmajflt_raw = 0;
        as->utime_raw = 0;
        as->stime_raw = 0;
        as->cutime_raw = 0;
        as->cstime_raw = 0;
        as->process_cpu_time = 0.0;
        as->num_threads = 0;
        as->vmsize = 0;
        as->vmrss = 0;
        as->rssanon = 0;
        as->rssfile = 0;
        as->rssshmem = 0;
        as->vmswap = 0;
        as->pss = 0;
        as->uss = 0;
        as->io_logical_bytes_read = 0;
        as->io_logical_bytes_written = 0;
        as->io_read_calls = 0;
        as->io_write_calls = 0;
        as->io_storage_bytes_read = 0;
        as->io_storage_bytes_written = 0;
        as->io_cancelled_write_bytes = 0;
        as->open_fds = 0;
    }
}

static struct app_status *__get_app_status(pid_t pid, const char *app_name) {
    struct app_status *as = NULL;

    as = (struct app_status *)xm_mempool_malloc(__app_status_xmp);
    if (likely(as)) {
        memset(as, 0, sizeof(struct app_status));
        as->app_pid = pid;
        strlcpy(as->app_name, app_name, XM_APP_NAME_SIZE);
        INIT_LIST_HEAD(&as->l_member);
        list_add_tail(&as->l_member, &__app_stat_list);
    }
    return as;
}

static void __del_app_status(pid_t pid) {
    struct list_head  *iter = NULL;
    struct app_status *as = NULL;

    __list_for_each(iter, &__app_stat_list) {
        as = list_entry(iter, struct app_status, l_member);
        if (as->app_pid == pid) {
            list_del(&as->l_member);
            xm_mempool_free(__app_status_xmp, as);
            break;
        }
    }
}

/**
 * It initializes a hash table that will be used to store information about the processes that
 * are being monitored
 *
 * @return The return value is the status of the function.
 */
int32_t init_apps_collector() {
    if (!__app_process_table) {
        CC_HashTableConf config;
        cc_hashtable_config_init(&config);

        config.key_length = sizeof(pid_t);
        config.hash = GENERAL_HASH;
        config.key_compare = __app_process_compare;

        enum cc_stat status = cc_hashtable_new_conf(&config, &__app_process_table);

        if (unlikely(CC_OK != status)) {
            error("init app process table failed.");
            return -1;
        }
    }

    if (!__app_status_xmp) {
        __app_status_xmp = xm_mempool_init(sizeof(struct app_status), 32, 32);
    }

    if (!__process_status_xmp) {
        __process_status_xmp = xm_mempool_init(sizeof(struct process_status), 1024, 128);
    }

    if (!__app_assoc_process_xml) {
        __app_assoc_process_xml = xm_mempool_init(sizeof(struct app_assoc_process), 1024, 128);
    }
    return 0;
}

int32_t update_collection_apps(struct app_filter_rules *afr) {
    char cmdline[XM_PROC_LINE_SIZE] = { 0 };

    DIR *dir = opendir("/proc");
    if (unlikely(!dir)) {
        error("[PLUGIN_APPSTATUS]: opendir /proc failed. error: %s", strerror(errno));
        return -1;
    }

    struct dirent *de = NULL;

    while ((de = readdir(dir))) {
        char *endptr = de->d_name;

        // 跳过非/proc/pid目录
        if (unlikely(de->d_type != DT_DIR || !is_number(de->d_name))) {
            continue;
        }

        if (unlikely(endptr == de->d_name || *endptr != '\0'))
            continue;

        // 读取pid cmdline
    }
    return 0;
}

// 统计应用资源数据
int32_t collect_apps_usage() {
    int32_t ret = 0;

    // 清零应用的资源数据
    __zero_all_appstatus();
    // 清理进程采集标志位
    pid_t                    *key = NULL;
    struct app_assoc_process *ap = NULL;

    // 采集应用进程的资源数据
    CC_HASHTABLE_FOREACH(__app_process_table, key, ap) {
        ap->update = 0;

        // ** 判断进程是否应该采集，条件，app_pid = pid 或 app_id = pid

        // 采集进程数据
        COLLECTOR_PROCESS_USAGE(ap->ps_target, ret);

        if (unlikely(0 != ret)) {
            debug("[PLUGIN_APPSTATUS]: aggregating '%s' pid %d has exited", ap->ps_target->comm,
                  *key);
        } else {
            // app累计进程资源
            if (likely(ap->ps_target->pid == *key && NULL != ap->as_target
                       && (ap->ps_target->pid == ap->as_target->app_pid
                           || ap->ps_target->ppid == ap->as_target->app_pid))) {
                //
                ap->as_target->minflt_raw += ap->ps_target->minflt_raw;
                ap->as_target->cminflt_raw += ap->ps_target->cminflt_raw;
                ap->as_target->majflt_raw += ap->ps_target->majflt_raw;
                ap->as_target->cmajflt_raw += ap->ps_target->cmajflt_raw;
                ap->as_target->utime_raw += ap->ps_target->utime_raw;
                ap->as_target->stime_raw += ap->ps_target->stime_raw;
                ap->as_target->cutime_raw += ap->ps_target->cutime_raw;
                ap->as_target->cstime_raw += ap->ps_target->cstime_raw;
                ap->as_target->process_cpu_time += ap->ps_target->process_cpu_time;
                ap->as_target->num_threads += ap->ps_target->num_threads;
                ap->as_target->vmsize += ap->ps_target->vmsize;
                ap->as_target->vmrss += ap->ps_target->vmrss;
                ap->as_target->rssanon += ap->ps_target->rssanon;
                ap->as_target->rssfile += ap->ps_target->rssfile;
                ap->as_target->rssshmem += ap->ps_target->rssshmem;
                ap->as_target->vmswap += ap->ps_target->vmswap;
                ap->as_target->pss += ap->ps_target->pss;
                ap->as_target->uss += ap->ps_target->uss;
                ap->as_target->io_logical_bytes_read += ap->ps_target->io_logical_bytes_read;
                ap->as_target->io_logical_bytes_written += ap->ps_target->io_logical_bytes_written;
                ap->as_target->io_read_calls += ap->ps_target->io_read_calls;
                ap->as_target->io_write_calls += ap->ps_target->io_write_calls;
                ap->as_target->io_storage_bytes_read += ap->ps_target->io_storage_bytes_read;
                ap->as_target->io_storage_bytes_written += ap->ps_target->io_storage_bytes_written;
                ap->as_target->io_cancelled_write_bytes += ap->ps_target->io_cancelled_write_bytes;
                ap->as_target->open_fds += ap->ps_target->open_fds;

                ap->update = 1;
                debug("[PLUGIN_APPSTATUS]: aggregating '%s' pid %d on app '%s' app_pid: %d",
                      ap->ps_target->comm, *key, ap->as_target->app_name, ap->as_target->app_pid);
            }
        }
    }

    // 清理退出的进程和应用
    CC_HashTableIter iterator;
    TableEntry      *next_entry;
    cc_hashtable_iter_init(&iterator, __app_process_table);
    while (cc_hashtable_iter_next(&iterator, &next_entry) != CC_ITER_END) {
        pid_t                    *pid = (pid_t *)next_entry->key;
        struct app_assoc_process *ap = (struct app_assoc_process *)next_entry->value;

        if (!ap->update) {
            if (ap->ps_target->pid == ap->as_target->app_pid) {
                // 释放应用统计对象
                __del_app_status(ap->as_target->app_pid);
            }
            // 释放进程统计对象
            free_process_status(ap->ps_target, __process_status_xmp);
            // 释放应用进程对象

            cc_hashtable_iter_remove(&iterator, NULL);
        }
    }

    return 0;
}

void free_apps_collector() {
    // 释放应用进程对象

    // 释放应用统计对象

    if (likely(__process_status_xmp)) {
        xm_mempool_fini(__process_status_xmp);
    }

    if (likely(__app_status_xmp)) {
        xm_mempool_fini(__app_status_xmp);
    }

    if (likely(__app_assoc_process_xml)) {
        xm_mempool_fini(__app_assoc_process_xml);
    }
}