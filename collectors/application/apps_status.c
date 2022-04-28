/*
 * @Author: CALM.WU
 * @Date: 2022-04-20 15:00:02
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-04-24 17:58:59
 */

#include "apps_status.h"
#include "apps_filter_rule.h"

#include "collectc/cc_hashtable.h"

#include "utils/compiler.h"
#include "utils/log.h"
#include "utils/regex.h"
#include "utils/strings.h"
#include "utils/mempool.h"
#include "utils/consts.h"
#include "utils/files.h"
#include "utils/os.h"

#include "collectors/process/process_status.h"

// app_stat列表
static LIST_HEAD(__app_status_list);
// app关联的进程列表
static CC_HashTable *__app_assoc_process_table = NULL;
//
struct xm_mempool_s *__process_status_xmp = NULL;
struct xm_mempool_s *__app_status_xmp = NULL;
struct xm_mempool_s *__app_assoc_process_xmp = NULL;

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
static int32_t __zero_all_appstatus() {
    int32_t            app_status_count = 0;
    struct list_head  *iter = NULL;
    struct app_status *as = NULL;

    __list_for_each(iter, &__app_status_list) {
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

        app_status_count++;
    }
    return app_status_count;
}

static struct app_status *__get_app_status(pid_t pid, const char *app_name) {
    struct app_status *as = NULL;
    struct list_head  *iter = NULL;

    // ** 判断这个pid对应的应用是否存在，相同的规则只能存在一个进程对应应用
    __list_for_each(iter, &__app_status_list) {
        as = list_entry(iter, struct app_status, l_member);
        if (likely(0 == strcmp(as->app_name, app_name) && as->app_pid == pid)) {
            debug("[PLUGIN_APPSTATUS] the app '%s' is already exists with pid: %d", app_name, pid);
            return as;
        } else if (0 == strcmp(as->app_name, app_name)) {
            // ** app_name是唯一的
            error("[PLUGIN_APPSTATUS] the app '%s' is already exists with pid: %d, so new pid: %d "
                  "cannot be bound to the same app",
                  app_name, as->app_pid, pid);
            return NULL;
        }
    }

    as = (struct app_status *)xm_mempool_malloc(__app_status_xmp);
    if (likely(as)) {
        memset(as, 0, sizeof(struct app_status));
        as->app_pid = pid;
        strlcpy(as->app_name, app_name, XM_APP_NAME_SIZE);
        INIT_LIST_HEAD(&as->l_member);
        list_add_tail(&as->l_member, &__app_status_list);
        debug("[PLUGIN_APPSTATUS] add app_status for app '%s' with pid: %d", app_name, pid);
    }
    return as;
}

/**
 * > Delete the app_assoc_process object from the hashtable, free the process_status object, and
 * free the app_assoc_process object
 *
 * @param aap The object to be deleted
 */
static void __del_app_assoc_process(struct app_assoc_process *aap, bool remove_from_hashtable) {
    if (likely(aap)) {
        if (remove_from_hashtable) {
            // 从hashtable中删除
            cc_hashtable_remove(__app_assoc_process_table, (void *)&aap->ps_target->pid, NULL);
        }

        aap->as_target->process_count--;
        debug("[PLUGIN_APPSTATUS] del app_assoc_process for pid: %d, app_pid: %d on app: '%s', "
              "current app have %d processes",
              aap->ps_target->pid, aap->as_target->app_pid, aap->as_target->app_name,
              aap->as_target->process_count);
        aap->as_target = NULL;
        // 释放进程统计对象
        free_process_status(aap->ps_target, __process_status_xmp);
        aap->ps_target = NULL;
        // 释放应用进程关联对象
        xm_mempool_free(__app_assoc_process_xmp, aap);
        aap = NULL;
    }
}

