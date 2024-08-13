/*
 * @Author: CALM.WU
 * @Date: 2022-10-14 11:41:54
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2024-08-13 15:27:35
 */

#include "vmlinux.h"
#include "xm_bpf_helpers_common.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

static __attribute((noinline))
__s32 get_opcode(struct bpf_raw_tracepoint_args *ctx)
{
	return ctx->args[1];
}

SEC("raw_tp/")
__s32 hello(struct bpf_raw_tracepoint_args *ctx)
{
	__s32 opcode = get_opcode(ctx);
	bpf_printk("Syscall: %d", opcode);
	return 0;
}

SEC("kprobe/do_unlinkat")
int BPF_KPROBE(do_unlinkat, int dfd, struct filename *name)
{
	pid_t pid;
	const char *filename;

	pid = bpf_get_current_pid_tgid() >> 32;
	filename = BPF_CORE_READ(name, name);
	bpf_printk("KPROBE ENTRY pid = %d, filename = %s\n", pid, filename);
	return 0;
}

SEC("kretprobe/do_unlinkat")
int BPF_KRETPROBE(do_unlinkat_exit, long ret)
{
	pid_t pid;

	pid = bpf_get_current_pid_tgid() >> 32;
	bpf_printk("KPROBE EXIT: pid = %d, ret = %ld\n", pid, ret);
	return 0;
}
