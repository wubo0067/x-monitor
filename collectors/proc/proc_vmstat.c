/*
 * @Author: CALM.WU
 * @Date: 2022-01-26 17:00:34
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-03-23 16:24:44
 */

// 取得虚拟内存统计信息（相关文件/proc/vmstat）

#include "plugin_proc.h"

#include "prometheus-client-c/prom.h"

#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/strings.h"
#include "utils/adaptive_resortable_list.h"

#include "app_config/app_config.h"

// https://billtian.github.io/digoal.blog/2016/08/06/03.html
// https://www.sunliaodong.cn/2021/02/05/Linux-proc-vmstat%E8%AF%A6%E8%A7%A3/

static const char *__def_proc_vmstat_filename = "/proc/vmstat";
static const char *__cfg_proc_vmstat_filename = NULL;
static struct proc_file *__pf_vmstat = NULL;
static ARL_BASE *__arl_vmstat = NULL;

static uint64_t
    // minor faults since last boot 一级页面和二级页面的错误数
    __pgfault = 0,
    // major faults since last boot 一级页面的错误数 (
    __pgmajfault = 0,
    // page ins since last boot 数据从硬盘读到物理内存
    __pgpgin = 0,
    // page outs since last boot 数据从物理内存写到硬盘
    __pgpgout = 0,
    // swap ins since last boot 数据从磁盘交换区装入内存
    __pswpin = 0,
    // swap outs since last boot 数据从内存转储到磁盘交换区的速率 (5 分钟内)
    // *should never be positive. This means the kernel is having to write
    // memory pages to disk to *free up memory for some other process or disk
    // cache. One may see occasional swapping on the *machine due to the kernel
    // swapping out a process page in favor of a disk cache page due to *the
    // swappiness factor set
    // *平均每秒把数据从磁盘交换区装到内存的数据量读取/proc/vmstat 文件得出最近
    // 240 秒内 pswpin 的增量， *把 pswpin 的增量再除以 240
    // 得到每秒的平均增量平均每秒把数据从内存装到磁盘交换区的数据量读取/proc/vmstat
    // 文件得出最近 240 秒内 pswpout 的增量， *把 pswpout 的增量再除以 240
    // 得到每秒的平均增量
    __pswpout = 0,
    // dirty pages waiting to be written to disk 脏页数
    // signifies amount of memory waiting to be written to disk. If you have a
    // power loss you can expect to lose this much data, unless your application
    // has some form of journaling (eg Transaction logs)
    __nr_dirty = 0,
    // dirty pages currently being written to disk 回写页数
    __nr_writeback = 0,
    //
    __oom_kill = 0,
    //
    __numa_hit = 0,
    //
    __numa_miss = 0,
    //
    __numa_foreign = 0,
    // 交叉分配使用的内存中使用本节点的内存次数
    __numa_interleave = 0,
    // 在本节点运行的程序使用本节点内存次数
    __numa_local = 0,
    // 在其他节点运行的程序使用本节点内存次数
    __numa_other = 0;

static prom_gauge_t *__metric_pgfault = NULL, *__metric_pgmajfault = NULL,
                    *__metric_pgpgin = NULL, *__metric_pgpgout = NULL,
                    *__metric_pswpin = NULL, *__metric_pswpout = NULL,
                    *__metric_nr_dirty = NULL, *__metric_nr_writeback = NULL,
                    *__metric_oom_kill = NULL, *__metric_numa_foreign = NULL,
                    *__metric_numa_hit = NULL, *__metric_numa_miss = NULL,
                    *__metric_numa_interleave = NULL,
                    *__metric_numa_local = NULL, *__metric_numa_other = NULL;

