/*
 * @Author: CALM.WU
 * @Date: 2022-04-13 15:23:06
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-04-13 16:45:02
 */

#pragma once

#include <unistd.h>
#include <sys/types.h>
#include "collectc/cc_list.h"

struct app_stat {
    pid_t   app_main_pid;    // 应用主进程ID
    int32_t process_count;   // 应用关联的进程数量

    // 汇总的统计指标
    uint64_t minflt_raw;   // 该任务不需要从硬盘拷数据而发生的缺页（次缺页）的次数
    uint64_t cminflt_raw;   // 累计的该任务的所有的waited-for进程曾经发生的次缺页的次数目
    uint64_t majflt_raw;   // 该任务需要从硬盘拷数据而发生的缺页（主缺页）的次数
    uint64_t cmajflt_raw;   // 累计的该任务的所有的waited-for进程曾经发生的主缺页的次数目
    uint64_t utime_raw;
    uint64_t stime_raw;
    uint64_t cutime_raw;
    uint64_t cstime_raw;
    double   process_cpu_time;
    int32_t  num_threads;
    uint64_t vmsize;   // 当前虚拟内存的实际使用量。
    uint64_t vmrss;   // 应用程序实际占用的物理内存大小，但。。。。更确切应该看pss和uss
    uint64_t rssanon;    // 匿名RSS内存大小 kB
    uint64_t rssfile;    // 文件RSS内存大小 kB
    uint64_t rssshmem;   // 共享内存RSS内存大小。
    uint64_t vmswap;     // 进程swap使用量
    uint64_t pss;
    uint64_t uss;
    uint64_t io_logical_bytes_read;
    uint64_t io_logical_bytes_written;
    uint64_t io_read_calls;
    uint64_t io_write_calls;
    uint64_t io_storage_bytes_read;
    uint64_t io_storage_bytes_written;
    int32_t  io_cancelled_write_bytes;
    int32_t  app_open_fds;

    CC_list *process_list;   // 对应进程的列表list process_stat
};