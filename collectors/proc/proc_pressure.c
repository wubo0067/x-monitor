/*
 * @Author: CALM.WU
 * @Date: 2022-01-26 17:36:28
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-01-26 17:37:43
 */

// https://xie.infoq.cn/article/931eee27dabb0de906869ba05
// https://blog.dbi-services.com/pressure-stall-information-on-autonomous-linux/
// grubby --update-kernel=/boot/vmlinuz-5.4.17-2136.302.7.2.el8uek.x86_64 --args=psi=1
/*
当 CPU、内存或 IO 设备争夺激烈的时候，系统会出现负载的延迟峰值、吞吐量下降，并可能触发内核的 OOM
Killer。PSI(Pressure Stall Information) 字面意思就是由于资源（CPU、内存和
IO）压力造成的任务执行停顿。PSI
量化了由于硬件资源紧张造成的任务执行中断，统计了系统中任务等待硬件资源的时间。我们可以用 PSI
作为指标，来衡量硬件资源的压力情况。停顿的时间越长，说明资源面临的压力越大。
*/

#include "plugin_proc.h"

#include "prometheus-client-c/prom.h"

#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/strings.h"

#include "appconfig/appconfig.h"

// linux calculates this every 2 seconds, see kernel/sched/psi.c PSI_FREQ
#define MIN_PRESSURE_UPDATE_EVERY 2

static struct proc_file *__pf_psi_cpu = NULL, *__pf_psi_mem = NULL, *__pf_psi_io = NULL;

// 输出的指标
static prom_gauge_t *__metric_psi_cpu_loadavg_10secs  = NULL,
                    *__metric_psi_cpu_loadavg_60secs  = NULL,
                    *__metric_psi_cpu_loadavg_300secs = NULL,
                    *__metric_psi_cpu_total_stall_msecs =
                        NULL;  // total 是任务停顿的总时间，以微秒（microseconds）为单位

// some：是所有任务叠加的。full：是所有任务重合的部分
static prom_gauge_t *__metric_psi_io_some_loadavg_10secs    = NULL,
                    *__metric_psi_io_some_loadavg_60secs    = NULL,
                    *__metric_psi_io_some_loadavg_300secs   = NULL,
                    *__metric_psi_io_some_total_stall_msecs = NULL,
                    *__metric_psi_io_full_loadavg_10secs    = NULL,
                    *__metric_psi_io_full_loadavg_60secs    = NULL,
                    *__metric_psi_io_full_loadavg_300secs   = NULL,
                    *__metric_psi_io_full_total_stall_msecs = NULL;

static prom_gauge_t *__metric_psi_mem_some_loadavg_10secs    = NULL,
                    *__metric_psi_mem_some_loadavg_60secs    = NULL,
                    *__metric_psi_mem_some_loadavg_300secs   = NULL,
                    *__metric_psi_mem_some_total_stall_msecs = NULL,
                    *__metric_psi_mem_full_loadavg_10secs    = NULL,
                    *__metric_psi_mem_full_loadavg_60secs    = NULL,
                    *__metric_psi_mem_full_loadavg_300secs   = NULL,
                    *__metric_psi_mem_full_total_stall_msecs = NULL;

static usec_t __next_pressure_dt = 0;

