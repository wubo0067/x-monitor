/*
 * @Author: CALM.WU
 * @Date: 2021-10-18 11:47:28
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-10-28 14:14:07
 */

#pragma once

#include <stdint.h>

extern int32_t appconfig_load(const char *config_file);
extern void    appconfig_destroy();
extern void    appconfig_reload();

// extern void appconfig_rdlock();
// extern void appconfig_wrlock();
// extern void appconfig_unlock();

// ---------------------------------------------------------
extern const char *appconfig_get_str(const char *key, const char *def);
extern int32_t     appconfig_get_bool(const char *key, int32_t def);
extern int32_t     appconfig_get_int(const char *key, int32_t def);

extern const char *appconfig_get_member_str(const char *path, const char *key,
                                            const char *def);
extern int32_t     appconfig_get_member_bool(const char *path, const char *key,
                                             int32_t def);
extern int32_t     appconfig_get_member_int(const char *path, const char *key,
                                            int32_t def);