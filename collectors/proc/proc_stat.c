/*
 * @Author: CALM.WU
 * @Date: 2022-01-10 11:31:16
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-01-13 17:24:03
 */

// https://www.jianshu.com/p/aea52895de5e
// https://supportcenter.checkpoint.com/supportcenter/portal?eventSubmit_doGoviewsolutiondetails=&solutionid=sk65143

#include "plugin_proc.h"

#include "prometheus-client-c/prom.h"

#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/resource.h"
#include "utils/strings.h"

#include "appconfig/appconfig.h"

static const char *      __proc_stat_filename = "/proc/stat";
static struct proc_file *__pf_stat            = NULL;

static prom_gauge_t *__metric_processes_running = NULL, *__metric_processes_blocked = NULL,
                    *__metric_interrupts_from_boot       = NULL,
                    *__metric_context_switches_from_boot = NULL,
                    *__metric_processes_from_boot        = NULL;

static const char *__metric_help_cpu_user_jiffies    = "time spent in user mode";
static const char *__metric_help_cpu_nice_jiffies    = "time spent in user mode with low priority";
static const char *__metric_help_cpu_system_jiffies  = "time spent in system mode";
static const char *__metric_help_cpu_idle_jiffies    = "time spent in idle mode";
static const char *__metric_help_cpu_iowait_jiffies  = "time spent waiting for IO to complete";
static const char *__metric_help_cpu_irq_jiffies     = "time spent servicing interrupts";
static const char *__metric_help_cpu_softirq_jiffies = "time spent servicing softirqs";
static const char *__metric_help_cpu_steal_jiffies   = "spent executing other virtual hosts";
static const char *__metric_help_cpu_guest_jiffies =
    "time spent running a virtual CPU for guest OS";
static const char *__metric_help_cpu_guest_nice_jiffies = "time spent running a niced guest";

struct cpu_utilization_metric {
    prom_gauge_t *metric_user_jiffies;
    char          metric_user_jiffies_name[PROM_METRIC_NAME_LEN];

    prom_gauge_t *metric_nice_jiffies;
    char          metric_nice_jiffies_name[PROM_METRIC_NAME_LEN];

    prom_gauge_t *metric_system_jiffies;
    char          metric_system_jiffies_name[PROM_METRIC_NAME_LEN];

    prom_gauge_t *metric_idle_jiffies;
    char          metric_idle_jiffies_name[PROM_METRIC_NAME_LEN];

    prom_gauge_t *metric_iowait_jiffies;
    char          metric_iowait_jiffies_name[PROM_METRIC_NAME_LEN];

    prom_gauge_t *metric_irq_jiffies;
    char          metric_irq_jiffies_name[PROM_METRIC_NAME_LEN];

    prom_gauge_t *metric_softirq_jiffies;
    char          metric_softirq_jiffies_name[PROM_METRIC_NAME_LEN];

    prom_gauge_t *metric_steal_jiffies;
    char          metric_steal_jiffies_name[PROM_METRIC_NAME_LEN];

    prom_gauge_t *metric_guest_jiffies;
    char          metric_guest_jiffies_name[PROM_METRIC_NAME_LEN];

    prom_gauge_t *metric_guest_nice_jiffies;
    char          metric_guest_nice_jiffies_name[PROM_METRIC_NAME_LEN];

    char label_cpu_val[PROM_METRIC_LABEL_VALUE_LEN];
};

static struct cpu_utilization_metric *__cpu_utilization_metrics = NULL;