int32_t init_collector_proc_pressure() {
    __metric_psi_cpu_loadavg_10secs = prom_collector_registry_must_register_metric(
        prom_gauge_new("psi_cpu_loadavg_10secs", "System psi cpu loadavg 10secs", 2,
                       (const char *[]){ "host", "psi" }));

    __metric_psi_cpu_loadavg_60secs = prom_collector_registry_must_register_metric(
        prom_gauge_new("psi_cpu_loadavg_60secs", "System psi cpu loadavg 60secs", 2,
                       (const char *[]){ "host", "psi" }));

    __metric_psi_cpu_loadavg_300secs = prom_collector_registry_must_register_metric(
        prom_gauge_new("psi_cpu_loadavg_300secs", "System psi cpu loadavg 300secs", 2,
                       (const char *[]){ "host", "psi" }));

    __metric_psi_cpu_total_stall_msecs = prom_collector_registry_must_register_metric(
        prom_gauge_new("psi_cpu_total_stall_msecs", "System psi cpu total absolute stall time", 2,
                       (const char *[]){ "host", "psi" }));

    __metric_psi_io_some_loadavg_10secs = prom_collector_registry_must_register_metric(
        prom_gauge_new("psi_io_some_loadavg_10secs", "System psi io some loadavg 10secs", 2,
                       (const char *[]){ "host", "psi" }));

    __metric_psi_io_some_loadavg_60secs = prom_collector_registry_must_register_metric(
        prom_gauge_new("psi_io_some_loadavg_60secs", "System psi io some loadavg 60secs", 2,
                       (const char *[]){ "host", "psi" }));

    __metric_psi_io_some_loadavg_300secs = prom_collector_registry_must_register_metric(
        prom_gauge_new("psi_io_some_loadavg_300secs", "System psi io some loadavg 300secs", 2,
                       (const char *[]){ "host", "psi" }));

    __metric_psi_io_some_total_stall_msecs = prom_collector_registry_must_register_metric(
        prom_gauge_new("psi_io_some_total_stall_msecs", "System psi io some total stall time", 2,
                       (const char *[]){ "host", "psi" }));

    __metric_psi_io_full_loadavg_10secs = prom_collector_registry_must_register_metric(
        prom_gauge_new("psi_io_full_loadavg_10secs", "System psi io full loadavg 10secs", 2,
                       (const char *[]){ "host", "psi" }));

    __metric_psi_io_full_loadavg_60secs = prom_collector_registry_must_register_metric(
        prom_gauge_new("psi_io_full_loadavg_60secs", "System psi io full loadavg 60secs", 2,
                       (const char *[]){ "host", "psi" }));

    __metric_psi_io_full_loadavg_300secs = prom_collector_registry_must_register_metric(
        prom_gauge_new("psi_io_full_loadavg_300secs", "System psi io full loadavg 300secs", 2,
                       (const char *[]){ "host", "psi" }));

    __metric_psi_io_full_total_stall_msecs = prom_collector_registry_must_register_metric(
        prom_gauge_new("psi_io_full_total_stall_msecs", "System psi io full total stall time", 2,
                       (const char *[]){ "host", "psi" }));

    __metric_psi_mem_some_loadavg_10secs = prom_collector_registry_must_register_metric(
        prom_gauge_new("psi_mem_some_loadavg_10secs", "System psi mem some loadavg 10secs", 2,
                       (const char *[]){ "host", "psi" }));

    __metric_psi_mem_some_loadavg_60secs = prom_collector_registry_must_register_metric(
        prom_gauge_new("psi_mem_some_loadavg_60secs", "System psi mem some loadavg 60secs", 2,
                       (const char *[]){ "host", "psi" }));

    __metric_psi_mem_some_loadavg_300secs = prom_collector_registry_must_register_metric(
        prom_gauge_new("psi_mem_some_loadavg_300secs", "System psi mem some loadavg 300secs", 2,
                       (const char *[]){ "host", "psi" }));

    __metric_psi_mem_some_total_stall_msecs = prom_collector_registry_must_register_metric(
        prom_gauge_new("psi_mem_some_total_stall_msecs", "System psi mem some total stall time", 2,
                       (const char *[]){ "host", "psi" }));

    __metric_psi_mem_full_loadavg_10secs = prom_collector_registry_must_register_metric(
        prom_gauge_new("psi_mem_full_loadavg_10secs", "System psi mem full loadavg 10secs", 2,
                       (const char *[]){ "host", "psi" }));

    __metric_psi_mem_full_loadavg_60secs = prom_collector_registry_must_register_metric(
        prom_gauge_new("psi_mem_full_loadavg_60secs", "System psi mem full loadavg 60secs", 2,
                       (const char *[]){ "host", "psi" }));

    __metric_psi_mem_full_loadavg_300secs = prom_collector_registry_must_register_metric(
        prom_gauge_new("psi_mem_full_loadavg_300secs", "System psi mem full loadavg 300secs", 2,
                       (const char *[]){ "host", "psi" }));

    __metric_psi_mem_full_total_stall_msecs = prom_collector_registry_must_register_metric(
        prom_gauge_new("psi_mem_full_total_stall_msecs", "System psi mem full total stall time", 2,
                       (const char *[]){ "host", "psi" }));

    debug("[PLUGIN_PROC:proc_pressure] init successed");

    return 0;
}

