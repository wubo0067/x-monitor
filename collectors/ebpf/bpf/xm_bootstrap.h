/*
 * @Author: CALM.WU
 * @Date: 2022-02-04 13:54:13
 * @Last Modified by: calmwu
 * @Last Modified time: 2022-02-19 21:54:12
 */

#pragma once

#include <vmlinux.h>
#include "common.h"

#define MAX_FILENAME_LEN 128

struct bs_event {
    pid_t pid;
    pid_t ppid;
    __u16 exit_code;
    __u64 duration_ns;
    char  comm[TASK_COMM_LEN];
    char  filename[MAX_FILENAME_LEN];
    bool  exit_event;
};
