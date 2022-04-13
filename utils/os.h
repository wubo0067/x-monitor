/*
 * @Author: CALM.WU
 * @Date: 2022-01-18 11:31:35
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-01-18 12:00:28
 */

#pragma once

#include <unistd.h>
#include "compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const char *get_hostname();

extern const char *get_ipaddr_by_iface(const char *iface, char *ip_buf, size_t ip_buf_size);

extern const char *get_macaddr_by_iface(const char *iface, char *mac_buf, size_t mac_buf_size);

extern int32_t bump_memlock_rlimit(void);

extern const char *get_username(uid_t uid);

extern int32_t get_system_cpus();

extern int32_t read_tcp_mem(uint64_t *low, uint64_t *pressure, uint64_t *high);

extern int32_t read_tcp_max_orphans(uint64_t *max_orphans);

static __always_inline int32_t get_pgsize_kb() {
    int32_t pgsize_kb = sysconf(_SC_PAGESIZE) >> 10;
    return pgsize_kb;
}

extern int32_t get_process_name(pid_t pid, char *name, size_t name_size);

extern uint32_t system_hz;
extern void     get_system_hz();

extern uint64_t get_uptime();

extern pid_t system_pid_max;
extern pid_t get_system_pid_max();

#ifdef __cplusplus
}
#endif