/*
 * @Author: CALM.WU
 * @Date: 2022-02-04 14:29:03
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-02-24 14:24:28
 */

//#include <linux/bpf.h> // uapi这个头文件和vmlinux.h不兼容啊，类型重复定义
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

#define TASK_COMM_LEN 16

#ifndef memcpy
#define memcpy(dest, src, n) __builtin_memcpy((dest), (src), (n))
#endif

// bpf_probe_read_kernel(&exit_code, sizeof(exit_code), &task->exit_code);

#define PROCESS_EXIT_BPF_PROG(tpfn, hash_map)                                                      \
    __s32 __##tpfn(void *ctx) {                                                                    \
        __u32 pid = xmonitor_get_pid();                                                            \
        char  comm[TASK_COMM_LEN];                                                                 \
        bpf_get_current_comm(&comm, sizeof(comm));                                                 \
                                                                                                   \
        __s32 ret = bpf_map_delete_elem(&hash_map, &pid);                                          \
        if (0 == ret) {                                                                            \
            struct task_struct *task = (struct task_struct *)bpf_get_current_task();               \
            __s32               exit_code = BPF_CORE_READ(task, exit_code);                        \
                                                                                                   \
            bpf_printk(                                                                            \
                "xmonitor pcomm: '%s' pid: %d exit_code: %d. remove element from hash_map.", comm, \
                pid, exit_code);                                                                   \
        }                                                                                          \
        return 0;                                                                                  \
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