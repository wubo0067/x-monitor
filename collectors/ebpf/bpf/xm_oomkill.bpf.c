/*
 * @Author: CALM.WU
 * @Date: 2022-02-24 11:44:58
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-02-24 14:27:28
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
#include "xm_bpf_common.h"

#define XM_EBPF_OOMKILL_MAX_ENTRIES 64

struct oomkill_info {
    pid_t pid;
    char  comm[TASK_COMM_LEN];
};

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_HASH);
    __uint(max_entries, XM_EBPF_OOMKILL_MAX_ENTRIES);
    __type(key, struct oomkill_info);
    __type(value, __u8);
} xm_oomkill SEC(".maps");

SEC("tp/oom/mark_victim")
__s32 BPF_PROG(xm_oom_mark_victim, struct trace_event_raw_mark_victim *evt) {
    struct oomkill_info info = {};
    __u8                val = 0;

    bpf_get_current_comm(&info.comm, sizeof(info.comm));
    info.pid = (pid_t)(evt->pid);

    bpf_map_update_elem(&xm_oomkill, &info, &val, BPF_ANY);

    return 0;
}

char _license[] SEC("license") = "GPL";
