/*
 * @Author: CALM.WU
 * @Date: 2022-03-23 16:19:17
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-03-29 14:29:08
 */

// https://www.cnblogs.com/songhaibin/p/13885403.html
// https://stackoverflow.com/questions/1420426/how-to-calculate-the-cpu-usage-of-a-process-by-pid-in-linux-from-c
// https://www.anshulpatel.in/post/linux_cpu_percentage/  How Linux calculates CPU utilization
// https://github.com/GNOME/libgtop
// https://man7.org/linux/man-pages/man5/proc.5.html

// processCPUTime = utime + stime + cutime + cstime
// Percentage = (100 * Process Jiffies)/Total CPU Jiffies (sampled per second)
// cat /proc/self/stat | awk '{print $14, $15, $16, $17}'

#include "process_collector.h"

#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/strings.h"
#include "utils/os.h"

int32_t collector_process_cpu_usage(struct process_collector *pc) {
    int32_t set_quotes = (NULL == pc->pf_proc_pid_stat) ? 1 : 0;

    // process随时都会结束，所以每次采集都要重新打开
    pc->pf_proc_pid_stat = procfile_reopen(pc->pf_proc_pid_stat, pc->stat_full_filename, NULL,
                                           PROCFILE_FLAG_NO_ERROR_ON_FILE_IO);
    if (unlikely(NULL == pc->pf_proc_pid_stat)) {
        error("[PROCESS] procfile_reopen '%s' failed.", pc->stat_full_filename);
        return -1;
    }

    if (unlikely(0 == set_quotes)) {
        procfile_set_open_close(pc->pf_proc_pid_stat, "(", ")");
    }

    pc->pf_proc_pid_stat = procfile_readall(pc->pf_proc_pid_stat);
    if (unlikely(!pc->pf_proc_pid_stat)) {
        error("[PROCESS:cpu] read process stat '%s' failed.", pc->pf_proc_pid_stat->filename);
        return -1;
    }

    const char *comm = procfile_lineword(pc->pf_proc_pid_stat, 0, 1);
    pc->ppid = (pid_t)str2uint32_t(procfile_lineword(pc->pf_proc_pid_stat, 0, 3));

    if (strcmp(comm, pc->comm) != 0) {
        strlcpy(pc->comm, comm, XM_PROCESS_COMM_SIZE);
    }

    pc->minflt_raw = str2uint64_t(procfile_lineword(pc->pf_proc_pid_stat, 0, 9));
    pc->cminflt_raw = str2uint64_t(procfile_lineword(pc->pf_proc_pid_stat, 0, 10));
    pc->majflt_raw = str2uint64_t(procfile_lineword(pc->pf_proc_pid_stat, 0, 11));
    pc->cmajflt_raw = str2uint64_t(procfile_lineword(pc->pf_proc_pid_stat, 0, 12));

    pc->utime_raw = str2uint64_t(procfile_lineword(pc->pf_proc_pid_stat, 0, 13));
    pc->stime_raw = str2uint64_t(procfile_lineword(pc->pf_proc_pid_stat, 0, 14));
    pc->cutime_raw = str2uint64_t(procfile_lineword(pc->pf_proc_pid_stat, 0, 15));
    pc->cstime_raw = str2uint64_t(procfile_lineword(pc->pf_proc_pid_stat, 0, 16));
    pc->process_cpu_time = (double)(pc->utime_raw + pc->stime_raw + pc->cutime_raw + pc->cstime_raw)
                           / (double)system_hz;

    pc->num_threads = str2int32_t(procfile_lineword(pc->pf_proc_pid_stat, 0, 19));
    pc->start_time =
        (double)str2uint64_t(procfile_lineword(pc->pf_proc_pid_stat, 0, 21)) / (double)system_hz;

    debug("[PROCESS:cpu] process: '%d' ppid: %d utime: %lu jiffies, stime: %lu jiffies, "
          "cutime: %lu jiffies, cstime: %lu jiffies, process_cpu_time: %lf seconds, minflt: %lu, "
          "cminflt: %lu, majflt: %lu, cmajflt: %lu, num_threads: %d, start_time: %.3f",
          pc->pid, pc->ppid, pc->utime_raw, pc->stime_raw, pc->cutime_raw, pc->cstime_raw,
          pc->process_cpu_time, pc->minflt_raw, pc->cminflt_raw, pc->majflt_raw, pc->cmajflt_raw,
          pc->num_threads, pc->start_time);

    return 0;
}