int32_t init_collector_proc_cpustat() {
    int32_t ret = 0;

    // 设置prom指标
    if (unlikely(!__metric_interrupts_from_boot)) {
        __metric_interrupts_from_boot = prom_collector_registry_must_register_metric(prom_gauge_new(
            "interrupts", "CPU Interrupts", 2, (const char *[]){ "host", "cpustat" }));
    }

    if (unlikely(!__metric_context_switches_from_boot)) {
        __metric_context_switches_from_boot = prom_collector_registry_must_register_metric(
            prom_gauge_new("context_switches", "CPU Context Switches", 2,
                           (const char *[]){ "host", "cpustat" }));
    }

    if (unlikely(!__metric_processes_from_boot)) {
        __metric_processes_from_boot = prom_collector_registry_must_register_metric(prom_gauge_new(
            "processes", "Started Processes", 2, (const char *[]){ "host", "cpustat" }));
    }

    if (unlikely(!__metric_processes_running)) {
        __metric_processes_running = prom_collector_registry_must_register_metric(prom_gauge_new(
            "procs_running", "System Processes", 2, (const char *[]){ "host", "cpustat" }));
    }

    if (unlikely(!__metric_processes_blocked)) {
        __metric_processes_blocked = prom_collector_registry_must_register_metric(prom_gauge_new(
            "procs_blocked", "System Processes", 2, (const char *[]){ "host", "cpustat" }));
    }

    // 动态构建cpu.utilization指标
    // https://man7.org/linux/man-pages/man5/proc.5.html

    int32_t cpu_cores = get_system_cpus() + 1;
    if (unlikely(!__cpu_utilization_metrics)) {

        __cpu_utilization_metrics = (struct cpu_utilization_metric *)calloc(
            cpu_cores, sizeof(struct cpu_utilization_metric));

        for (int32_t index = 0; index < cpu_cores; index++) {
            struct cpu_utilization_metric *cum = &__cpu_utilization_metrics[index];

            if (0 == index) {
                ret =
                    snprintf(cum->label_cpu_val, PROM_METRIC_LABEL_VALUE_LEN - 1, "%s", "cpu.all");
                cum->label_cpu_val[ret] = '\0';

                cum->metric_user_jiffies = prom_collector_registry_must_register_metric(
                    prom_gauge_new("total_cpu_user_jiffies", __metric_help_cpu_user_jiffies, 2,
                                   (const char *[]){ "host", "cpustat" }));

                cum->metric_nice_jiffies = prom_collector_registry_must_register_metric(
                    prom_gauge_new("total_cpu_nice_jiffies", __metric_help_cpu_nice_jiffies, 2,
                                   (const char *[]){ "host", "cpustat" }));

                cum->metric_system_jiffies = prom_collector_registry_must_register_metric(
                    prom_gauge_new("total_cpu_system_jiffies", __metric_help_cpu_system_jiffies, 2,
                                   (const char *[]){ "host", "cpustat" }));

                cum->metric_idle_jiffies = prom_collector_registry_must_register_metric(
                    prom_gauge_new("total_cpu_idle_jiffies", __metric_help_cpu_idle_jiffies, 2,
                                   (const char *[]){ "host", "cpustat" }));

                cum->metric_iowait_jiffies = prom_collector_registry_must_register_metric(
                    prom_gauge_new("total_cpu_iowait_jiffies", __metric_help_cpu_iowait_jiffies, 2,
                                   (const char *[]){ "host", "cpustat" }));

                cum->metric_irq_jiffies = prom_collector_registry_must_register_metric(
                    prom_gauge_new("total_cpu_irq_jiffies", __metric_help_cpu_irq_jiffies, 2,
                                   (const char *[]){ "host", "cpustat" }));

                cum->metric_softirq_jiffies = prom_collector_registry_must_register_metric(
                    prom_gauge_new("total_cpu_softirq_jiffies", __metric_help_cpu_softirq_jiffies,
                                   2, (const char *[]){ "host", "cpustat" }));

                cum->metric_steal_jiffies = prom_collector_registry_must_register_metric(
                    prom_gauge_new("total_cpu_steal_jiffies", __metric_help_cpu_steal_jiffies, 2,
                                   (const char *[]){ "host", "cpustat" }));

                cum->metric_guest_jiffies = prom_collector_registry_must_register_metric(
                    prom_gauge_new("total_cpu_guest_jiffies", __metric_help_cpu_guest_jiffies, 2,
                                   (const char *[]){ "host", "cpustat" }));

                cum->metric_guest_nice_jiffies =
                    prom_collector_registry_must_register_metric(prom_gauge_new(
                        "total_cpu_guest_nice_jiffies", __metric_help_cpu_guest_nice_jiffies, 2,
                        (const char *[]){ "host", "cpustat" }));
            } else {
                int32_t core_id = index - 1;
                ret = snprintf(cum->label_cpu_val, PROM_METRIC_LABEL_VALUE_LEN - 1, "cpu.core%d",
                               core_id);
                cum->label_cpu_val[ret] = '\0';

                snprintf(cum->metric_user_jiffies_name, PROM_METRIC_NAME_LEN - 1,
                         "cpu_core%d_user_jiffies", core_id);
                cum->metric_user_jiffies = prom_collector_registry_must_register_metric(
                    prom_gauge_new(cum->metric_user_jiffies_name, __metric_help_cpu_user_jiffies, 2,
                                   (const char *[]){ "host", "cpustat" }));
                debug("cpu.core:%d: '%s'", core_id, cum->metric_user_jiffies_name);

                //--------------------------------------------------------------------------------
                snprintf(cum->metric_nice_jiffies_name, PROM_METRIC_NAME_LEN - 1,
                         "cpu_core%d_nice_jiffies", core_id);
                cum->metric_nice_jiffies = prom_collector_registry_must_register_metric(
                    prom_gauge_new(cum->metric_nice_jiffies_name, __metric_help_cpu_nice_jiffies, 2,
                                   (const char *[]){ "host", "cpustat" }));
                debug("cpu.core:%d: '%s'", core_id, cum->metric_nice_jiffies_name);

                //--------------------------------------------------------------------------------

                snprintf(cum->metric_system_jiffies_name, PROM_METRIC_NAME_LEN - 1,
                         "cpu_core%d_system_jiffies", core_id);
                cum->metric_system_jiffies =
                    prom_collector_registry_must_register_metric(prom_gauge_new(
                        cum->metric_system_jiffies_name, __metric_help_cpu_system_jiffies, 2,
                        (const char *[]){ "host", "cpustat" }));
                debug("cpu.core:%d: '%s'", core_id, cum->metric_system_jiffies_name);

                //--------------------------------------------------------------------------------

                snprintf(cum->metric_idle_jiffies_name, PROM_METRIC_NAME_LEN - 1,
                         "cpu_core%d_idle_jiffies", core_id);
                cum->metric_idle_jiffies = prom_collector_registry_must_register_metric(
                    prom_gauge_new(cum->metric_idle_jiffies_name, __metric_help_cpu_idle_jiffies, 2,
                                   (const char *[]){ "host", "cpustat" }));
                debug("cpu.core:%d: '%s'", core_id, cum->metric_idle_jiffies_name);

                //--------------------------------------------------------------------------------

                snprintf(cum->metric_iowait_jiffies_name, PROM_METRIC_NAME_LEN - 1,
                         "cpu_core%d_iowait_jiffies", core_id);
                cum->metric_iowait_jiffies =
                    prom_collector_registry_must_register_metric(prom_gauge_new(
                        cum->metric_iowait_jiffies_name, __metric_help_cpu_iowait_jiffies, 2,
                        (const char *[]){ "host", "cpustat" }));
                debug("cpu.core:%d: '%s'", core_id, cum->metric_idle_jiffies_name);

                //--------------------------------------------------------------------------------

                snprintf(cum->metric_irq_jiffies_name, PROM_METRIC_NAME_LEN - 1,
                         "cpu_core%d_irq_jiffies", core_id);
                cum->metric_irq_jiffies = prom_collector_registry_must_register_metric(
                    prom_gauge_new(cum->metric_irq_jiffies_name, __metric_help_cpu_irq_jiffies, 2,
                                   (const char *[]){ "host", "cpustat" }));
                debug("cpu.core:%d: '%s'", core_id, cum->metric_irq_jiffies_name);

                //--------------------------------------------------------------------------------

                snprintf(cum->metric_softirq_jiffies_name, PROM_METRIC_NAME_LEN - 1,
                         "cpu_core%d_softirq_jiffies", core_id);
                cum->metric_softirq_jiffies =
                    prom_collector_registry_must_register_metric(prom_gauge_new(
                        cum->metric_softirq_jiffies_name, __metric_help_cpu_softirq_jiffies, 2,
                        (const char *[]){ "host", "cpustat" }));
                debug("cpu.core:%d: '%s'", core_id, cum->metric_softirq_jiffies_name);

                //--------------------------------------------------------------------------------

                snprintf(cum->metric_steal_jiffies_name, PROM_METRIC_NAME_LEN - 1,
                         "cpu_core%d_steal_jiffies", core_id);
                cum->metric_steal_jiffies = prom_collector_registry_must_register_metric(
                    prom_gauge_new(cum->metric_steal_jiffies_name, __metric_help_cpu_steal_jiffies,
                                   2, (const char *[]){ "host", "cpustat" }));
                debug("cpu.core:%d: '%s'", core_id, cum->metric_steal_jiffies_name);

                //--------------------------------------------------------------------------------

                snprintf(cum->metric_guest_jiffies_name, PROM_METRIC_NAME_LEN - 1,
                         "cpu_core%d_guest_jiffies", core_id);
                cum->metric_guest_jiffies = prom_collector_registry_must_register_metric(
                    prom_gauge_new(cum->metric_guest_jiffies_name, __metric_help_cpu_guest_jiffies,
                                   2, (const char *[]){ "host", "cpustat" }));
                debug("cpu.core:%d: '%s'", core_id, cum->metric_guest_jiffies_name);

                //--------------------------------------------------------------------------------

                snprintf(cum->metric_guest_nice_jiffies_name, PROM_METRIC_NAME_LEN - 1,
                         "cpu_core%d_guest_nice_jiffies", core_id);
                cum->metric_guest_nice_jiffies =
                    prom_collector_registry_must_register_metric(prom_gauge_new(
                        cum->metric_guest_nice_jiffies_name, __metric_help_cpu_guest_nice_jiffies,
                        2, (const char *[]){ "host", "cpustat" }));
                debug("cpu.core:%d: '%s'", core_id, cum->metric_guest_nice_jiffies_name);
            }
        }
    }
    debug("[PLUGIN_PROC:proc_stat] init successed");
    return 0;
}

