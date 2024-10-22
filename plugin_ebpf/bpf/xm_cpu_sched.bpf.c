/*
 * @Author: CALM.WU
 * @Date: 2023-03-23 10:30:47
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2024-03-04 15:49:07
 */

// Observation of CPU scheduling status
// 1: 运行队列等待时长，单位微秒
// 2: task off cpu 的时长，单位微秒
// 3: 对 task hang 观察，输出 hang 的 task 信息

#include <vmlinux.h>
#include "../bpf_and_user.h"
#include "xm_bpf_helpers_common.h"
#include "xm_bpf_helpers_net.h"
#include "xm_bpf_helpers_maps.h"
#include "xm_bpf_helpers_math.h"

// **全局变量：过滤条件
// 过滤范围，1:os，2：namespace，3：CGroup，4：PID，5：PGID，暂时不支持 cg
const volatile __s32 __filter_scope_type = 1;
// 范围具体值，例如 pidnsID, pid, pgid，如果 scope_type 为 1，表示范围为整个 os
const volatile __s64 __filter_scope_value = 0;
const volatile __s64 __offcpu_min_duration_nanosecs = 5000000000;
const volatile __s64 __offcpu_max_duration_nanosecs =
	50000000000; // 从离开 cpu 到从新进入 cpu 的时间间隔，单位纳秒
const volatile __s8 __offcpu_task_type = 0; // 0: all，1：user，2：kernel

// **bpf map 定义
// 记录 task_struct 插入 running queue 的时间
BPF_HASH(xm_cs_runqlat_start_map, __u32, __u64, MAX_THREAD_COUNT);
// 记录从插入 running-queue 时间到上 cpu 的等待时间的分布，单位微秒
BPF_HASH(xm_cs_runqlat_hists_map, __u32, struct xm_runqlat_hist, 1);
// 记录 task_struct 从 cpu 调度出去的时间
BPF_HASH(xm_cs_offcpu_start_map, __u32, __u64, MAX_THREAD_COUNT);
// CPU 调度 event 通知
struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 1 << 24); // 16M
} xm_cs_event_ringbuf_map SEC(".maps");

// ** 类型标识，为了 bpf2go 程序生成 golang 类型
static struct xm_runqlat_hist __zero_hist = {
	.slots = { 0 },
};
const enum xm_cpu_sched_evt_type __unused_cs_evt_type __attribute__((unused)) =
	XM_CS_EVT_TYPE_NONE;
// !! Force emitting struct event into the ELF. 切记不能使用 static 做修饰符
const struct xm_cpu_sched_evt_data *__unused_cs_evt __attribute__((unused));

// ** 辅助函数
static __s32 __insert_cs_start_map(struct task_struct *ts,
				   enum xm_cpu_sched_evt_type evt_type)
{
	pid_t tgid = ts->tgid;
	pid_t pid = ts->pid;

	if (__filter_scope_type == 2) {
		// 判断 pid_namespace 是否相同
		__u32 pidns_id = __xm_get_task_pidns(ts);
		if (pidns_id != (pid_t)__filter_scope_value) {
			return 0;
		}
	} else if (__filter_scope_type == 4) {
		// 判断是否指定的进程
		if (tgid != (pid_t)__filter_scope_value) {
			return 0;
		}
	} else if (__filter_scope_type == 5) {
		// 判断是否指定的进程组
		pid_t pgid = __xm_get_task_pgid(ts);
		if (pgid != (pid_t)__filter_scope_value) {
			return 0;
		}
	} else if (__filter_scope_type > 5) {
		return 0;
	}

	// 得到 wakeup 时间，单位纳秒
	__u64 now_ns = bpf_ktime_get_ns();
	if (evt_type == XM_CS_EVT_TYPE_RUNQLAT) {
		// 更新 map，记录 thread wakeup time
		bpf_map_update_elem(&xm_cs_runqlat_start_map, &pid, &now_ns,
				    BPF_ANY);
	} else if (evt_type == XM_CS_EVT_TYPE_OFFCPU) {
		// 更新 map，记录 thread offcpu time
		if (__offcpu_task_type == 0) {
			bpf_map_update_elem(&xm_cs_offcpu_start_map, &pid,
					    &now_ns, BPF_ANY);
		} else if (__offcpu_task_type == 1 &&
			   !__xm_task_is_kthread(ts)) {
			// 只关注用户的 task
			bpf_map_update_elem(&xm_cs_offcpu_start_map, &pid,
					    &now_ns, BPF_ANY);
			// bpf_printk("xm_cs_offcpu_start_map add user pid:%d", pid);
		} else if (__offcpu_task_type == 2 &&
			   __xm_task_is_kthread(ts)) {
			// 只关注内核的 task
			bpf_map_update_elem(&xm_cs_offcpu_start_map, &pid,
					    &now_ns, BPF_ANY);
		}
	}
	return 0;
}

