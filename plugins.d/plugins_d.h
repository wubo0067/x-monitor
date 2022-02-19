/*
 * @Author: CALM.WU
 * @Date: 2021-10-15 14:40:46
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-10-22 14:54:57
 */

#pragma once

#include "utils/common.h"

extern int32_t pluginsd_routine_init();
extern void   *pluginsd_routine_start(void *arg);
extern void    pluginsd_routine_stop();