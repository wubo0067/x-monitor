/*
 * @Author: CALM.WU
 * @Date: 2022-03-23 16:19:17
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-04-13 16:43:51
 */

// https://www.cnblogs.com/songhaibin/p/13885403.html
// https://stackoverflow.com/questions/1420426/how-to-calculate-the-cpu-usage-of-a-process-by-pid-in-linux-from-c
// https://www.anshulpatel.in/post/linux_cpu_percentage/  How Linux calculates CPU utilization
// https://github.com/GNOME/libgtop
// https://man7.org/linux/man-pages/man5/proc.5.html

// processCPUTime = utime + stime + cutime + cstime
// Percentage = (100 * Process Jiffies)/Total CPU Jiffies (sampled per second)
// cat /proc/self/stat | awk '{print $14, $15, $16, $17, $43, $44}'

#include "process_status.h"

#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/strings.h"
#include "utils/os.h"

int32_t collector_process_cpu_usage(struct process_status *ps) {
    int32_t set_quotes = (NULL == ps->pf_proc_pid_stat) ? 1 : 0;

    // process随时都会结束，所以每次采集都要重新打开
    ps->pf_proc_pid_stat = procfile_reopen(ps->pf_proc_pid_stat, ps->stat_full_filename, NULL,
                                           PROCFILE_FLAG_NO_ERROR_ON_FILE_IO);
    if (unlikely(NULL == ps->pf_proc_pid_stat)) {
        error("[PROCESS] procfile_reopen '%s' failed.", ps->stat_full_filename);
        return -1;
    }

    if (unlikely(0 == set_quotes)) {
        procfile_set_open_close(ps->pf_proc_pid_stat, "(", ")");
    }

    ps->pf_proc_pid_stat = procfile_readall(ps->pf_proc_pid_stat);
    if (unlikely(!ps->pf_proc_pid_stat)) {
        error("[PROCESS:cpu] read process stat '%s' failed.", ps->pf_proc_pid_stat->filename);
        return -1;
    }

    const char *comm = procfile_lineword(ps->pf_proc_pid_stat, 0, 1);
    ps->ppid = (pid_t)str2uint32_t(procfile_lineword(ps->pf_proc_pid_stat, 0, 3));

    if (strcmp(comm, ps->comm) != 0) {
        strlcpy(ps->comm, comm, XM_PROCESS_COMM_SIZE);
    }

    uint64_t gtime = 0, cgtime = 0;

    ps->minflt_raw = str2uint64_t(procfile_lineword(ps->pf_proc_pid_stat, 0, 9));
    ps->cminflt_raw = str2uint64_t(procfile_lineword(ps->pf_proc_pid_stat, 0, 10));
    ps->majflt_raw = str2uint64_t(procfile_lineword(ps->pf_proc_pid_stat, 0, 11));
    ps->cmajflt_raw = str2uint64_t(procfile_lineword(ps->pf_proc_pid_stat, 0, 12));

    ps->utime_raw = str2uint64_t(procfile_lineword(ps->pf_proc_pid_stat, 0, 13));
    ps->stime_raw = str2uint64_t(procfile_lineword(ps->pf_proc_pid_stat, 0, 14));
    ps->cutime_raw = str2uint64_t(procfile_lineword(ps->pf_proc_pid_stat, 0, 15));
    ps->cstime_raw = str2uint64_t(procfile_lineword(ps->pf_proc_pid_stat, 0, 16));

    gtime = str2uint64_t(procfile_lineword(ps->pf_proc_pid_stat, 0, 42));
    cgtime = str2uint64_t(procfile_lineword(ps->pf_proc_pid_stat, 0, 43));

    ps->utime_raw -= gtime;
    ps->cutime_raw -= cgtime;

    ps->process_cpu_secs = (double)(ps->utime_raw + ps->stime_raw + ps->cutime_raw + ps->cstime_raw)
                           / (double)system_hz;

    ps->num_threads = str2int32_t(procfile_lineword(ps->pf_proc_pid_stat, 0, 19));
    ps->start_time =
        (double)str2uint64_t(procfile_lineword(ps->pf_proc_pid_stat, 0, 21)) / (double)system_hz;

    debug("[PROCESS:cpu] process: '%d' ppid: %d utime: %lu jiffies, stime: %lu jiffies, "
          "cutime: %lu jiffies, cstime: %lu jiffies, process_cpu_secs: %lf seconds, minflt: %lu, "
          "cminflt: %lu, majflt: %lu, cmajflt: %lu, num_threads: %d, start_time: %.3f",
          ps->pid, ps->ppid, ps->utime_raw, ps->stime_raw, ps->cutime_raw, ps->cstime_raw,
          ps->process_cpu_secs, ps->minflt_raw, ps->cminflt_raw, ps->majflt_raw, ps->cmajflt_raw,
          ps->num_threads, ps->start_time);

    return 0;
}