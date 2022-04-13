/*
 * @Author: CALM.WU
 * @Date: 2022-04-13 15:18:39
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-04-13 15:21:02
 */

#pragma once

#include <stdint.h>

extern int32_t application_routine_init();
extern void   *application_routine_start(void *arg);
extern void    application_routine_stop();