static void __collector_proc_psi_cpu(const char *config_path) {
    const char *psi_cpu =
        appconfig_get_member_str(config_path, "monitor_cpu_file", "/proc/pressure/cpu");

    if (unlikely(!__pf_psi_cpu)) {
        __pf_psi_cpu = procfile_open(psi_cpu, " =", PROCFILE_FLAG_DEFAULT);
        if (unlikely(!__pf_psi_cpu)) {
            error("[PLUGIN_PROC:proc_pressure] Cannot open %s", psi_cpu);
            return;
        }
    }

    __pf_psi_cpu = procfile_readall(__pf_psi_cpu);
    if (unlikely(!__pf_psi_cpu)) {
        error("[PLUGIN_PROC:proc_pressure] Cannot read %s", psi_cpu);
        return;
    }

    if (unlikely(procfile_lines(__pf_psi_cpu) < 1)) {
        error("[PLUGIN_PROC:proc_pressure] %s has no lines", psi_cpu);
        return;
    }
    static double   psi_cpu_10secs, psi_cpu_60secs, psi_cpu_300secs;
    static uint64_t psi_cpu_total_stall_msecs;

    psi_cpu_10secs            = strtod(procfile_lineword(__pf_psi_cpu, 0, 2), NULL);
    psi_cpu_60secs            = strtod(procfile_lineword(__pf_psi_cpu, 0, 4), NULL);
    psi_cpu_300secs           = strtod(procfile_lineword(__pf_psi_cpu, 0, 6), NULL);
    psi_cpu_total_stall_msecs = str2uint64_t(procfile_lineword(__pf_psi_cpu, 0, 8));

    debug("[PLUGIN_PROC:proc_pressure] psi_cpu_10secs: %.2f, psi_cpu_60secs: %.2f, "
          "psi_cpu_300secs: %.2f, psi_cpu_total_stall_mecs: %lu",
          psi_cpu_10secs, psi_cpu_60secs, psi_cpu_300secs, psi_cpu_total_stall_msecs);

    prom_gauge_set(__metric_psi_cpu_loadavg_10secs, psi_cpu_10secs,
                   (const char *[]){ premetheus_instance_label, "cpu" });

    prom_gauge_set(__metric_psi_cpu_loadavg_60secs, psi_cpu_60secs,
                   (const char *[]){ premetheus_instance_label, "cpu" });

    prom_gauge_set(__metric_psi_cpu_loadavg_300secs, psi_cpu_300secs,
                   (const char *[]){ premetheus_instance_label, "cpu" });

    prom_gauge_set(__metric_psi_cpu_total_stall_msecs, psi_cpu_total_stall_msecs,
                   (const char *[]){ premetheus_instance_label, "cpu" });
}

