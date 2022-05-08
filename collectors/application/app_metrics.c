/*
 * @Author: calmwu
 * @Date: 2022-05-08 19:10:57
 * @Last Modified by: calmwu
 * @Last Modified time: 2022-05-08 19:11:36
 */

#include "app_metrics.h"

const char *__app_metric_minflt_help =
    "The number of minor faults the app has made which have not required "
    "loading a memory page from disk.";
const char *__app_metric_cminflt_help =
    "The number of minor faults that the app's waited-for children have made.";
const char *__app_metric_majflt_help =
    "The number of major faults the app has made which have required loading a "
    "memory page from disk.";
const char *__app_metric_cmajflt_help =
    "The number of major faults that the app's waited-for children have made.";
const char *__app_metric_utime_help =
    "The number of jiffies the app has been scheduled in user mode.";
const char *__app_metric_stime_help =
    "The number of jiffies the app has been scheduled in kernel mode.";
const char *__app_metric_cutime_help =
    "The number of jiffies the app's waited-for children have been scheduled in user mode.";
const char *__app_metric_cstime_help =
    "The number of jiffies the app's waited-for children have been scheduled in kernel mode.";
const char *__app_metric_cpu_secs_help = "The number of cpu seconds the app has been scheduled";
const char *__app_metric_num_threads_help = "The number of threads the app has";
const char *__app_metric_vmsize_help = "The number of kilobytes of virtual memory";
const char *__app_metric_vmrss_help =
    "The number of kilobytes of resident memory, the value here is the sum of "
    "RssAnon, RssFile, and RssShmem.";
const char *__app_metric_rssanon_help = "The number of kilobytes of resident anonymous memory";
const char *__app_metric_rssfile_help = "The number of kilobytes of resident file mappings";
const char *__app_metric_rssshmem_help =
    "The number of kilobytes of resident shared memory (includes System V "
    "shared memory, mappings from tmpfs(5), and shared anonymous mappings)";
const char *__app_metric_vmswap_help =
    "The number of kilobytes of Swapped-out virtual memory size by anonymous private pages";
const char *__app_metric_pss_help =
    "The number of kilobytes of Proportional Set memory size, It works exactly "
    "like RSS, but with the added difference of partitioning shared libraries";
const char *__app_metric_uss_help =
    "The number of kilobytes of Unique Set memory size, represents the private memory of a "
    "process.  it shows libraries and pages allocated only to this process";
const char *__app_metric_io_logical_bytes_read_help =
    "The number of bytes which this task has caused to be read from storage,  This is "
    "simply the sum of bytes which this process passed to read(2) and similar system calls";
const char *__app_metric_io_logical_bytes_written_help =
    "The number of bytes which this task has caused, or shall cause to be written to disk";
const char *__app_metric_io_read_calls_help =
    "Attempt to count the number of read I/O operations—that is, system calls "
    "such as read(2) and pread(2)";
const char *__app_metric_io_write_calls_help =
    "write syscalls Attempt to count the number of write I/O operations—that "
    "is, system calls such as write(2) and pwrite(2)";
const char *__app_metric_io_storage_bytes_read_help =
    "Attempt to count the number of bytes which this process really did cause to be "
    "fetched from the storage layer.  This is accurate for block-backed filesystems.";
const char *__app_metric_io_storage_bytes_written_help =
    "Attempt to count the number of bytes which this process caused to be sent "
    "to the storage layer.";
const char *__app_metric_io_cancelled_write_bytes_help =
    "this field represents the number of bytes which this process caused to not "
    "happen, by truncating pagecache.";
const char *__app_metric_open_fds_help = "The number of open file descriptors";