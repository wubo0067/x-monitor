/*
 * @Author: CALM.WU
 * @Date: 2021-11-03 11:26:22
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-02-15 15:06:30
 */

#pragma once

#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
//#include <linux/compiler.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
//#include <linux/bpf.h>
//#include <linux/ptrace.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ksym {
    long  addr;
    char *name;
};

//#include <trace_helpers.h>
//#include <perf-sys.h>

extern int32_t      load_kallsyms();
extern struct ksym *ksym_search(long key);
extern long         ksym_get_addr(const char *name);
/* open kallsyms and find addresses on the fly, faster than load + search. */
extern int32_t kallsyms_find(const char *sym, unsigned long long *addr);

extern int32_t bpf_printf(enum libbpf_print_level level, const char *fmt, va_list args);

extern const char *bpf_get_ksym_name(uint64_t addr);

extern int32_t open_raw_sock(const char *iface);

extern const char *bpf_xdpaction2str(uint32_t action);

extern int32_t bpf_get_bpf_map_info(int32_t fd, struct bpf_map_info *info, int32_t verbose);

// struct perf_event_attr;

// static inline int sys_perf_event_open(struct perf_event_attr *attr, pid_t pid,
//                                       int cpu, int group_fd,
//                                       unsigned long flags)
// {
//     return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
// }

extern void bpf_xdp_stats_print(int32_t xdp_stats_map_fd);

extern int32_t bpf_xdp_link_attach(int32_t ifindex, uint32_t xdp_flags, int32_t prog_fd);

extern int32_t bpf_xdp_link_detach(int32_t ifindex, uint32_t xdp_flags, uint32_t expected_prog_id);

static __always_inline uint32_t bpf_num_possible_cpus() {
    int possible_cpus = libbpf_num_possible_cpus();

    if (possible_cpus < 0) {
        fprintf(stderr, "Failed to get # of possible cpus: '%s'!\n", strerror(-possible_cpus));
        exit(1);
    }
    return possible_cpus;
}

#define __bpf_percpu_val_align __attribute__((__aligned__(8)))

#define BPF_DECLARE_PERCPU(type, name) \
    struct {                           \
        type v; /* padding */          \
    } __bpf_percpu_val_align name[bpf_num_possible_cpus()]
#define bpf_percpu(name, cpu) name[(cpu)].v

#ifdef __cplusplus
}
#endif