int32_t init_collector_proc_vmstat() {
    __arl_vmstat = arl_create("proc_vmstat", NULL, 3);
    if (unlikely(NULL == __arl_vmstat)) {
        return -1;
    }

    arl_expect(__arl_vmstat, "pgfault", &__pgfault);
    arl_expect(__arl_vmstat, "pgmajfault", &__pgmajfault);
    arl_expect(__arl_vmstat, "pgpgin", &__pgpgin);
    arl_expect(__arl_vmstat, "pgpgout", &__pgpgout);
    arl_expect(__arl_vmstat, "pswpin", &__pswpin);
    arl_expect(__arl_vmstat, "pswpout", &__pswpout);
    arl_expect(__arl_vmstat, "nr_dirty", &__nr_dirty);
    arl_expect(__arl_vmstat, "nr_writeback", &__nr_writeback);
    arl_expect(__arl_vmstat, "oom_kill", &__oom_kill);
    arl_expect(__arl_vmstat, "numa_foreign", &__numa_foreign);
    arl_expect(__arl_vmstat, "numa_interleave", &__numa_interleave);
    arl_expect(__arl_vmstat, "numa_local", &__numa_local);
    arl_expect(__arl_vmstat, "numa_other", &__numa_other);
    arl_expect(__arl_vmstat, "numa_hit", &__numa_hit);
    arl_expect(__arl_vmstat, "numa_miss", &__numa_miss);

    // 初始化指标
    __metric_pgfault = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_vmstat_pgfaults",
                       "Number of page faults the system has made since last "
                       "boot (minor + major).",
                       1, (const char *[]){ "vmstat" }));
    __metric_pgmajfault = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_vmstat_pgmajfaults",
                       "Number of major faults since last boot the system "
                       "required loading a "
                       "memory page from disk.",
                       1, (const char *[]){ "vmstat" }));

    __metric_pgpgin = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_vmstat_pgpgin",
                       "Number of pages that are read from disk since last "
                       "boot",
                       1, (const char *[]){ "vmstat" }));
    __metric_pgpgout = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_vmstat_pgpgout",
                       "Number of pages that are write to disk since last boot",
                       1, (const char *[]){ "vmstat" }));

    __metric_pswpin = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_vmstat_pswpin",
                       "Number of pages the system has swapped in from disk", 1,
                       (const char *[]){ "vmstat" }));
    __metric_pswpout = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_vmstat_pswpout",
                       "Number of pages the system has swapped out to disk", 1,
                       (const char *[]){ "vmstat" }));

    __metric_nr_dirty = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_vmstat_nr_dirty",
                       "dirty pages waiting to be written to disk", 1,
                       (const char *[]){ "vmstat" }));
    __metric_nr_writeback = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_vmstat_nr_writeback",
                       "dirty pages currently being written to disk", 1,
                       (const char *[]){ "vmstat" }));

    __metric_oom_kill = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_vmstat_oom_kill", "Out of Memory Kills count", 1,
                       (const char *[]){ "vmstat" }));

    __metric_numa_foreign = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_vmstat_numa_foreign",
                       "A process wanted to allocate on this node, but ended "
                       "up with memory from another node.",
                       1, (const char *[]){ "vmstat" }));

    __metric_numa_interleave = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_vmstat_numa_interleave",
                       "The number of interleave policy pages successfully "
                       "allocated to this node.",
                       1, (const char *[]){ "vmstat" }));
    __metric_numa_local = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_vmstat_numa_local",
                       "The number of pages successfully allocated on this "
                       "node by a process on this node.",
                       1, (const char *[]){ "vmstat" }));
    __metric_numa_other = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_vmstat_numa_other",
                       "The number of pages allocated on this node by a "
                       "process on another node.",
                       1, (const char *[]){ "vmstat" }));
    __metric_numa_miss = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_vmstat_numa_miss",
                       "The number of pages that were allocated on this node "
                       "because of low memory "
                       "on the intended node.",
                       1, (const char *[]){ "vmstat" }));
    __metric_numa_hit = prom_collector_registry_must_register_metric(
        prom_gauge_new("node_vmstat_numa_hit",
                       "The number of pages that were successfully allocated "
                       "to this node.",
                       1, (const char *[]){ "vmstat" }));

    debug("[PLUGIN_PROC:proc_vmstat] init successed");
    return 0;
}

