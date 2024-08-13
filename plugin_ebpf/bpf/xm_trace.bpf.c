/*
 * @Author: CALM.WU
 * @Date: 2022-12-26 15:12:47
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2024-08-13 16:54:50
 */

// 使用 BPF_PROG_TYPE_BTF_RAW_TRACEPOINT 来追踪进程的系统调用，对于
// btf_raw_tracepoint，使用 BPF_PROG 封装 1: 统计进程的系统调用次数 2:
// 统计进程的系统调用累积耗时 3: 统计进程的系统调用耗时分布 4:
// 过滤进程的系统调用

// https://sysdig.com/blog/the-art-of-writing-ebpf-programs-a-primer/

#include <vmlinux.h>
#include "../bpf_and_user.h"
#include "xm_bpf_helpers_common.h"
#include "xm_bpf_helpers_maps.h"

// 过滤系统调用的进程 id
const volatile pid_t xm_trace_syscall_filter_pid = 0;

// 过滤系统调用的 map
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, __s64); // syscall_nr
	__type(value, __s64); // 返回值
	__uint(max_entries, XM_MAX_FILTER_SYSCALL_COUNT);
} xm_filter_syscalls_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 1 << 24); // 16M
} xm_syscalls_event_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, __u32); // tid
	__type(value, struct syscall_event); // 系统调用事件
	__uint(max_entries, 10000);
} xm_syscalls_record_map SEC(".maps");

// #define PERF_MAX_STACK_DEPTH		127
struct {
	__uint(type, BPF_MAP_TYPE_STACK_TRACE);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, PERF_MAX_STACK_DEPTH * sizeof(__u64));
	__uint(max_entries, 8196);
} xm_syscalls_stack_map SEC(".maps");

const struct syscall_event *unused __attribute__((unused));

// vmlinux.h typedef void (*btf_trace_sys_enter)(void *, struct pt_regs *, long
// int);

SEC("tp_btf/sys_enter")
__s32 BPF_PROG(xm_trace_tp_btf__sys_enter, struct pt_regs *regs,
	       __s64 syscall_nr)
{
	pid_t pid = __xm_get_pid();
	__u32 tid = __xm_get_tid();
	struct syscall_event se;

	// 只过滤指定进程的系统调用
	if (!xm_trace_syscall_filter_pid ||
	    (xm_trace_syscall_filter_pid && pid != xm_trace_syscall_filter_pid))
		return 0;

	// load program: permission denied: invalid indirect read from stack R3 off
	// -56+16 size 48 (67 line(s) omitted)
	// 对于栈变量必须初始化，不然 map 操作在虚拟机校验过程中会报错
	__builtin_memset(&se, 0, sizeof(struct syscall_event));
	se.call_start_ns = bpf_ktime_get_ns();
	se.syscall_nr = syscall_nr;
	se.pid = pid;
	se.tid = tid;

	bpf_map_update_elem(&xm_syscalls_record_map, &tid, &se, BPF_ANY);

	// if (syscall_nr == 267) {
	//     bpf_printk("xm_trace_syscalls pid:%d, tid:%d sys_enter
	//     sys_readlinkat",
	//                pid, tid);
	// }

	return 0;
}

// typedef void (*btf_trace_sys_exit)(void *, struct pt_regs *, long int);

