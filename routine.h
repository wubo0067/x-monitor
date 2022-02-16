/*
 * @Author: CALM.WU
 * @Date: 2021-10-22 14:49:37
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-10-22 16:32:23
 */

#pragma once

#include <pthread.h>
#include <signal.h>
#include <stdint.h>

struct xmonitor_static_routine {
    const char *name;
    const char *config_name;

    volatile sig_atomic_t enabled; // the current status of the thread
    pthread_t            *thread_id;

    int32_t (*init_routine)();
    void *(*start_routine)(void *);
    void (*stop_routine)();

    volatile sig_atomic_t exit_flag;

    struct xmonitor_static_routine *next;
};

extern void
register_xmonitor_static_routine(struct xmonitor_static_routine *routine);