static void do_cpu_utilization(size_t line, int32_t core_index) {
    // debug("[PLUGIN_PROC:proc_stat] /proc/stat line: %lu, core_index: %d", line, core_index);

    // sysconf(_SC_CLK_TCK)一般地定义为jiffies(一般地等于10ms)
    // CPU时间 = user + system + nice + idle + iowait + irq + softirq
    int32_t ret = 0;

    uint64_t user_jiffies,  // 用户态时间
        nice_jiffies,       // nice用户态时间
        system_jiffies,     // 系统态时间
        idle_jiffies,       // 空闲时间, 不包含IO等待时间
        io_wait_jiffies,    // IO等待时间
        irq_jiffies,        // 硬中断时间
        soft_irq_jiffies,   // 软中断时间
        steal_jiffies,  // 虚拟化环境中运行其他操作系统上花费的时间（since Linux 2.6.11）
        guest_jiffies,       // 操作系统运行虚拟CPU花费的时间（since Linux 2.6.24）
        guest_nice_jiffies;  // 运行一个带nice值的guest花费的时间（since Linux 2.6.24）

    user_jiffies       = str2uint64_t(procfile_lineword(__pf_stat, line, 1));
    nice_jiffies       = str2uint64_t(procfile_lineword(__pf_stat, line, 2));
    system_jiffies     = str2uint64_t(procfile_lineword(__pf_stat, line, 3));
    idle_jiffies       = str2uint64_t(procfile_lineword(__pf_stat, line, 4));
    io_wait_jiffies    = str2uint64_t(procfile_lineword(__pf_stat, line, 5));
    irq_jiffies        = str2uint64_t(procfile_lineword(__pf_stat, line, 6));
    soft_irq_jiffies   = str2uint64_t(procfile_lineword(__pf_stat, line, 7));
    steal_jiffies      = str2uint64_t(procfile_lineword(__pf_stat, line, 8));
    guest_jiffies      = str2uint64_t(procfile_lineword(__pf_stat, line, 9));
    guest_nice_jiffies = str2uint64_t(procfile_lineword(__pf_stat, line, 10));

    user_jiffies -= guest_jiffies;
    nice_jiffies -= guest_nice_jiffies;

    // 设置指标
    int32_t cpu_metric_pos = core_index + 1;

    struct cpu_utilization_metric *cum = &__cpu_utilization_metrics[cpu_metric_pos];

    prom_gauge_set(cum->metric_user_jiffies, (double)user_jiffies,
                   (const char *[]){ premetheus_instance_label, cum->label_cpu_val });
    prom_gauge_set(cum->metric_nice_jiffies, (double)nice_jiffies,
                   (const char *[]){ premetheus_instance_label, cum->label_cpu_val });
    prom_gauge_set(cum->metric_system_jiffies, (double)system_jiffies,
                   (const char *[]){ premetheus_instance_label, cum->label_cpu_val });
    prom_gauge_set(cum->metric_idle_jiffies, (double)idle_jiffies,
                   (const char *[]){ premetheus_instance_label, cum->label_cpu_val });
    prom_gauge_set(cum->metric_iowait_jiffies, (double)io_wait_jiffies,
                   (const char *[]){ premetheus_instance_label, cum->label_cpu_val });
    prom_gauge_set(cum->metric_irq_jiffies, (double)irq_jiffies,
                   (const char *[]){ premetheus_instance_label, cum->label_cpu_val });
    prom_gauge_set(cum->metric_softirq_jiffies, (double)soft_irq_jiffies,
                   (const char *[]){ premetheus_instance_label, cum->label_cpu_val });
    prom_gauge_set(cum->metric_steal_jiffies, (double)steal_jiffies,
                   (const char *[]){ premetheus_instance_label, cum->label_cpu_val });
    prom_gauge_set(cum->metric_guest_jiffies, (double)guest_jiffies,
                   (const char *[]){ premetheus_instance_label, cum->label_cpu_val });
    prom_gauge_set(cum->metric_guest_nice_jiffies, (double)guest_nice_jiffies,
                   (const char *[]){ premetheus_instance_label, cum->label_cpu_val });

    debug("[PLUGIN_PROC:proc_stat] core_index: %d user_jiffies: %lu, nice_jiffies: %lu, "
          "system_jiffies: %lu, "
          "idle_jiffies: %lu, io_wait_jiffies: %lu, irq_jiffies: %lu, soft_irq_jiffies: %lu, "
          "steal_jiffies: %lu, guest_jiffies: %lu, guest_nice_jiffies: %lu",
          core_index, user_jiffies, nice_jiffies, system_jiffies, idle_jiffies, io_wait_jiffies,
          irq_jiffies, soft_irq_jiffies, steal_jiffies, guest_jiffies, guest_nice_jiffies);
}

