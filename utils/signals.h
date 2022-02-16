/*
 * @Author: CALM.WU 
 * @Date: 2021-10-15 10:26:46 
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-11-19 10:42:54
 */

#pragma once

#include <stdint.h>

enum signal_action_mode {
    E_SIGNAL_IGNORE,
    E_SIGNAL_EXIT_CLEANLY,
    E_SIGNAL_SAVE_DATABASE,
    E_SIGNAL_FATAL,
    E_SIGNAL_CHILD,
    E_SIGNAL_RELOADCONFIG,
};

typedef void (*on_signal_t)(int32_t signo, enum signal_action_mode mode);

extern void    signals_init(void);
extern void    signals_handle(on_signal_t fn);
extern int32_t signals_block();
extern int32_t signals_unblock();