static void __collector_proc_psi_mem(const char *config_path) {
    const char *psi_memory =
        appconfig_get_member_str(config_path, "monitor_mem_file", "/proc/pressure/memory");

    if (unlikely(!__pf_psi_mem)) {
        __pf_psi_mem = procfile_open(psi_memory, " =", PROCFILE_FLAG_DEFAULT);
        if (unlikely(!__pf_psi_mem)) {
            error("[PLUGIN_PROC:proc_pressure] Cannot open %s", psi_memory);
            return;
        }
    }

    __pf_psi_mem = procfile_readall(__pf_psi_mem);
    if (unlikely(!__pf_psi_mem)) {
        error("[PLUGIN_PROC:proc_pressure] Cannot read %s", psi_memory);
        return;
    }

    if (unlikely(procfile_lines(__pf_psi_mem) < 1)) {
        error("[PLUGIN_PROC:proc_pressure] %s has no lines", psi_memory);
        return;
    }

    static double   psi_mem_some_10secs, psi_mem_some_60secs, psi_mem_some_300secs;
    static uint64_t psi_mem_some_total_stall_msecs;

    // do some
    psi_mem_some_10secs            = strtod(procfile_lineword(__pf_psi_mem, 0, 2), NULL);
    psi_mem_some_60secs            = strtod(procfile_lineword(__pf_psi_mem, 0, 4), NULL);
    psi_mem_some_300secs           = strtod(procfile_lineword(__pf_psi_mem, 0, 6), NULL);
    psi_mem_some_total_stall_msecs = str2uint64_t(procfile_lineword(__pf_psi_mem, 0, 8));

    debug("[PLUGIN_PROC:proc_pressure] psi_mem_some_10secs: %.2f, psi_mem_some_60secs: %.2f, "
          "psi_mem_some_300secs: %.2f, psi_mem_some_total_stall_msecs: %lu",
          psi_mem_some_10secs, psi_mem_some_60secs, psi_mem_some_300secs,
          psi_mem_some_total_stall_msecs);

    prom_gauge_set(__metric_psi_io_some_loadavg_10secs, psi_mem_some_10secs,
                   (const char *[]){ premetheus_instance_label, "mem" });
    prom_gauge_set(__metric_psi_mem_some_loadavg_60secs, psi_mem_some_60secs,
                   (const char *[]){ premetheus_instance_label, "mem" });
    prom_gauge_set(__metric_psi_mem_some_loadavg_300secs, psi_mem_some_300secs,
                   (const char *[]){ premetheus_instance_label, "mem" });
    prom_gauge_set(__metric_psi_mem_some_total_stall_msecs, psi_mem_some_total_stall_msecs,
                   (const char *[]){ premetheus_instance_label, "mem" });

    // do full
    static double   psi_mem_full_10secs, psi_mem_full_60secs, psi_mem_full_300secs;
    static uint64_t psi_mem_full_total_stall_msecs;

    psi_mem_full_10secs            = strtod(procfile_lineword(__pf_psi_mem, 0, 10), NULL);
    psi_mem_full_60secs            = strtod(procfile_lineword(__pf_psi_mem, 0, 12), NULL);
    psi_mem_full_300secs           = strtod(procfile_lineword(__pf_psi_mem, 0, 14), NULL);
    psi_mem_full_total_stall_msecs = str2uint64_t(procfile_lineword(__pf_psi_mem, 0, 16));

    debug("[PLUGIN_PROC:proc_pressure] psi_mem_full_10secs: %.2f, psi_mem_full_60secs: %.2f, "
          "psi_mem_full_300secs: %.2f, psi_mem_full_total_stall_msecs: %lu",
          psi_mem_full_10secs, psi_mem_full_60secs, psi_mem_full_300secs,
          psi_mem_full_total_stall_msecs);

    prom_gauge_set(__metric_psi_mem_full_loadavg_10secs, psi_mem_full_10secs,
                   (const char *[]){ premetheus_instance_label, "mem" });
    prom_gauge_set(__metric_psi_mem_full_loadavg_60secs, psi_mem_full_60secs,
                   (const char *[]){ premetheus_instance_label, "mem" });
    prom_gauge_set(__metric_psi_mem_full_loadavg_300secs, psi_mem_full_300secs,
                   (const char *[]){ premetheus_instance_label, "mem" });
    prom_gauge_set(__metric_psi_mem_full_total_stall_msecs, psi_mem_full_total_stall_msecs,
                   (const char *[]){ premetheus_instance_label, "mem" });
}