// 构造应用进程关联结构对象
static struct app_assoc_process *__get_app_assoc_process(struct app_status *as, pid_t pid,
                                                         pid_t app_pid, const char *app_name) {
    struct app_assoc_process *aap = NULL;

    // debug("[PLUGIN_APPSTATUS] There are %lu processes being collected",
    //       cc_hashtable_size(__app_assoc_process_table));

    // ** 判断这个pid对应的关联对象是否存在
    if (unlikely(cc_hashtable_get(__app_assoc_process_table, &pid, (void *)&aap) == CC_OK)) {
        // 如果存在，判断对应的app_status是否和当前的一致
        if (likely(NULL != aap->as_target && aap->as_target->app_pid == app_pid
                   && 0 == strcmp(aap->as_target->app_name, app_name))) {
            debug("the app_assoc_process already exists. pid: %d, app_pid: %d on app '%s'", pid,
                  app_pid, app_name);
            return aap;
        } else {
            // 释放这个关联对象
            warn("[PLUGIN_APPSTATUS] app_assoc_process already exists for pid: %d, exist_app_pid: "
                 "%d, exist_app_name: '%s', curr_app_pid: %d, curr_app_name: '%s', but the "
                 "app_status is not the same, so delete it",
                 pid, aap->as_target->app_pid, aap->as_target->app_name, app_pid, app_name);
            __del_app_assoc_process(aap, true);
        }
    }

    // 构造一个新的关联对象
    aap = (struct app_assoc_process *)xm_mempool_malloc(__app_assoc_process_xmp);
    if (likely(aap)) {
        memset(aap, 0, sizeof(struct app_assoc_process));
        aap->as_target = as;
        aap->ps_target = new_process_status(pid, __process_status_xmp);
        aap->update = 0;
        cc_hashtable_add(__app_assoc_process_table, (void *)&(aap->ps_target->pid), (void *)aap);
        as->process_count++;
        debug("[PLUGIN_APPSTATUS] add app_assoc_process pid: %d, app_pid: %d on app '%s', "
              "the app have %d processes, total %lu processes",
              pid, app_pid, app_name, as->process_count,
              cc_hashtable_size(__app_assoc_process_table));
    }

    return aap;
}

/**
 * > It reads the command line of a process, and if it matches a rule, it creates an app_status
 * object and an app_assoc_process object
 *
 * @param pid the process ID of the process to be matched
 * @param afr The application filter rules
 */
