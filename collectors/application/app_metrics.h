/*
 * @Author: calmwu
 * @Date: 2022-05-08 14:44:34
 * @Last Modified by: calmwu
 * @Last Modified time: 2022-05-08 19:13:18
 */

#pragma once

#include "prometheus-client-c/prom.h"

extern const char *__app_metric_minflt_help;
extern const char *__app_metric_cminflt_help;
extern const char *__app_metric_majflt_help;
extern const char *__app_metric_cmajflt_help;
extern const char *__app_metric_utime_help;
extern const char *__app_metric_stime_help;
extern const char *__app_metric_cutime_help;
extern const char *__app_metric_cstime_help;
extern const char *__app_metric_cpu_secs_help;
extern const char *__app_metric_num_threads_help;
extern const char *__app_metric_vmsize_help;
extern const char *__app_metric_vmrss_help;
extern const char *__app_metric_rssanon_help;
extern const char *__app_metric_rssfile_help;
extern const char *__app_metric_rssshmem_help;
extern const char *__app_metric_vmswap_help;
extern const char *__app_metric_pss_help;
extern const char *__app_metric_uss_help;
extern const char *__app_metric_io_logical_bytes_read_help;
extern const char *__app_metric_io_logical_bytes_written_help;
extern const char *__app_metric_io_read_calls_help;
extern const char *__app_metric_io_write_calls_help;
extern const char *__app_metric_io_storage_bytes_read_help;
extern const char *__app_metric_io_storage_bytes_written_help;
extern const char *__app_metric_io_cancelled_write_bytes_help;
extern const char *__app_metric_open_fds_help;

struct app_metrics {
    prom_gauge_t *metric_minflt;
    prom_gauge_t *metric_cminflt;
    prom_gauge_t *metric_majflt;
    prom_gauge_t *metric_cmajflt;
    prom_gauge_t *metric_utime;
    prom_gauge_t *metric_stime;
    prom_gauge_t *metric_cutime;
    prom_gauge_t *metric_cstime;
    prom_gauge_t *metric_cpu_secs;
    prom_gauge_t *metric_num_threads;
    prom_gauge_t *metric_vmsize;
    prom_gauge_t *metric_vmrss;
    prom_gauge_t *metric_rssanon;
    prom_gauge_t *metric_rssfile;
    prom_gauge_t *metric_rssshmem;
    prom_gauge_t *metric_vmswap;
    prom_gauge_t *metric_pss;
    prom_gauge_t *metric_uss;
    prom_gauge_t *metric_io_logical_bytes_read;
    prom_gauge_t *metric_io_logical_bytes_written;
    prom_gauge_t *metric_io_read_calls;
    prom_gauge_t *metric_io_write_calls;
    prom_gauge_t *metric_io_storage_bytes_read;
    prom_gauge_t *metric_io_storage_bytes_written;
    prom_gauge_t *metric_io_cancelled_write_bytes;
    prom_gauge_t *metric_open_fds;
};