SEC("tp_btf/sys_exit")
__s32 BPF_PROG(xm_trace_tp_btf__sys_exit, struct pt_regs *regs, __s64 ret)
{
	pid_t pid = __xm_get_pid();
	__u32 tid = __xm_get_tid();

	// 怎么从 sys_exit 中得到 syscall_nr 呢 %ax 寄存器保存的是返回结果，
	// orig_ax 保存的是系统调用号

	__s64 syscall_nr = BPF_CORE_READ(regs, orig_ax);

	if (!xm_trace_syscall_filter_pid ||
	    (xm_trace_syscall_filter_pid && pid != xm_trace_syscall_filter_pid))
		return 0;

	struct syscall_event *se =
		bpf_map_lookup_elem(&xm_syscalls_record_map, &tid);
	if (se) {
		se->call_delay_ns = bpf_ktime_get_ns() - se->call_delay_ns;
		se->syscall_ret = ret;

		struct syscall_event *evt =
			bpf_ringbuf_reserve(&xm_syscalls_event_map,
					    sizeof(struct syscall_event), 0);

		if (!evt) {
			bpf_printk(
				"xm_trace_syscalls pid: %d, tid: %d, syscall_nr: %ld "
				"reserve ringbuf failed",
				pid, tid, syscall_nr);
		} else {
			__builtin_memcpy(evt, se, sizeof(struct syscall_event));
			bpf_ringbuf_submit(evt, 0);
			bpf_map_delete_elem(&xm_syscalls_record_map, &tid);
		}
	}

	// if (syscall_nr == 267) {
	//     bpf_printk("xm_trace_syscalls pid:%d, tid:%d sys_exit
	//     sys_readlinkat",
	//                pid, tid);
	// }

	return 0;
}

// !! 获取的堆栈栈帧地址不对，是不是不能使用 btf_raw_tracepoint，试试
// raw_tracepoint

#define XM_TRACE_KPROBE_PROG(name)                                             \
	__s32 xm_trace_kp__##name(struct pt_regs *ctx)                         \
	{                                                                      \
		pid_t pid = __xm_get_pid();                                    \
		__u32 tid = __xm_get_tid();                                    \
                                                                               \
		if (!xm_trace_syscall_filter_pid ||                            \
		    (xm_trace_syscall_filter_pid &&                            \
		     pid != xm_trace_syscall_filter_pid))                      \
			return 0;                                              \
                                                                               \
		struct syscall_event *se =                                     \
			bpf_map_lookup_elem(&xm_syscalls_record_map, &tid);    \
		if (se) {                                                      \
			__s32 kernel_stack_id =                                \
				bpf_get_stackid(ctx, &xm_syscalls_stack_map,   \
						KERN_STACKID_FLAGS);           \
			__s32 user_stack_id =                                  \
				bpf_get_stackid(ctx, &xm_syscalls_stack_map,   \
						USER_STACKID_FLAGS);           \
			if (kernel_stack_id > 0) {                             \
				se->kernel_stack_id = kernel_stack_id;         \
			}                                                      \
			if (user_stack_id > 0) {                               \
				se->user_stack_id = user_stack_id;             \
			}                                                      \
		}                                                              \
		return 0;                                                      \
	}

// SEC("kprobe/" SYSCALL(sys_close))
// XM_TRACE_KPROBE_PROG(sys_close)

// SEC("kprobe/" SYSCALL(sys_readlinkat))
// XM_TRACE_KPROBE_PROG(sys_readlinkat)

// SEC("kprobe/" SYSCALL(sys_openat))
// XM_TRACE_KPROBE_PROG(sys_openat)
#if 0
/*
TRACE_EVENT_FN(sys_enter,
    TP_PROTO(struct pt_regs *regs, long id),
*/
SEC("raw_tracepoint/sys_enter")
__s32 xm_trace_raw_tp__sys_enter(struct bpf_raw_tracepoint_args *ctx) {
    pid_t pid = __xm_get_pid();
    __u32 tid = __xm_get_tid();
    struct syscall_event se;

    // 只过滤指定进程的系统调用
    if (!xm_trace_syscall_filter_pid
        || (xm_trace_syscall_filter_pid && pid != xm_trace_syscall_filter_pid))
        return 0;

    // load program: permission denied: invalid indirect read from stack R3 off
    // -56+16 size 48 (67 line(s) omitted)
    // 对于栈变量必须初始化，不然 map 操作在虚拟机校验过程中会报错
    __builtin_memset(&se, 0, sizeof(struct syscall_event));
    se.call_start_ns = bpf_ktime_get_ns();
    se.syscall_nr = ctx->args[1];
    se.pid = pid;
    se.tid = tid;

    // __s32 kernel_stack_id =
    //     bpf_get_stackid(ctx, &xm_syscalls_stack_map, KERN_STACKID_FLAGS);
    // __s32 user_stack_id =
    //     bpf_get_stackid(ctx, &xm_syscalls_stack_map, USER_STACKID_FLAGS);
    // if (kernel_stack_id > 0) {
    //     se.kernel_stack_id = kernel_stack_id;
    // }
    // if (user_stack_id > 0) {
    //     se.user_stack_id = user_stack_id;
    // }

    bpf_map_update_elem(&xm_syscalls_record_map, &tid, &se, BPF_ANY);

    /*
    如果是字符串，先读取地址，在读取内容
    if (syscall_id != __NR_openat) {
        char buf[64];
        //bpf_probe_read(&pathname, sizeof(pathname), &regs->si);
        const char *pathname = PT_REGS_PARM2_CORE(regs);
        bpf_probe_read_str(buf, sizeof(buf), pathname);
    }
    */

    return 0;
}

