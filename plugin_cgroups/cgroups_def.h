/*
 * @Author: calmwu
 * @Date: 2022-08-31 15:21:31
 * @Last Modified by: calmwu
 * @Last Modified time: 2022-08-31 23:09:02
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#include "utils/simple_pattern.h"
#include "utils/sds/sds.h"
#include "utils/list.h"
#include "utils/adaptive_resortable_list.h"

#include "cgroup_metrics.h"

struct plugin_cgroups_ctx {
    int32_t max_cgroups_num;

    // cgroup subsystem enable
    bool cs_cpuacct_enable;
    bool cs_memory_enable;
    bool cs_cpuset_enable;
    bool cs_blkio_enable;
    bool cs_device_enable;

    // v1
    const char *cs_cpuacct_path;
    const char *cs_memory_path;
    const char *cs_cpuset_path;
    const char *cs_blkio_path;
    const char *cs_device_path;
    // v2
    const char *unified_path;

    // matching pattern
    SIMPLE_PATTERN *cgroups_matching;
    SIMPLE_PATTERN *cgroups_subpaths_matching;
};

struct sys_cgroup_usage_per_cpu {
    uint64_t usage_ns;
    uint64_t usage_user_ns;
    uint64_t usage_system_ns;
};

struct sys_cgroup_counters {
    uint64_t cpu_stat_nr_periods;
    uint64_t cpu_stat_nr_throttled;
    uint64_t cpu_stat_throttled_time;

    uint64_t cpu_stat_usage_time;
    uint64_t cpu_stat_usage_user_time;
    uint64_t cpu_stat_usage_system_time;

    // uint64_t cpuacct_usage_ns_per_cpu;
    // uint64_t cpuacct_usage_user_ns_per_cpu;
    // uint64_t cpuacct_usage_system_ns_per_cpu;
    struct sys_cgroup_usage_per_cpu *usage_per_cpus;   // file cpuacct.usage_all

    uint64_t memory_stat_pgpgin;
    uint64_t memory_stat_pgpgout;
    uint64_t memory_stat_pgfault;
    uint64_t memory_stat_pgmajfault;

    uint64_t memory_stat_total_pgpgin;
    uint64_t memory_stat_total_pgpgout;
    uint64_t memory_stat_total_pgfault;
    uint64_t memory_stat_total_pgmajfault;
};

enum vmpressure_levels {
    VMPRESSURE_LOW = 0,
    VMPRESSURE_MEDIUM,
    VMPRESSURE_CRITICAL,
};

// cgroup对象
struct xm_cgroup_obj {
    struct list_head l_discovery_member;    // 发现链表成员
    struct list_head l_collection_member;   // 采集链表成员

    sds      cg_id;       // cgroup 目录名
    uint32_t cg_hash;     // cgroup 目录名的hash值
    char     find_flag;   // cgroup是否存在

    // 需要采集的配置文件
    // v1
    sds cpuacct_cpu_stat_filename;
    sds cpuacct_cpuacct_stat_filename;
    sds cpuacct_cpu_shares_filename;
    sds cpuacct_cpu_cfs_period_us_filename;
    sds cpuacct_cpu_cfs_quota_us_filename;
    sds cpuacct_usage_all_filename;
    sds cpuacct_usage_filename;
    sds cpuacct_usage_user_filename;
    sds cpuacct_usage_sys_filename;

    sds memory_stat_filename;             // memory.stat
    sds memory_usage_in_bytes_filename;   // memory.usage_in_bytes
    sds memory_limit_in_bytes_filename;   // memory.limit_in_bytes
    sds memory_failcnt_filename;
    sds memory_max_usage_in_bytes_filename;

    sds blkio_io_service_bytes_filename;            // blkio.io_service_bytes
    sds blkio_io_serviced_filename;                 // blkio.io_serviced
    sds blkio_io_merged_filename;                   // blkio.io_merged
    sds blkio_io_queued_filename;                   // blkio.io_queued
    sds blkio_throttle_io_service_bytes_filename;   // blkio.throttle.io_service_bytes
    sds blkio_throttle_io_serviced_filename;        // blkio.throttle.io_serviced

    // v2 需要采集的文件
    sds unified_io_stat_filename;
    sds unified_io_pressure;
    sds unified_cpu_max_filename;          // cpu.max
    sds unified_cpu_stat_filename;         // cpu.stat
    sds unified_memory_stat_filename;      // memory.stat
    sds unified_memory_current_filename;   // memory.current
    sds unified_memory_max_filename;       // memory.max
    sds unified_cpu_pressure;
    sds unified_memory_pressure;

    // for prometheus metrics
    prom_collector_t *cg_prom_collector;
    // cgroup指标
    struct sys_cgroup_metrics cg_metrics;
    // 计算counter指标
    struct sys_cgroup_counters cg_counters;

    // cgroup memory pressure event fd
    int32_t mem_pressure_low_level_evt_fd;
    int32_t mem_pressure_medium_level_evt_fd;
    int32_t mem_pressure_critical_level_evt_fd;

    ARL_BASE *arl_base_mem_stat;
};