int32_t collector_proc_vmstat(int32_t UNUSED(update_every), usec_t UNUSED(dt),
                              const char *config_path) {
    debug("[PLUGIN_PROC:proc_vmstat] config:%s running", config_path);

    if (unlikely(!__cfg_proc_vmstat_filename)) {
        __cfg_proc_vmstat_filename = appconfig_get_member_str(
            config_path, "monitor_file", __def_proc_vmstat_filename);
    }

    if (unlikely(!__pf_vmstat)) {
        __pf_vmstat = procfile_open(__cfg_proc_vmstat_filename,
                                    " \t:", PROCFILE_FLAG_DEFAULT);
        if (unlikely(!__pf_vmstat)) {
            error("[PLUGIN_PROC:proc_vmstat] Cannot open %s",
                  __cfg_proc_vmstat_filename);
            return -1;
        }
        debug("[PLUGIN_PROC:proc_vmstat] opened '%s'",
              __cfg_proc_vmstat_filename);
    }

    __pf_vmstat = procfile_readall(__pf_vmstat);
    if (unlikely(!__pf_vmstat)) {
        error("Cannot read %s", __cfg_proc_vmstat_filename);
        return -1;
    }

    size_t lines = procfile_lines(__pf_vmstat);

    // 更新采集数值
    arl_begin(__arl_vmstat);

    for (size_t l = 0; l < lines; l++) {
        size_t words = procfile_linewords(__pf_vmstat, l);
        if (unlikely(words < 2))
            continue;

        const char *vmstat_key = procfile_lineword(__pf_vmstat, l, 0);
        if (unlikely(arl_check(__arl_vmstat, vmstat_key,
                               procfile_lineword(__pf_vmstat, l, 1)))) {
            break;
        }
    }

    // 更新采集数值
    prom_gauge_set(__metric_pgfault, __pgfault, (const char *[]){ "page" });
    prom_gauge_set(__metric_pgmajfault, __pgmajfault,
                   (const char *[]){ "page" });

    prom_gauge_set(__metric_pgpgin, __pgpgin, (const char *[]){ "page" });
    prom_gauge_set(__metric_pgpgout, __pgpgout, (const char *[]){ "page" });

    //__pswpin = __pswpin / sysconf(_SC_PAGESIZE) * 1024,
    prom_gauge_set(__metric_pswpin, __pswpin, (const char *[]){ "page" });
    //__pswpout = __pswpout / sysconf(_SC_PAGESIZE) * 1024,
    prom_gauge_set(__metric_pswpout, __pswpout, (const char *[]){ "page" });

    prom_gauge_set(__metric_nr_dirty, __nr_dirty, (const char *[]){ "page" });
    prom_gauge_set(__metric_nr_writeback, __nr_writeback,
                   (const char *[]){ "page" });

    prom_gauge_set(__metric_oom_kill, __oom_kill,
                   (const char *[]){ "oom_kill" });

    debug("[PLUGIN_PROC:proc_vmstat] pgfault:%lu, pgmajfault:%lu, pgpgin:%lu, "
          "pgpgout:%lu, "
          "pswpin:%lu, pswpout:%lu, nr_dirty:%lu, nr_writeback:%lu, "
          "oom_kill:%lu",
          __pgfault, __pgmajfault, __pgpgin, __pgpgout, __pswpin, __pswpout,
          __nr_dirty, __nr_writeback, __oom_kill);

    prom_gauge_set(__metric_numa_foreign, __numa_foreign,
                   (const char *[]){ "numa" });
    prom_gauge_set(__metric_numa_hit, __numa_hit, (const char *[]){ "numa" });
    prom_gauge_set(__metric_numa_miss, __numa_miss, (const char *[]){ "numa" });
    prom_gauge_set(__metric_numa_interleave, __numa_interleave,
                   (const char *[]){ "numa" });
    prom_gauge_set(__metric_numa_local, __numa_local,
                   (const char *[]){ "numa" });
    prom_gauge_set(__metric_numa_other, __numa_other,
                   (const char *[]){ "numa" });

    debug("[PLUGIN_PROC:proc_vmstat] numa_foreign:%lu, num_hit:%lu, "
          "numa_miss:%lu, "
          "numa_interleave:%lu, numa_local:%lu, numa_other:%lu",
          __numa_foreign, __numa_hit, __numa_miss, __numa_interleave,
          __numa_local, __numa_other);

    return 0;
}

void fini_collector_proc_vmstat() {
    if (likely(__arl_vmstat)) {
        arl_free(__arl_vmstat);
        __arl_vmstat = NULL;
    }

    if (likely(__pf_vmstat)) {
        procfile_close(__pf_vmstat);
        __pf_vmstat = NULL;
    }
    debug("[PLUGIN_PROC:proc_vmstat] stopped");
}