/*
TRACE_EVENT_FN(sys_exit,
    TP_PROTO(struct pt_regs *regs, long ret),
*/
SEC("raw_tracepoint/sys_exit")
__s32 xm_trace_raw_tp__sys_exit(struct bpf_raw_tracepoint_args *ctx) {
    pid_t pid = __xm_get_pid();
    __u32 tid = __xm_get_tid();

    if (!xm_trace_syscall_filter_pid
        || (xm_trace_syscall_filter_pid && pid != xm_trace_syscall_filter_pid))
        return 0;

    struct syscall_event *se =
        bpf_map_lookup_elem(&xm_syscalls_record_map, &tid);
    if (se) {
        se->call_delay_ns = bpf_ktime_get_ns() - se->call_start_ns;
        se->syscall_ret = ctx->args[1];

        struct syscall_event *evt = bpf_ringbuf_reserve(
            &xm_syscalls_event_map, sizeof(struct syscall_event), 0);

        if (!evt) {
            bpf_printk("xm_trace_syscalls pid: %d, tid: %d, syscall_nr: %ld "
                       "reserve ringbuf failed",
                       pid, tid, se->syscall_nr);
        } else {
            __builtin_memcpy(evt, se, sizeof(struct syscall_event));
            bpf_ringbuf_submit(evt, 0);
            bpf_map_delete_elem(&xm_syscalls_record_map, &tid);
        }
    }

    return 0;
}
#endif
char _license[] SEC("license") = "GPL";

/*
root@localhost  /lib/modules/4.18.0/build  nm -A vmlinux|grep
__x64_sys_openat vmlinux:ffffffff82e2b9b0 t _eil_addr___x64_sys_openat
vmlinux:ffffffff8132deb0 T __x64_sys_openat
 ⚡ root@localhost  /lib/modules/4.18.0/build  addr2line -e vmlinux
ffffffff82e2b9b0 /usr/src/kernels/linux-4.18.0-348.7.1.el8_5/fs/open.c:1130

get stack from raw_tracepoint/sys_enter
I0105 16:39:35.254070 2630321 main.go:145] pid:342284, tid:342290,
(248999017.365810 ms) syscall_nr:8 = 0 I0105 16:39:35.254127 2630321
main.go:168] 	0xip[0]: ffffffffc0656e7c016x	entries_lock
[binfmt_misc]+55ac I0105 16:39:35.254159 2630321 main.go:168] 	0xip[1]:
ffffffffbaa0fe0e016x	bpf_get_stackid_raw_tp+4e I0105 16:39:35.254184 2630321
main.go:168] 	0xip[2]: ffffffffc0656e7c016x	entries_lock
[binfmt_misc]+55ac I0105 16:39:35.254204 2630321 main.go:168] 	0xip[3]:
ffffffffbaa0de87016x	bpf_trace_run2+37 ^CI0105 16:39:35.254219 2630321
main.go:168] 	0xip[4]: ffffffffba803f0e016x	syscall_trace_enter+29e I0105
16:39:35.254233 2630321 main.go:168] 	0xip[5]: ffffffffba8043a9016x
do_syscall_64+149 I0105 16:39:35.254256 2630321 main.go:168] 	0xip[6]:
ffffffffbb2000ad016x	entry_SYSCALL_64_after_hwframe+65

使用 bcc trace 命令：trace '__x64_sys_openat' -K -U -T -a -p 342284

dump prog 指令：bpftool prog dump xlated id 209 linum
*/
