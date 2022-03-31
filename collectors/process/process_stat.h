/*
 * @Author: CALM.WU
 * @Date: 2022-03-28 15:26:24
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-03-31 10:30:54
 */

#pragma once

#include <sys/types.h>
#include <stdint.h>
#include "utils/consts.h"

struct proc_file;

struct process_stat {
    pid_t    pid;
    pid_t    ppid;
    char     comm[XM_CMD_LINE_MAX];
    uint32_t hash;

    int32_t           fd_status;
    struct proc_file *pf_proc_pid_stat;
    struct proc_file *pf_proc_pid_io;

    // /proc/<pid>/stat
    uint64_t minflt_raw;   // 该任务不需要从硬盘拷数据而发生的缺页（次缺页）的次数
    uint64_t cminflt_raw;   // 累计的该任务的所有的waited-for进程曾经发生的次缺页的次数目
    uint64_t majflt_raw;   // 该任务需要从硬盘拷数据而发生的缺页（主缺页）的次数
    uint64_t cmajflt_raw;   // 累计的该任务的所有的waited-for进程曾经发生的主缺页的次数目

    // cpu
    uint64_t utime_raw;   // 该任务在用户态运行的时间，单位为jiffies
    uint64_t stime_raw;   // 该任务在核心态运行的时间，单位为jiffies
    uint64_t
        cutime_raw;   // 累计的该任务的所有的waited-for进程曾经在用户态运行的时间，单位为jiffies
    uint64_t
        cstime_raw;   // 累计的该任务的所有的waited-for进程曾经在核心态运行的时间，单位为jiffies

    int32_t num_threads;   //

    // /proc/<pid>/status

    uint64_t vmsize;   // 当前虚拟内存的实际使用量。
    uint64_t vmrss;   // 应用程序实际占用的物理内存大小，但。。。。更确切应该看pss和uss
    uint64_t rssanon;    // 匿名RSS内存大小 kB
    uint64_t rssfile;    // 文件RSS内存大小 kB
    uint64_t rssshmem;   // 共享内存RSS内存大小。
    uint64_t vmswap;     // 进程swap使用量
    // /proc/<pid>/pmaps
    uint64_t pss;
    uint64_t uss;
};

extern struct process_stat *process_stat_new(pid_t pid);

extern void process_stat_free(struct process_stat *stat);

extern int32_t collector_process_mem_usage(struct process_stat *stat);
