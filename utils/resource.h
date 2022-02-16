/*
 * @Author: CALM.WU
 * @Date: 2021-11-03 11:50:57
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-02-11 10:36:50
 */

#pragma once

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int32_t bump_memlock_rlimit(void);

extern const char *get_username(uid_t uid);

extern int32_t get_system_cpus();

#ifdef __cplusplus
}
#endif