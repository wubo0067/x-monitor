/*
 * @Author: CALM.WU
 * @Date: 2022-02-04 13:50:05
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-02-04 16:22:23
 */

#include <vmlinux.h>
#include "common.h"
#include "bootstrap.h"

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 8192);
    __type(key, pid_t);
    __type(value, __u64);
} exec_start SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} rb SEC(".maps");

SEC("tp/sched/sched_process_exec")
__s32 handle_exec(struct trace_event_raw_sched_process_exec *ctx) {
    struct task_struct *task;
    __u16               fname_off;
    struct bs_event *   e;
    pid_t               pid;
    __u64               ts;

    pid = xmonitor_get_pid();
    ts  = bpf_ktime_get_ns();
    bpf_map_update_elem(&exec_start, &pid, &ts, BPF_ANY);

    e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e) {
        return 0;
    }

    // 填写bs_event
    task          = (struct task_struct *)bpf_get_current_task();
    e->exit_event = false;
    e->pid        = pid;
    e->ppid       = BPF_CORE_READ(task, real_parent, tgid);
    bpf_get_current_comm(&e->comm, sizeof(e->comm));

    fname_off = ctx->__data_loc_filename & 0xFFFF;
    bpf_probe_read_str(&e->filename, sizeof(e->filename), (void *)ctx + fname_off);

    bpf_ringbuf_submit(e, 0);

    return 0;
}

SEC("tp/sched/sched_process_exit")
__s32 handle_exit(struct trace_event_raw_sched_process_template *ctx) {
    struct task_struct *task;
    struct bs_event *   e;
    pid_t               pid, tid;
    __u64               id, ts, *start_ns, duration_ns = 0;

    pid = xmonitor_get_pid();
    tid = xmonitor_get_tid();

    // ignore thread exits
    if (pid != tid) {
        return 0;
    }

    start_ns = bpf_map_lookup_elem(&exec_start, &pid);
    if (start_ns) {
        duration_ns = bpf_ktime_get_ns() - *start_ns;
    }

    bpf_map_delete_elem(&exec_start, &pid);

    e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e) {
        return 0;
    }

    task           = (struct task_struct *)bpf_get_current_task();
    e->exit_event  = true;
    e->duration_ns = duration_ns;
    e->pid         = pid;
    e->ppid        = BPF_CORE_READ(task, real_parent, tgid);
    e->exit_code   = (BPF_CORE_READ(task, exit_code) >> 8) & 0xff;
    bpf_get_current_comm(&e->comm, sizeof(e->comm));

    bpf_ringbuf_submit(e, 0);
    return 0;
}