int32_t collector_proc_cpustat(int32_t update_every, usec_t dt, const char *config_path) {
    debug("[PLUGIN_PROC:proc_stat] config:%s running", config_path);

    const char *f_stat =
        appconfig_get_member_str(config_path, "monitor_file", __proc_stat_filename);

    if (unlikely(!__pf_stat)) {
        __pf_stat = procfile_open(f_stat, " \t:", PROCFILE_FLAG_DEFAULT);
        if (unlikely(!__pf_stat)) {
            error("Cannot open %s", f_stat);
            return -1;
        }
    }

    __pf_stat = procfile_readall(__pf_stat);
    if (unlikely(!__pf_stat)) {
        error("Cannot read %s", f_stat);
        return -1;
    }

    size_t lines      = procfile_lines(__pf_stat);
    size_t line_words = 0;

    uint64_t interrupts_from_boot       = 0;
    uint64_t context_switches_from_boot = 0;
    uint64_t processes_from_boot        = 0;
    uint64_t processes_running          = 0;
    uint64_t processes_blocked          = 0;

    debug("[PLUGIN_PROC:proc_stat] lines: %lu", lines);

    for (size_t index = 0; index < lines; index++) {
        char *row_name = procfile_lineword(__pf_stat, index, 0);

        if (likely(row_name[0] == 'c' && row_name[1] == 'p' && row_name[2] == 'u')) {
            line_words = procfile_linewords(__pf_stat, index);

            if (unlikely(line_words < 10)) {
                error("Cannot read /proc/stat cpu line. Expected 10 params, read %lu.", line_words);
                continue;
            }

            bool    is_core    = (row_name[3] != '\0');
            int32_t core_index = (is_core ? (row_name[3] - '0') : -1);
            do_cpu_utilization(index, core_index);

        } else if (unlikely(strncmp(row_name, "intr", 4) == 0)) {
            // “intr”这行给出中断的信息，第一个为自系统启动以来，发生的所有的中断的次数；然后每个数对应一个特定的中断自系统启动以来所发生的次数。
            // The first column is the total of all interrupts serviced; each subsequent column
            // is the total for that particular interrupt
            interrupts_from_boot = str2uint64_t(procfile_lineword(__pf_stat, index, 1));
            debug("[PLUGIN_PROC:proc_stat] interrupts_from_boot: %lu", interrupts_from_boot);

            prom_gauge_set(__metric_interrupts_from_boot, (double)interrupts_from_boot,
                           (const char *[]){ premetheus_instance_label,
                                             "counts of interrupts serviced since boot time" });

        } else if (unlikely(strncmp(row_name, "ctxt", 4) == 0)) {
            // The "ctxt" line gives the total number of context switches across all CPUs.
            context_switches_from_boot = str2uint64_t(procfile_lineword(__pf_stat, index, 1));

            debug("[PLUGIN_PROC:proc_stat] context_switches_from_boot: %lu",
                  context_switches_from_boot);

            prom_gauge_set(__metric_context_switches_from_boot, (double)context_switches_from_boot,
                           (const char *[]){ premetheus_instance_label,
                                             "number of context switches across all CPUs" });

        } else if (unlikely(strncmp(row_name, "processes", 9) == 0)) {
            // The "processes" line gives the number of processes and threads created, which
            // includes (but is not limited to) those created by calls to the fork() and clone()
            // system calls.
            processes_from_boot = str2uint64_t(procfile_lineword(__pf_stat, index, 1));

            debug("[PLUGIN_PROC:proc_stat] processes_from_boot :%lu", processes_from_boot);

            prom_gauge_set(__metric_processes_from_boot, (double)processes_from_boot,
                           (const char *[]){ premetheus_instance_label,
                                             "number of processes and threads created" });

        } else if (unlikely(strncmp(row_name, "procs_running", 13) == 0)) {
            // The "procs_running" line gives the number of processes currently running on CPUs.
            processes_running = str2uint64_t(procfile_lineword(__pf_stat, index, 1));

            debug("[PLUGIN_PROC:proc_stat] processes_running %lu", processes_running);

            prom_gauge_set(__metric_processes_running, (double)processes_running,
                           (const char *[]){ premetheus_instance_label,
                                             "number of processes in runnable state" });

        } else if (unlikely(strncmp(row_name, "procs_blocked", 13) == 0)) {
            // The "procs_blocked" line gives the number of processes currently blocked, waiting
            // for I/O to complete.
            processes_blocked = str2uint64_t(procfile_lineword(__pf_stat, index, 1));

            debug("[PLUGIN_PROC:proc_stat] procs_blocked: %lu", processes_blocked);

            prom_gauge_set(
                __metric_processes_blocked, (double)processes_blocked,
                (const char *[]){
                    premetheus_instance_label,
                    "number of processes currently blocked, waiting for I/O to complete" });
        }
    }

    return 0;
}

