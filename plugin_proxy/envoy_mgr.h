/*
 * @Author: calmwu
 * @Date: 2022-03-16 09:59:47
 * @Last Modified by:   calmwu
 * @Last Modified time: 2022-03-16 09:59:47
 */

#pragma once

#include "utils/common.h"

extern int32_t envoy_manager_routine_init();
extern void *envoy_manager_routine_start(void *arg);
extern void envoy_manager_routine_stop();
