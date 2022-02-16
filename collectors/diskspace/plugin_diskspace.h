/*
 * @Author: CALM.WU
 * @Date: 2021-12-20 11:17:39
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-12-20 11:25:43
 */

#pragma once

#include <stdint.h>

extern int32_t diskspace_routine_init();
extern void *  diskspace_routine_start(void *arg);
extern void    diskspace_routine_stop();