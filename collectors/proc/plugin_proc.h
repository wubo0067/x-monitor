/*
 * @Author: CALM.WU
 * @Date: 2021-11-30 14:58:26
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-04-13 15:21:13
 */

#pragma once

#include <stdint.h>

#include "utils/clocks.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int32_t proc_routine_init();
extern void   *proc_routine_start(void *arg);
extern void    proc_routine_stop();

enum disk_type {
    DISK_TYPE_UNKNOWN,
    DISK_TYPE_PHYSICAL,
    DISK_TYPE_PARTITION,
    DISK_TYPE_VIRTUAL,
};

#define DEF_PROC_COLLECTOR_FUNC(name)                                     \
    extern int32_t init_collector_proc_##name();                          \
    extern int32_t collector_proc_##name(int32_t update_every, usec_t dt, \
                                         const char *config_path);        \
    extern void    fini_collector_proc_##name();

#define REGISTER_PROC_COLLECTOR(ca)                                            \
    {                                                                          \
        .name = #ca, .enabled = 1, .init_func = init_collector_proc_##ca,      \
        .do_func = collector_proc_##ca, .fini_func = fini_collector_proc_##ca, \
    }

DEF_PROC_COLLECTOR_FUNC(diskstats)

DEF_PROC_COLLECTOR_FUNC(stat)

DEF_PROC_COLLECTOR_FUNC(loadavg)

DEF_PROC_COLLECTOR_FUNC(pressure)

DEF_PROC_COLLECTOR_FUNC(vmstat)

DEF_PROC_COLLECTOR_FUNC(meminfo)

DEF_PROC_COLLECTOR_FUNC(netstat)

DEF_PROC_COLLECTOR_FUNC(net_snmp)

DEF_PROC_COLLECTOR_FUNC(net_dev)

DEF_PROC_COLLECTOR_FUNC(net_sockstat)

DEF_PROC_COLLECTOR_FUNC(net_stat_conntrack)

DEF_PROC_COLLECTOR_FUNC(cgroups)

DEF_PROC_COLLECTOR_FUNC(schedstat)

#ifdef __cplusplus
}
#endif
