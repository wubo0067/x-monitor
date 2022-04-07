/*
 * @Author: CALM.WU
 * @Date: 2022-03-31 10:24:17
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-03-31 10:32:43
 */

#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/log.h"
#include "utils/os.h"
#include "utils/strings.h"
#include "utils/procfile.h"
#include "utils/clocks.h"
#include "collectors/process/process_collector.h"

// 计算进程的cpu占用 https://www.cnblogs.com/songhaibin/p/13885403.html

static int32_t __sig_exit = 0;
// static const int32_t __def_loop_count = 500000000;
static const char       *__proc_stat_filename = "/proc/stat";
static struct proc_file *__pf_stat = NULL;

static void __sig_handler(int sig) {
    __sig_exit = 1;
    debug("SIGINT/SIGTERM received, exiting...");
}

static double __get_total_cpu_time() {
    double user_seconds,    // 用户态时间
        nice_seconds,       // nice用户态时间
        system_seconds,     // 系统态时间
        idle_seconds,       // 空闲时间, 不包含IO等待时间
        io_wait_seconds,    // IO等待时间
        irq_seconds,        // 硬中断时间
        soft_irq_seconds,   // 软中断时间
        steal_seconds,   // 虚拟化环境中运行其他操作系统上花费的时间（since Linux 2.6.11）
        guest_seconds;

    user_seconds = (double)str2uint64_t(procfile_lineword(__pf_stat, 0, 1));
    nice_seconds = (double)str2uint64_t(procfile_lineword(__pf_stat, 0, 2));
    system_seconds = (double)str2uint64_t(procfile_lineword(__pf_stat, 0, 3));
    idle_seconds = (double)str2uint64_t(procfile_lineword(__pf_stat, 0, 4));
    io_wait_seconds = (double)str2uint64_t(procfile_lineword(__pf_stat, 0, 5));
    irq_seconds = (double)str2uint64_t(procfile_lineword(__pf_stat, 0, 6));
    soft_irq_seconds = (double)str2uint64_t(procfile_lineword(__pf_stat, 0, 7));
    steal_seconds = (double)str2uint64_t(procfile_lineword(__pf_stat, 0, 8));
    guest_seconds = (double)str2uint64_t(procfile_lineword(__pf_stat, 0, 9));

    return (user_seconds + nice_seconds + system_seconds + idle_seconds + io_wait_seconds
            + irq_seconds + soft_irq_seconds + steal_seconds + guest_seconds)
           / (double)system_hz;
}

int32_t main(int32_t argc, char **argv) {
    if (log_init("../cli/process_stat_test/log.cfg", "process_stat_test") != 0) {
        fprintf(stderr, "log init failed\n");
        return -1;
    }

    if (unlikely(argc != 2)) {
        fatal("./process_stat_test <pid>\n");
        return -1;
    }

    pid_t pid = str2int32_t(argv[1]);
    if (unlikely(0 == pid)) {
        fatal("./process_stat_test <pid>\n");
        return -1;
    }

    if (unlikely(!__pf_stat)) {
        __pf_stat = procfile_open(__proc_stat_filename, " \t:", PROCFILE_FLAG_DEFAULT);
        if (unlikely(!__pf_stat)) {
            error("Cannot open %s", __proc_stat_filename);
            return -1;
        }
        debug("opened '%s'", __proc_stat_filename);
    }

    __pf_stat = procfile_readall(__pf_stat);
    if (unlikely(!__pf_stat)) {
        error("Cannot read %s", __proc_stat_filename);
        return -1;
    }

    int32_t cores = get_system_cpus();
    get_system_hz();

    // pid_t self = getpid();

    debug("process_stat_test pid: %d", pid);

    struct process_collector *pc = new_process_collector(pid);
    if (pc == NULL) {
        error("new_process_collector() failed");
        return -1;
    }

    signal(SIGINT, __sig_handler);
    signal(SIGTERM, __sig_handler);

    int32_t ret = 0;

    double curr_total_cpu_time = 0.0, prev_total_cpu_time = 0.0;
    double curr_process_cpu_time = 0.0, prev_process_cpu_time = 0.0;

    while (!__sig_exit) {
        // 采集进程的系统资源

        COLLECTOR_PROCESS_USAGE(pc, ret);
        __pf_stat = procfile_readall(__pf_stat);

        if (unlikely(0 != ret)) {
            debug("process '%d' exit", pid);
            break;
        }

        prev_process_cpu_time = curr_process_cpu_time;
        curr_process_cpu_time = pc->process_cpu_time;

        prev_total_cpu_time = curr_total_cpu_time;
        curr_total_cpu_time = __get_total_cpu_time();

        // 计算进程占用CPU时间百分比
        double process_cpu_percentage = (curr_process_cpu_time - prev_process_cpu_time)
                                        / (curr_total_cpu_time - prev_total_cpu_time) * 100.0
                                        * (double)cores;
        debug("process '%d' cpu %f%%", pid, process_cpu_percentage);

        sleep_usec(1000000);
        // debug(" ");
    }

    debug("ret: %d", ret);

    free_process_collector(pc);
    procfile_close(__pf_stat);
    log_fini();

    return 0;
}