static int32_t __match_app_process(pid_t pid, struct app_filter_rules *afr) {
    int32_t                   ret = 0;
    uint16_t                  must_match_count = 0;
    uint8_t                   app_process_is_matched = 0;
    int32_t                   read_size = 0;
    int32_t                   child_pid_count = 0;
    struct app_status        *as = NULL;
    struct app_assoc_process *aap = NULL;
    char                      cmd_line[XM_CMD_LINE_MAX] = { 0 };
    char                      children_pids_file[XM_PROC_FILENAME_MAX] = { 0 };
    char                      children_pids_line[XM_PROC_LINE_SIZE] = { 0 };
    uint64_t                  children_pids[XM_CHILDPID_COUNT_MAX] = { 0 };

    // 读取进程的命令行
    ret = read_proc_pid_cmdline(pid, cmd_line, XM_CMD_LINE_MAX);
    if (unlikely(ret)) {
        // error("[PLUGIN_APPSTATUS] read pid:%d cmdline failed", pid);
        return -1;
    }

    // 过滤每个规则
    struct list_head               *iter = NULL;
    struct app_process_filter_rule *rule = NULL;

    __list_for_each(iter, &afr->rule_list_head) {

        rule = list_entry(iter, struct app_process_filter_rule, l_member);
        if (!rule->enable || rule->is_matched) {
            // 没有enable，或已经匹配过，跳过
            continue;
        }

        must_match_count = rule->key_count;
        app_process_is_matched = 0;

        for (uint16_t k_i = 0; k_i < rule->key_count; k_i++) {
            //** strstr在cmd_line中查找rule->key[k_i]
            if (strstr(cmd_line, rule->keys[k_i])) {
                must_match_count--;
            } else {
                break;
            }
        }

        if (0 == must_match_count) {
            // ** 所有的key都匹配成功
            info("[PLUGIN_APPSTATUS] the pid:%d match all of keys with rule '%s'", pid,
                 rule->app_name);
            app_process_is_matched = 1;
            break;
        }
    }

    if (app_process_is_matched) {
        // 构造应用统计结构对象
        as = __get_app_status(pid, rule->app_name);
        if (unlikely(!as)) {
            error("[PLUGIN_APPSTATUS] get app_status for pid %d on app '%s' failed", pid,
                  rule->app_name);
            return -1;
        }
        // 规则成功匹配应用对象
        rule->is_matched = 1;

        // 构造应用进程关联结构对象
        aap = __get_app_assoc_process(as, pid, pid, rule->app_name);
        if (unlikely(NULL == aap)) {
            info("[PLUGIN_APPSTATUS] get app_assoc_process for pid %d, app_pid %d on app '%s' "
                 "failed",
                 pid, pid, rule->app_name);
            return -1;
        }

        // 匹配成功，判断rule的assign_type
        if (rule->assign_type == APP_ASSIGN_PIDS_KEYS_MATCH_PID_AND_PPID) {
            // ** /proc/pid/task/tid/children读取该文件，分解为pid列表
            snprintf(children_pids_file, XM_PROC_FILENAME_MAX, "/proc/%d/task/%d/children", pid,
                     pid);
            read_size = read_file(children_pids_file, children_pids_line, XM_PROC_LINE_SIZE - 1);
            if (likely(read_size > 0)) {
                child_pid_count = str_split_to_nums(children_pids_line, " ", children_pids,
                                                    XM_CHILDPID_COUNT_MAX);

                debug("[PLUGIN_APPSTATUS] children file '%s' have %d pids: '%s'",
                      children_pids_file, child_pid_count, children_pids_line);

                if (likely(child_pid_count > 0)) {
                    // 判断这个pid是否存在，而且父进程是否相同
                    for (int32_t ci = 0; ci < child_pid_count; ci++) {
                        pid_t child_pid = (pid_t)children_pids[ci];
                        aap = __get_app_assoc_process(as, child_pid, pid, rule->app_name);
                        if (unlikely(NULL == aap)) {
                            error("[PLUGIN_APPSTATUS] get app_assoc_process for pid %d, app_pid "
                                  "%d on app '%s' failed",
                                  child_pid, pid, rule->app_name);
                            return -1;
                        }
                    }
                }
            }
        }
    }

    return 0;
}

/**
 * It initializes a hash table that will be used to store information about the processes that
 * are being monitored
 *
 * @return The return value is the status of the function.
 */
int32_t init_apps_collector() {
    if (!__app_assoc_process_table) {
        CC_HashTableConf config;

        cc_hashtable_conf_init(&config);

        config.key_length = sizeof(pid_t);
        config.hash = GENERAL_HASH;
        config.key_compare = __app_process_compare;

        enum cc_stat status = cc_hashtable_new_conf(&config, &__app_assoc_process_table);

        if (unlikely(CC_OK != status)) {
            error("[PLUGIN_APPSTATUS] init app process table failed.");
            return -1;
        }
    }

    if (!__app_status_xmp) {
        __app_status_xmp = xm_mempool_init(sizeof(struct app_status), 32, 32);
    }

    if (!__process_status_xmp) {
        __process_status_xmp = xm_mempool_init(sizeof(struct process_status), 1024, 128);
    }

    if (!__app_assoc_process_xmp) {
        __app_assoc_process_xmp = xm_mempool_init(sizeof(struct app_assoc_process), 1024, 128);
    }
    return 0;
}

