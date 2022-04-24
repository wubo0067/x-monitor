/*
 * @Author: CALM.WU
 * @Date: 2022-03-28 15:26:24
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-04-24 15:12:17
 */

#pragma once

#include <sys/types.h>
#include <stdint.h>
#include "utils/consts.h"

struct proc_file;
struct xm_mempool_s;

struct process_status {
    pid_t pid;
    pid_t ppid;
    char  comm[XM_PROCESS_COMM_SIZE];

    char *stat_full_filename;
    char *status_full_filename;
    char *io_full_filename;
    char *fd_full_filename;

    struct proc_file *pf_proc_pid_stat;
    struct proc_file *pf_proc_pid_io;

    // /proc/<pid>/stat
    uint64_t minflt_raw;   // 该任务不需要从硬盘拷数据而发生的缺页（次缺页）的次数
    uint64_t cminflt_raw;   // 累计的该任务的所有的waited-for进程曾经发生的次缺页的次数目
    uint64_t majflt_raw;   // 该任务需要从硬盘拷数据而发生的缺页（主缺页）的次数
    uint64_t cmajflt_raw;   // 累计的该任务的所有的waited-for进程曾经发生的主缺页的次数目

    // 该任务在用户态运行的时间，单位为jiffies
    uint64_t utime_raw;
    // 该任务在核心态运行的时间，单位为jiffies
    uint64_t stime_raw;
    // 累计的该任务的所有的waited-for进程曾经在用户态运行的时间，单位为jiffies
    uint64_t cutime_raw;
    // 累计的该任务的所有的waited-for进程曾经在核心态运行的时间，单位为jiffies
    uint64_t cstime_raw;
    // 单位为jiffies，除以system_hz得到的值为该任务的启动时间，单位为秒
    double process_cpu_time;

    //
    int32_t num_threads;
    // 该任务的启动时间，单位为jiffies，除以system_hz得到的值为该任务的启动时间，单位为秒
    double start_time;

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

    // /proc/<pid>/io
    //     I/O counter: chars read
    // The number of bytes which this task has caused to be read from storage. This
    // is simply the sum of bytes which this process passed to read() and pread().
    // It includes things like tty IO and it is unaffected by whether or not actual
    // physical disk IO was required (the read might have been satisfied from
    // pagecache).
    // 读出的总字节数，read或者pread()中的长度参数总和（pagecache中统计而来，不代表实际磁盘的读入）
    uint64_t io_logical_bytes_read;
    //     I/O counter: chars written
    // The number of bytes which this task has caused, or shall cause to be written
    // to disk. Similar caveats apply here as with rchar.
    // 写入的总字节数，write或者pwrite中的长度参数总和
    uint64_t io_logical_bytes_written;
    // I/O counter: read syscalls
    // Attempt to count the number of read I/O operations, i.e. syscalls like read()
    // and pread().
    // read()或者pread()总的调用次数
    uint64_t io_read_calls;
    //     I/O counter: write syscalls
    // Attempt to count the number of write I/O operations, i.e. syscalls like
    // write() and pwrite().
    // write()或者pwrite()总的调用次数
    uint64_t io_write_calls;
    // I/O counter: bytes read
    // Attempt to count the number of bytes which this process really did cause to
    // be fetched from the storage layer. Done at the submit_bio() level, so it is
    // accurate for block-backed filesystems. <please add status regarding NFS and
    // CIFS at a later time>
    // 实际从磁盘中读取的字节总数   (这里if=/dev/zero 所以没有实际的读入字节数)
    uint64_t io_storage_bytes_read;
    // I/O counter: bytes written
    // Attempt to count the number of bytes which this process caused to be sent to
    // the storage layer. This is done at page-dirtying time.
    // 实际写入到磁盘中的字节总数
    uint64_t io_storage_bytes_written;
    // The big inaccuracy here is truncate. If a process writes 1MB to a file and
    // then deletes the file, it will in fact perform no writeout. But it will have
    // been accounted as having caused 1MB of write.
    // In other words: The number of bytes which this process caused to not happen,
    // by truncating pagecache. A task can cause "negative" IO too. If this task
    // truncates some dirty pagecache, some IO which another task has been accounted
    // for (in its write_bytes) will not be happening. We _could_ just subtract that
    // from the truncating task's write_bytes, but there is information loss in doing
    // that.
    // 由于截断pagecache导致应该发生而没有发生的写入字节数（可能为负数）
    int32_t io_cancelled_write_bytes;

    //
    int32_t process_open_fds;
};

extern struct process_status *new_process_status(pid_t pid, struct xm_mempool_s *xmp);

extern void free_process_status(struct process_status *ps, struct xm_mempool_s *xmp);

extern int32_t collector_process_mem_usage(struct process_status *ps);

extern int32_t collector_process_cpu_usage(struct process_status *ps);

extern int32_t collector_process_io_usage(struct process_status *ps);

extern int32_t collector_process_fd_usage(struct process_status *ps);

#define COLLECTOR_PROCESS_USAGE(ps, ret)                              \
    do {                                                              \
        if (unlikely((ret = collector_process_cpu_usage(ps)) != 0)) { \
            break;                                                    \
        }                                                             \
        if (unlikely((ret = collector_process_mem_usage(ps)) != 0)) { \
            break;                                                    \
        }                                                             \
        if (unlikely((ret = collector_process_io_usage(ps)) != 0)) {  \
            break;                                                    \
        }                                                             \
        if (unlikely((ret = collector_process_fd_usage(ps)) != 0)) {  \
            break;                                                    \
        }                                                             \
    } while (0)
