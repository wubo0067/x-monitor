/*
 * @Author: CALM.WU
 * @Date: 2022-02-24 11:44:58
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2024-02-29 17:25:58
 */

/*
https://lore.kernel.org/linux-mm/20170531163928.GZ27783@dhcp22.suse.cz/
> Trace the following events:
> 1) a process is marked as an oom victim,
> 2) a process is added to the oom reaper list,
> 3) the oom reaper starts reaping process's mm,
> 4) the oom reaper finished reaping,
> 5) the oom reaper skips reaping.
*/

#include <vmlinux.h>
#include "../bpf_and_user.h"
#include "xm_bpf_helpers_common.h"
#include "xm_bpf_helpers_maps.h"

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 1 << 16); // 64k
} xm_oomkill_event_ringbuf_map SEC(".maps");

const struct xm_oomkill_evt_data *__unused_oom_evt __attribute__((unused));

// SEC("tp/oom/mark_victim")
// __s32 BPF_PROG(tracepoint__xm_oom_mark_victim,
//                struct trace_event_raw_mark_victim *evt) {
//     struct xm_oomkill_evt_data info = {};
//     __u8 val = 0;

//     bpf_get_current_comm(&info.comm, sizeof(info.comm));
//     info.pid = (pid_t)(evt->pid);

//     bpf_map_update_elem(&xm_oomkill, &info, &val, BPF_ANY);

//     return 0;
// }

SEC("kprobe/oom_kill_process")
__s32 BPF_KPROBE(kprobe__xm_oom_kill_process, struct oom_control *oc,
		 const char *message)
{
	struct mem_cgroup *memcg;
	struct cgroup *cg;
	struct task_struct *ts;
	struct mm_struct *mm;

	// 通过 ringbuf 分配事件结构
	struct xm_oomkill_evt_data *evt =
		bpf_ringbuf_reserve(&xm_oomkill_event_ringbuf_map,
				    sizeof(struct xm_oomkill_evt_data), 0);
	if (evt) {
		// 读取 oom msg
		bpf_core_read_str(evt->msg, OOM_KILL_MSG_LEN, message);
		// 获取 badness 评估的 points
		evt->points = READ_KERN(oc->chosen_points);
		// 获取 totalpages
		evt->total_pages = READ_KERN(oc->totalpages);
		// 获取 ts 指针
		ts = READ_KERN(oc->chosen);
		// tid
		evt->tid = READ_KERN(ts->pid);
		// pid
		evt->pid = READ_KERN(ts->tgid);
		// 读取 cgroup 指针
		memcg = READ_KERN(oc->memcg);
		if (memcg) {
			// get cgroup
			BPF_CORE_READ_INTO(&cg, memcg, css.cgroup);
			// 获取 cgroup id，这里因为内核版本不同，结构都不通，需要 co-re
			// 的适配
			evt->memcg_id = (__u32)__xm_get_cgroup_id(cg);
			// 获取物理内存分配的 page 数量
			evt->memcg_page_counter =
				BPF_CORE_READ(memcg, memory.usage.counter);
			// bpf_printk("kprobe__xm_oom_kill_process memcg_id:%d, "
			//            "memcg_page_counter:%lu",
			//            evt->memcg_id, evt->memcg_page_counter);
		}
		mm = BPF_CORE_READ(ts, mm);
		// oom_badness 函数计算中 rss 的计算，包括
		// MM_FILEPAGES,MM_ANONPAGES,MM_SHMEMPAGES
		/*
		evt->rss_filepages = BPF_CORE_READ(
			ts, mm, rss_stat.count[MM_FILEPAGES].counter);
		evt->rss_anonpages = BPF_CORE_READ(
			ts, mm, rss_stat.count[MM_ANONPAGES].counter);
		evt->rss_shmepages = BPF_CORE_READ(
			ts, mm, rss_stat.count[MM_SHMEMPAGES].counter);*/
		evt->rss_filepages = __xm_get_mm_rss_count(mm, MM_FILEPAGES);
		evt->rss_anonpages = __xm_get_mm_rss_count(mm, MM_ANONPAGES);
		evt->rss_shmepages = __xm_get_mm_rss_count(mm, MM_SHMEMPAGES);
		// 获取应用程序的 comm
		bpf_core_read_str(evt->comm, sizeof(evt->comm), &ts->comm);
		// bpf_printk("kprobe__xm_oom_kill_process pid:%d, comm:%s, points:%lu "
		//            "was killed "
		//            "because oom",
		//            evt->pid, evt->comm, evt->points);
		bpf_ringbuf_submit(evt, 0);
	} else {
		bpf_printk(
			"kprobe__xm_oom_kill_process bpf_ringbuf_reserve alloc "
			"failed");
	}
	return 0;
}

// ** GPL
char LICENSE[] SEC("license") = "Dual BSD/GPL";