int32_t update_app_collection(struct app_filter_rules *afr) {
    pid_t pid = 0;

    debug("[PLUGIN_APPSTATUS] start update app collection with %d filter rules", afr->rule_count);

    DIR *dir = opendir("/proc");
    if (unlikely(!dir)) {
        error("[PLUGIN_APPSTATUS] opendir /proc failed. error: %s", strerror(errno));
        return -1;
    }

    struct dirent *de = NULL;

    while ((de = readdir(dir))) {
        char *endptr = de->d_name;
        // 跳过非/proc/pid目录
        if (unlikely(de->d_type != DT_DIR || de->d_name[0] < '0' || de->d_name[0] > '9')) {
            continue;
        }

        // 应用、进程匹配
        pid = (pid_t)strtoul(de->d_name, &endptr, 10);

        if (unlikely(endptr == de->d_name || *endptr != '\0'))
            continue;

        __match_app_process(pid, afr);
    }
    closedir(dir);
    debug("[PLUGIN_APPSTATUS] update app collection done.");
    return 0;
}

// 统计应用资源数据
int32_t collecting_apps_usage(/*struct app_filter_rules *afr*/) {
    int32_t ret = 0;

    debug("[PLUGIN_APPSTATUS] start collecting apps usage.");

    // 清零应用的资源数据
    if (unlikely(0 == __zero_all_appstatus())) {
        debug("[PLUGIN_APPSTATUS] apps collection is empty.");
        return 0;
    }

    // 清理进程采集标志位
    pid_t                     key_pid;
    struct app_status        *as = NULL;
    struct app_assoc_process *aap = NULL;
    CC_HashTableIter          iter_hash;
    TableEntry               *next_entry = NULL;

    // 采集应用进程的资源数据
    cc_hashtable_iter_init(&iter_hash, __app_assoc_process_table);
    while (cc_hashtable_iter_next(&iter_hash, &next_entry) != CC_ITER_END) {

        key_pid = *(pid_t *)next_entry->key;
        aap = (struct app_assoc_process *)next_entry->value;

        debug("[PLUGIN_APPSTATUS] collect app '%s' pid %d comm '%s' usage.",
              aap->as_target->app_name, key_pid, aap->ps_target->comm);

        aap->update = 0;

        // 采集进程数据
        COLLECTOR_PROCESS_USAGE(aap->ps_target, ret);

        if (unlikely(0 != ret)) {
            debug("[PLUGIN_APPSTATUS] failed to collect pid: %d on app '%s' comm '%s' resources "
                  "used.",
                  key_pid, aap->as_target->app_name, aap->ps_target->comm);
        } else {
            // app累计进程资源
            if (likely(aap->ps_target->pid == key_pid && NULL != aap->as_target
                       && (aap->ps_target->pid == aap->as_target->app_pid
                           || aap->ps_target->ppid == aap->as_target->app_pid))) {
                aap->as_target->minflt_raw += aap->ps_target->minflt_raw;
                aap->as_target->cminflt_raw += aap->ps_target->cminflt_raw;
                aap->as_target->majflt_raw += aap->ps_target->majflt_raw;
                aap->as_target->cmajflt_raw += aap->ps_target->cmajflt_raw;
                aap->as_target->utime_raw += aap->ps_target->utime_raw;
                aap->as_target->stime_raw += aap->ps_target->stime_raw;
                aap->as_target->cutime_raw += aap->ps_target->cutime_raw;
                aap->as_target->cstime_raw += aap->ps_target->cstime_raw;
                aap->as_target->process_cpu_time += aap->ps_target->process_cpu_time;
                aap->as_target->num_threads += aap->ps_target->num_threads;
                aap->as_target->vmsize += aap->ps_target->vmsize;
                aap->as_target->vmrss += aap->ps_target->vmrss;
                aap->as_target->rssanon += aap->ps_target->rssanon;
                aap->as_target->rssfile += aap->ps_target->rssfile;
                aap->as_target->rssshmem += aap->ps_target->rssshmem;
                aap->as_target->vmswap += aap->ps_target->vmswap;
                aap->as_target->pss += aap->ps_target->pss;
                aap->as_target->uss += aap->ps_target->uss;
                aap->as_target->io_logical_bytes_read += aap->ps_target->io_logical_bytes_read;
                aap->as_target->io_logical_bytes_written +=
                    aap->ps_target->io_logical_bytes_written;
                aap->as_target->io_read_calls += aap->ps_target->io_read_calls;
                aap->as_target->io_write_calls += aap->ps_target->io_write_calls;
                aap->as_target->io_storage_bytes_read += aap->ps_target->io_storage_bytes_read;
                aap->as_target->io_storage_bytes_written +=
                    aap->ps_target->io_storage_bytes_written;
                aap->as_target->io_cancelled_write_bytes +=
                    aap->ps_target->io_cancelled_write_bytes;
                aap->as_target->open_fds += aap->ps_target->process_open_fds;

                aap->update = 1;
                debug("[PLUGIN_APPSTATUS] aggregating '%s' pid %d on app '%s' app_pid: %d",
                      aap->ps_target->comm, key_pid, aap->as_target->app_name,
                      aap->as_target->app_pid);
            }
        }
    }

    debug("[PLUGIN_APPSTATUS] clean up apps and processes.");

    // **先清理进程，update = 0表明进程不存在
    cc_hashtable_iter_init(&iter_hash, __app_assoc_process_table);
    while (cc_hashtable_iter_next(&iter_hash, &next_entry) != CC_ITER_END) {
        key_pid = *(pid_t *)next_entry->key;
        aap = (struct app_assoc_process *)next_entry->value;

        if (!aap->update) {
            debug("[PLUGIN_APPSTATUS] pid %d on app '%s' is not update so will be removed.",
                  key_pid, aap->as_target->app_name);

            cc_hashtable_iter_remove(&iter_hash, NULL);
            // 释放应用进程关联对象，在这里同时释放进程统计对象
            __del_app_assoc_process(aap, false);
        }
    }

    debug("[PLUGIN_APPSTATUS] There are %lu processes being collected",
          cc_hashtable_size(__app_assoc_process_table));

    // 清理应用统计对象
    struct list_head *iter_list;
again:
    __list_for_each(iter_list, &__app_status_list) {
        as = list_entry(iter_list, struct app_status, l_member);
        debug("[PLUGIN_APPSTATUS] app '%s' app_pid: %d current have %d processes.", as->app_name,
              as->app_pid, as->process_count);

        if (0 == as->process_count) {
            //
            info("[PLUGIN_APPSTATUS] app '%s' app_pid: %d to be cleaned up", as->app_name,
                 as->app_pid);

            list_del(&as->l_member);
            xm_mempool_free(__app_status_xmp, as);
            goto again;
        }
    }
    debug("[PLUGIN_APPSTATUS] collecting apps usage done.");
    // 更新应用指标
    return 0;
}

