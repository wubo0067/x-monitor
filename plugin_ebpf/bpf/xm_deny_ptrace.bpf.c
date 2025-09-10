/*
 * @Author: CALM.WU
 * @Date: 2022-09-13 10:45:37
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2025-09-10 14:23:44
 */

#include <vmlinux.h>
#include "xm_bpf_helpers_common.h"

#include "../bpf_and_user.h"

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 256 * 1024);
} xm_dp_evt_rb SEC(".maps");

struct syscalls_enter_ptrace_args {
	long long pad;
	__s32 syscall_nr;
	__s64 request;
	__s64 pid;
	__u64 addr;
	__u64 data;
};

// 拒绝 ptrace 的 pid
const volatile __s64 target_pid = 0;

SEC("tracepoint/syscalls/sys_enter_ptrace")
__s32 xm_bpf_deny_ptrace(struct syscalls_enter_ptrace_args *ctx)
{
	// long ret = 0;
	// // strace 的 pid
	// pid_t pid = __xm_get_pid();

	// if (target_pid != 0) {
	//     if (ctx->pid != target_pid) {
	//         return 0;
	//     }
	// }

	// bpf_printk("x-monitor deny pid:'%ld' ptrace to target pid:'%ld'", pid,
	//            target_pid);
	// ret = bpf_send_signal(9);

	// // log event
	// struct xm_ebpf_event *evt = NULL;
	// evt = bpf_ringbuf_reserve(&xm_dp_evt_rb, sizeof(*evt), 0);
	// if (evt) {
	//     evt->pid = pid;
	//     evt->ppid = 0;
	//     evt->err_code = ret;
	//     bpf_get_current_comm(&evt->comm, sizeof(evt->comm));
	//     bpf_ringbuf_submit(evt, 0);
	// }
	return 0;
}

char _license[] SEC("license") = "GPL";