static __s32 __statistics_runqlat(pid_t pid, __u64 now_ns)
{
	struct xm_runqlat_hist *hist = NULL;
	// 查询 map 得到该 task 何时插入 running queue
	__u64 *wakeup_ns = bpf_map_lookup_elem(&xm_cs_runqlat_start_map, &pid);
	if (!wakeup_ns) {
		// wakeup 时间不存在
		if (__filter_scope_type == 1) {
			bpf_printk("xm_ebpf_exporter runqlat pid:%d is not in "
				   "xm_cs_runqlat_start_map.",
				   pid);
		}
		return 0;
	} else {
		// 计算插入 running queue 到被选中上 cpu 的时间差，单位纳秒
		__s64 wakeup_to_run_duration_us = (__s64)(now_ns - *wakeup_ns);
		if (wakeup_to_run_duration_us < 0)
			goto cleanup;

		__u32 hkey = -1;
		hist = (struct xm_runqlat_hist *)__xm_bpf_map_lookup_or_try_init(
			&xm_cs_runqlat_hists_map, &hkey, &__zero_hist);
		if (!hist) {
			bpf_printk("xm_ebpf_exporter runqlat lookup or init "
				   "xm_runqlat_hists_map failed.");
			goto cleanup;
		}
		// 时间换算为微秒
		wakeup_to_run_duration_us = wakeup_to_run_duration_us / 1000U;
		// 计算 2 的对数，求出在哪个 slot, 总共 26 个槽位，1---2 的 25 次方
		// !! I found out the reason, is because of slot definition. "u32 slot"
		// !! doesn't work, u64 works, this is very strange.
		__u64 slot = __xm_log2l(wakeup_to_run_duration_us);
		// 超过最大槽位，放到最后一个槽位
		if (slot >= XM_RUNQLAT_MAX_SLOTS) {
			slot = XM_RUNQLAT_MAX_SLOTS - 1;
		}
		__sync_fetch_and_add(&hist->slots[slot], 1);
	}

cleanup:
	// 从 map 中删除该 task
	bpf_map_delete_elem(&xm_cs_runqlat_start_map, &pid);

	return 0;
}

static __s32 __process_return_to_cpu_task(struct task_struct *ts, __u64 now_ns)
{
	pid_t pid = ts->pid;
	pid_t tgid = ts->tgid;

	__u64 *offcpu_ns = bpf_map_lookup_elem(&xm_cs_offcpu_start_map, &pid);
	if (!offcpu_ns) {
		return 0;
	}

	__s64 offcpu_duration = (__s64)(now_ns - *offcpu_ns);
	if (offcpu_duration <= 0) {
		goto cleanup;
	}

	if (offcpu_duration >= __offcpu_min_duration_nanosecs &&
	    offcpu_duration <= __offcpu_max_duration_nanosecs) {
		// 如果回归超时，生成事件，通过 ringbuf 发送
		struct xm_cpu_sched_evt_data *evt = bpf_ringbuf_reserve(
			&xm_cs_event_ringbuf_map,
			sizeof(struct xm_cpu_sched_evt_data), 0);
		//__builtin_memset(evt, 0, sizeof(*evt));
		if (evt) {
			evt->evt_type = XM_CS_EVT_TYPE_OFFCPU;
			evt->tid = pid;
			evt->pid = tgid;
			evt->offcpu_duration_millsecs =
				offcpu_duration / 1000000U;
			bpf_probe_read_str(&evt->comm, sizeof(evt->comm),
					   ts->comm);
			// 进程如果 sleep 后重新执行，这里会显示 offcpu duration 睡眠时间
			// bpf_printk("xm_ebpf_exporter pid:%d, comm:'%s', offcpu duration "
			//            "nanosecs:%lld",
			//            evt->pid, evt->comm, offcpu_duration);
			bpf_ringbuf_submit(evt, 0);
		}
	}

cleanup:
	bpf_map_delete_elem(&xm_cs_offcpu_start_map, &pid);
	return 0;
}

// ** ebpf program
// ts 被唤醒，当 task 被插入 cpu 的 runqueue 后，会触发该 tracepoint
// activate_task(rq, p, en_flags); ttwu_do_wakeup(rq, p, wake_flags, rf);
SEC("tp_btf/sched_wakeup")
__s32 BPF_PROG(btf_tracepoint__xm_sched_wakeup, struct task_struct *ts)
{
	// bpf_printk("xm_ebpf_exporter runqlat sched_wakeup pid:%d", ts->pid);
	return __insert_cs_start_map(ts, XM_CS_EVT_TYPE_RUNQLAT);
}