void free_apps_collector() {
    struct app_status        *as = NULL;
    struct app_assoc_process *aap = NULL;

    // 释放应用进程对象
    CC_HashTableIter iter_hash;
    TableEntry      *next_entry;
    cc_hashtable_iter_init(&iter_hash, __app_assoc_process_table);
    while (cc_hashtable_iter_next(&iter_hash, &next_entry) != CC_ITER_END) {
        // pid_t *pid = (pid_t *)next_entry->key;
        aap = (struct app_assoc_process *)next_entry->value;

        // 释放应用进程关联对象，在这里同时释放进程统计对象
        cc_hashtable_iter_remove(&iter_hash, NULL);
        __del_app_assoc_process(aap, false);
    }

    // 释放应用统计对象
    struct list_head *iter_list;
again:
    __list_for_each(iter_list, &__app_status_list) {
        as = list_entry(iter_list, struct app_status, l_member);
        list_del(&as->l_member);
        xm_mempool_free(__app_status_xmp, as);
        goto again;
    }

    if (likely(__app_assoc_process_table)) {
        cc_hashtable_destroy(__app_assoc_process_table);
        __app_assoc_process_table = NULL;
    }

    if (likely(__process_status_xmp)) {
        xm_mempool_fini(__process_status_xmp);
    }

    if (likely(__app_status_xmp)) {
        xm_mempool_fini(__app_status_xmp);
    }

    if (likely(__app_assoc_process_xmp)) {
        xm_mempool_fini(__app_assoc_process_xmp);
    }
}