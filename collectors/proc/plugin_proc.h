/*
 * @Author: CALM.WU
 * @Date: 2021-11-30 14:58:26
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-01-18 15:18:09
 */

#pragma once

#include <stdint.h>

#include "utils/clocks.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int32_t proc_routine_init();
extern void *  proc_routine_start(void *arg);
extern void    proc_routine_stop();

enum disk_type {
    DISK_TYPE_UNKNOWN,
    DISK_TYPE_PHYSICAL,
    DISK_TYPE_PARTITION,
    DISK_TYPE_VIRTUAL,
};

#define DEF_COLLECTOR_PROC_FUNC(name)                                     \
    extern int32_t init_collector_proc_##name();                          \
    extern int32_t collector_proc_##name(int32_t update_every, usec_t dt, \
                                         const char *config_path);        \
    extern void    fini_collector_proc_##name();

DEF_COLLECTOR_PROC_FUNC(diskstats)

DEF_COLLECTOR_PROC_FUNC(cpustat)

DEF_COLLECTOR_PROC_FUNC(loadavg)

DEF_COLLECTOR_PROC_FUNC(pressure)

DEF_COLLECTOR_PROC_FUNC(vmstat)

DEF_COLLECTOR_PROC_FUNC(meminfo)

#ifdef __cplusplus
}
#endif
