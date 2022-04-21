/*
 * @Author: CALM.WU
 * @Date: 2022-04-13 15:18:39
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-04-13 15:21:02
 */

#pragma once

#include <stdint.h>

extern int32_t appstat_collector_routine_init();
extern void   *appstat_collector_routine_start(void *arg);
extern void    appstat_collector_routine_stop();