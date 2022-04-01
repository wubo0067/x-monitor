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

int32_t collector_process_cpu_usage(struct process_stat *stat) {
    return 0;
}