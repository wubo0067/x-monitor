/*
 * @Author: CALM.WU 
 * @Date: 2021-11-02 15:08:24 
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-12-16 11:33:54
 */

#pragma once

#include <linux/ptrace.h>
#include <linux/version.h>
#include <linux/threads.h>
#include <linux/sched.h>
#include <uapi/linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include <trace_common.h>

// Avoiding format string array on the stack
#undef printk
#if !__has_builtin(__builtin_preserve_type_info)
#define printk(fmt, ...)                                                       \
    ({                                                                         \
        static const char ____fmt[] = fmt;                                     \
        bpf_trace_printk(____fmt, sizeof(____fmt), ##__VA_ARGS__);             \
    })
#else
#define printk(fmt, ...)                                                       \
    ({                                                                         \
        static char ____fmt[] = fmt "\0";                                      \
        if (bpf_core_type_exists(                                              \
                struct trace_event_raw_bpf_trace_printk___x)) {                \
            bpf_trace_printk(____fmt, sizeof(____fmt) - 1, ##__VA_ARGS__);     \
        } else {                                                               \
            ____fmt[sizeof(____fmt) - 2] = '\n';                               \
            bpf_trace_printk(____fmt, sizeof(____fmt), ##__VA_ARGS__);         \
        }                                                                      \
    })
#endif

#define PROCESS_EXIT_BPF_PROG(tpfn, hash_map)                                                \
    __s32 __##tpfn(void *ctx) {                                                              \
        __u32 pid = xmonitor_get_pid();                                                      \
        char  comm[TASK_COMM_LEN];                                                           \
        bpf_get_current_comm(&comm, sizeof(comm));                                           \
                                                                                             \
        __s32 ret = bpf_map_delete_elem(&hash_map, &pid);                                    \
        if (0 == ret) {                                                                      \
            struct task_struct *task =                                                       \
                (struct task_struct *)bpf_get_current_task();                                \
            __s32 exit_code;                                                                 \
            bpf_probe_read_kernel(&exit_code, sizeof(exit_code),                             \
                                  &task->exit_code);                                         \
                                                                                             \
            printk(                                                                          \
                "xmonitor pcomm: '%s' pid: %d exit_code: %d. remove element from hash_map.", \
                comm, pid, exit_code);                                                       \
        }                                                                                    \
        return 0;                                                                            \
    }

static __always_inline void xmonitor_update_u64(__u64 *res, __u64 value) {
    __sync_fetch_and_add(res, value);
    if ((0xFFFFFFFFFFFFFFFF - *res) <= value) {
        *res = value;
    }
}

static __always_inline __u32 xmonitor_get_pid() {
    return bpf_get_current_pid_tgid() >> 32;
}

static __always_inline __s32 xmonitor_get_tid() {
    return bpf_get_current_pid_tgid() & 0xFFFFFFFF;
}

// static __always_inline __s32 xmonitor_get_comm(char (*p)[TASK_COMM_LEN]) {
//     struct task_struct *task = (struct task_struct *)bpf_get_current_task();
//     return bpf_core_read_str(&p, TASK_COMM_LEN, &task->comm);
// }