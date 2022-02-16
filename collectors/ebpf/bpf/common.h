/*
 * @Author: CALM.WU
 * @Date: 2022-02-04 14:29:03
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-02-10 16:39:17
 */

//#include <linux/bpf.h> // uapi这个头文件和vmlinux.h不兼容啊，类型重复定义
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

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