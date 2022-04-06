/*
 * @Author: CALM.WU
 * @Date: 2022-03-23 16:19:17
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-03-29 14:29:08
 */

// https://www.cnblogs.com/songhaibin/p/13885403.html
// https://stackoverflow.com/questions/1420426/how-to-calculate-the-cpu-usage-of-a-process-by-pid-in-linux-from-c
// https://www.anshulpatel.in/post/linux_cpu_percentage/
// https://github.com/GNOME/libgtop
// https://man7.org/linux/man-pages/man5/proc.5.html

// processCPUTime = utime + stime + cutime + cstime
// Percentage = (100 * Process Jiffies)/Total CPU Jiffies (sampled per second)
// cat /proc/self/stat | awk '{print $14, $15, $16, $17}'

#include "process_stat.h"

#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/strings.h"
#include "utils/os.h"

int32_t collector_process_cpu_usage(struct process_stat *stat) {

    stat->pf_proc_pid_stat = procfile_readall(stat->pf_proc_pid_stat);
    if (unlikely(!stat->pf_proc_pid_stat)) {
        error("[PROCESS:cpu] read process stat '%s' failed.", stat->pf_proc_pid_stat->filename);
        return -1;
    }

    const char *comm = procfile_lineword(stat->pf_proc_pid_stat, 0, 1);
    stat->ppid = (pid_t)str2uint32_t(procfile_lineword(stat->pf_proc_pid_stat, 0, 3));

    if (strcmp(comm, stat->comm) != 0) {
        strlcpy(stat->comm, comm, XM_PROCESS_COMM_SIZE);
    }

    stat->minflt_raw = str2uint64_t(procfile_lineword(stat->pf_proc_pid_stat, 0, 9));
    stat->cminflt_raw = str2uint64_t(procfile_lineword(stat->pf_proc_pid_stat, 0, 10));
    stat->majflt_raw = str2uint64_t(procfile_lineword(stat->pf_proc_pid_stat, 0, 11));
    stat->cmajflt_raw = str2uint64_t(procfile_lineword(stat->pf_proc_pid_stat, 0, 12));

    stat->utime_raw = str2uint64_t(procfile_lineword(stat->pf_proc_pid_stat, 0, 13));
    stat->stime_raw = str2uint64_t(procfile_lineword(stat->pf_proc_pid_stat, 0, 14));
    stat->cutime_raw = str2uint64_t(procfile_lineword(stat->pf_proc_pid_stat, 0, 15));
    stat->cstime_raw = str2uint64_t(procfile_lineword(stat->pf_proc_pid_stat, 0, 16));
    stat->process_cpu_time =
        (double)(stat->utime_raw + stat->stime_raw + stat->cutime_raw + stat->cstime_raw)
        / (double)system_hz;

    stat->num_threads = str2int32_t(procfile_lineword(stat->pf_proc_pid_stat, 0, 19));
    stat->start_time =
        (double)str2uint64_t(procfile_lineword(stat->pf_proc_pid_stat, 0, 21)) / (double)system_hz;

    debug("[PROCESS:cpu] read '%s' process: '%s' ppid: %d utime: %lu jiffies, stime: %lu jiffies, "
          "cutime: %lu jiffies, cstime: %lu jiffies, process_cpu_time: %lf seconds, minflt: %lu, "
          "cminflt: %lu, majflt: %lu, cmajflt: %lu, num_threads: %d, start_time: %.3f",
          stat->pf_proc_pid_stat->filename, stat->comm, stat->ppid, stat->utime_raw,
          stat->stime_raw, stat->cutime_raw, stat->cstime_raw, stat->process_cpu_time,
          stat->minflt_raw, stat->cminflt_raw, stat->majflt_raw, stat->cmajflt_raw,
          stat->num_threads, stat->start_time);

    return 0;
}