SEC("tp_btf/sched_wakeup_new")
__s32 BPF_PROG(btf_tracepoint__xm_sched_wakeup_new, struct task_struct *ts)
{
	// ts 被唤醒
	return __insert_cs_start_map(ts, XM_CS_EVT_TYPE_RUNQLAT);
}

SEC("tp_btf/sched_switch")
__s32 BPF_PROG(btf_tracepoint__xm_sched_switch, bool preempt,
	       struct task_struct *prev, struct task_struct *next)
{
	// 调度出 cpu 的 task，如果状态还是 RUNNING，继续插入 wakeup
	// map，算成一种重新唤醒
	__s64 prev_task_state = __xm_get_task_state(prev);
	if (prev_task_state == TASK_RUNNING) {
		__insert_cs_start_map(prev, XM_CS_EVT_TYPE_RUNQLAT);
	}

	pid_t prev_pid = prev->pid;
	if (prev->exit_state != 0 ||
	    (0 != (prev_task_state &
		   (TASK_DEAD | TASK_WAKEKILL | __TASK_STOPPED)))) {
		bpf_map_delete_elem(&xm_cs_offcpu_start_map, &prev_pid);
		bpf_map_delete_elem(&xm_cs_runqlat_start_map, &prev_pid);
		return 0;
	}

	// bpf_printk("btf_tracepoint__xm_sched_switch prev_task pid:%d, state:%lx,
	// "
	//            "exit_state:%lx",
	//            prev_pid, prev_task_state, prev->exit_state);

	// 因为是 raw BTF tracepoint，所以可以直接使用内核结构
	// next 是被选中即将在 cpu 上执行的 task
	// prev 是被调度出去的 task
	__u64 now_ns = bpf_ktime_get_ns();
	pid_t next_pid = next->pid;

	// 记录离开 cpu 的 task 的时间
	__insert_cs_start_map(prev, XM_CS_EVT_TYPE_OFFCPU);

	if (next_pid != 0) {
		__statistics_runqlat(next_pid, now_ns);
		__process_return_to_cpu_task(next, now_ns);
	}

	return 0;
}

/*
bpftrace -e 'tracepoint:sched:sched_process_hang { printf("%s, pid:%d\n", args->comm, args->pid); }'
Attaching 1 probe...
hung_task_threa, pid:12907
hung_task_threa, pid:12909
hung_task_threa, pid:12911
hung_task_threa, pid:12912
hung_task_threa, pid:12913
*/

SEC("tp_btf/sched_process_hang")
__s32 BPF_PROG(btf_tracepoint__xm_sched_process_hang, struct task_struct *ts)
{
	pid_t pid = ts->pid;
	pid_t tgid = ts->tgid;

	struct xm_cpu_sched_evt_data *evt =
		bpf_ringbuf_reserve(&xm_cs_event_ringbuf_map,
				    sizeof(struct xm_cpu_sched_evt_data), 0);
	if (!evt) {
		bpf_printk(
			"btf_tracepoint__xm_sched_process_hang. reserving ringbuf "
			"for pid:%d, tgid:%d failed",
			pid, tgid);
	} else {
		evt->evt_type = XM_CS_EVT_TYPE_HUNG_TASK;
		evt->tid = pid;
		evt->pid = tgid;
		bpf_probe_read_str(&evt->comm, sizeof(evt->comm), ts->comm);
		bpf_printk(
			"btf_tracepoint__xm_sched_process_hang. pid:%d, comm:'%s' is hung task",
			evt->pid, evt->comm);
		bpf_ringbuf_submit(evt, 0);
	}

	return 0;
}

// task 被释放
SEC("tp_btf/sched_process_exit")
__s32 BPF_PROG(btf_tracepoint__xm_sched_process_exit, struct task_struct *ts)
{
	pid_t pid = ts->pid;
	pid_t tgid = ts->tgid;

	struct xm_cpu_sched_evt_data *evt =
		bpf_ringbuf_reserve(&xm_cs_event_ringbuf_map,
				    sizeof(struct xm_cpu_sched_evt_data), 0);
	if (!evt) {
		bpf_printk(
			"btf_tracepoint__xm_sched_process_exit reserving ringbuf "
			"for pid:%d, tgid:%d failed",
			pid, tgid);
	} else {
		evt->evt_type = XM_CS_EVT_TYPE_PROCESS_EXIT;
		evt->tid = pid;
		evt->pid = tgid;
		bpf_probe_read_str(&evt->comm, sizeof(evt->comm), ts->comm);
		bpf_ringbuf_submit(evt, 0);
	}

	// 如果 task 被调度处 cpu 后结束生命周期，从 offcpu map 中删除
	bpf_map_delete_elem(&xm_cs_offcpu_start_map, &pid);
	bpf_map_delete_elem(&xm_cs_runqlat_start_map, &pid);

	return 0;
}

// ** GPL
char LICENSE[] SEC("license") = "Dual BSD/GPL";