/*
 * @Author: CALM.WU
 * @Date: 2021-10-15 10:20:43
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-10-21 14:52:57
 */

#pragma once

#include "common.h"

extern int32_t become_daemon(int32_t dont_fork, const char *pid_file, const char *user);
extern int32_t become_user(const char *user, int32_t pid_fd, const char *pid_file);
extern int32_t kill_pid(pid_t pid);