static void __collector_proc_psi_io(const char *config_path) {
    const char *psi_io =
        appconfig_get_member_str(config_path, "monitor_io_file", "/proc/pressure/io");

    if (unlikely(!__pf_psi_io)) {
        __pf_psi_io = procfile_open(psi_io, " =", PROCFILE_FLAG_DEFAULT);
        if (unlikely(!__pf_psi_io)) {
            error("[PLUGIN_PROC:proc_pressure] Cannot open %s", psi_io);
            return;
        }
    }

    __pf_psi_io = procfile_readall(__pf_psi_io);
    if (unlikely(!__pf_psi_io)) {
        error("[PLUGIN_PROC:proc_pressure] Cannot read %s", psi_io);
        return;
    }

    if (unlikely(procfile_lines(__pf_psi_io) < 1)) {
        error("[PLUGIN_PROC:proc_pressure] %s has no lines", psi_io);
        return;
    }

    // do some
    static double   psi_io_some_10secs, psi_io_some_60secs, psi_io_some_300secs;
    static uint64_t psi_io_some_total_stall_msecs;

    psi_io_some_10secs            = strtod(procfile_lineword(__pf_psi_io, 0, 2), NULL);
    psi_io_some_60secs            = strtod(procfile_lineword(__pf_psi_io, 0, 4), NULL);
    psi_io_some_300secs           = strtod(procfile_lineword(__pf_psi_io, 0, 6), NULL);
    psi_io_some_total_stall_msecs = str2uint64_t(procfile_lineword(__pf_psi_io, 0, 8));

    debug("[PLUGIN_PROC:proc_pressure] psi_io_some_10secs: %.2f, psi_io_some_60secs: %.2f, "
          "psi_io_some_300secs: %.2f, psi_io_some_total_stall_msecs: %lu",
          psi_io_some_10secs, psi_io_some_60secs, psi_io_some_300secs,
          psi_io_some_total_stall_msecs);

    prom_gauge_set(__metric_psi_io_some_loadavg_10secs, psi_io_some_10secs,
                   (const char *[]){ premetheus_instance_label, "io" });
    prom_gauge_set(__metric_psi_io_some_loadavg_60secs, psi_io_some_60secs,
                   (const char *[]){ premetheus_instance_label, "io" });
    prom_gauge_set(__metric_psi_io_some_loadavg_300secs, psi_io_some_300secs,
                   (const char *[]){ premetheus_instance_label, "io" });
    prom_gauge_set(__metric_psi_io_some_total_stall_msecs, psi_io_some_total_stall_msecs,
                   (const char *[]){ premetheus_instance_label, "io" });

    // do full
    static double   psi_io_full_10secs, psi_io_full_60secs, psi_io_full_300secs;
    static uint64_t psi_io_full_total_stall_msecs;

    psi_io_full_10secs            = strtod(procfile_lineword(__pf_psi_io, 0, 10), NULL);
    psi_io_full_60secs            = strtod(procfile_lineword(__pf_psi_io, 0, 12), NULL);
    psi_io_full_300secs           = strtod(procfile_lineword(__pf_psi_io, 0, 14), NULL);
    psi_io_full_total_stall_msecs = str2uint64_t(procfile_lineword(__pf_psi_io, 0, 16));

    debug("[PLUGIN_PROC:proc_pressure] psi_io_full_10secs: %.2f, psi_io_full_60secs: %.2f, "
          "psi_io_full_300secs: %.2f, psi_io_full_total_stall_msecs: %lu",
          psi_io_full_10secs, psi_io_full_60secs, psi_io_full_300secs,
          psi_io_full_total_stall_msecs);

    prom_gauge_set(__metric_psi_io_full_loadavg_10secs, psi_io_full_10secs,
                   (const char *[]){ premetheus_instance_label, "io" });
    prom_gauge_set(__metric_psi_io_full_loadavg_60secs, psi_io_full_60secs,
                   (const char *[]){ premetheus_instance_label, "io" });
    prom_gauge_set(__metric_psi_io_full_loadavg_300secs, psi_io_full_300secs,
                   (const char *[]){ premetheus_instance_label, "io" });
    prom_gauge_set(__metric_psi_io_full_total_stall_msecs, psi_io_full_total_stall_msecs,
                   (const char *[]){ premetheus_instance_label, "io" });
}

int32_t collector_proc_pressure(int32_t update_every, usec_t dt, const char *config_path) {
    debug("[PLUGIN_PROC:proc_pressure] config:%s running", config_path);

    update_every =
        (update_every < MIN_PRESSURE_UPDATE_EVERY) ? MIN_PRESSURE_UPDATE_EVERY : update_every;

    // /proc/pressure是没两秒更新一次
    if (__next_pressure_dt <= dt) {
        __next_pressure_dt = update_every * USEC_PER_SEC;
    } else {
        __next_pressure_dt -= dt;
        return 0;
    }

    __collector_proc_psi_cpu(config_path);
    __collector_proc_psi_mem(config_path);
    __collector_proc_psi_io(config_path);

    return 0;
}

void fini_collector_proc_pressure() {
    if (likely(__pf_psi_cpu)) {
        procfile_close(__pf_psi_cpu);
        __pf_psi_cpu = NULL;
    }

    if (likely(__pf_psi_mem)) {
        procfile_close(__pf_psi_mem);
        __pf_psi_mem = NULL;
    }

    if (likely(__pf_psi_io)) {
        procfile_close(__pf_psi_io);
        __pf_psi_io = NULL;
    }

    debug("[PLUGIN_PROC:proc_pressure] stopped");
}