void fini_collector_proc_cpustat() {
    if (likely(__pf_stat)) {
        procfile_close(__pf_stat);
        __pf_stat = NULL;
    }

    if (likely(__cpu_utilization_metrics)) {
        free(__cpu_utilization_metrics);
        __cpu_utilization_metrics = NULL;
    }

    // if (likely(__metric_context_switches_from_boot)) {
    //     prom_gauge_destroy(__metric_context_switches_from_boot);
    //     __metric_context_switches_from_boot = NULL;
    // }

    // if (likely(__metric_interrupts_from_boot)) {
    //     prom_gauge_destroy(__metric_interrupts_from_boot);
    //     __metric_interrupts_from_boot = NULL;
    // }

    // if (likely(__metric_processes_from_boot)) {
    //     prom_gauge_destroy(__metric_processes_from_boot);
    //     __metric_processes_from_boot = NULL;
    // }

    // if (likely(__metric_processes_running)) {
    //     prom_gauge_destroy(__metric_processes_running);
    //     __metric_processes_running = NULL;
    // }

    // if (likely(__metric_processes_blocked)) {
    //     prom_gauge_destroy(__metric_processes_blocked);
    //     __metric_processes_blocked = NULL;
    // }

    debug("[PLUGIN_PROC:proc_stat] stopped");
}
