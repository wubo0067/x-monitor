/*
 * @Author: CALM.WU
 * @Date: 2022-02-04 13:50:05
 * @Last Modified by: CALM.WUU
 * @Last Modified time: 2022-02-24 14:20:52
 */

#include "xm_bootstrap.h"

// https://mp.weixin.qq.com/s/OiH3qZVRE61yAyQNhVrzDQ

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 8192);
    __type(key, pid_t);
    __type(value, struct sched_process_ev);
} exec_start SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);   // 所有cpu共享的大小，位是字节，必须是内核页大小（ 几乎永远是
                                       // 4096）的倍数，也必须是 2 的幂次。
} rb SEC(".maps");

// const volatile part is important, it marks the variable as read-only for BPF code and user-space
// code
// volatile is necessary to make sure Clang doesn't optimize away the variable altogether, ignoring
// user-space provided value.
const volatile __u64 min_duration_ns = 0;

SEC("tp/sched/sched_process_exec")
__s32 handle_exec(struct trace_event_raw_sched_process_exec *ctx) {
    struct task_struct      *task;
    __u16                    fname_off;
    struct sched_process_ev *sp_ev, *rb_ev;
    pid_t                    pid;

    pid = xmonitor_get_pid();

    sp_ev = bpf_map_lookup_elem(&exec_start, &pid);
    if (!sp_ev) {
        struct sched_process_ev init_value = {
            .pid = pid,
            .exit_event = false,
            .duration_ns = 0,
        };

        task = (struct task_struct *)bpf_get_current_task();

        init_value.ppid = BPF_CORE_READ(task, real_parent, tgid);
        init_value.start_ns = bpf_ktime_get_ns();
        bpf_get_current_comm(&init_value.comm, sizeof(init_value.comm));

        fname_off = ctx->__data_loc_filename & 0xFFFF;
        bpf_core_read_str(&init_value.filename, sizeof(init_value.filename),
                          (void *)ctx + fname_off);

        bpf_map_update_elem(&exec_start, &pid, &init_value, BPF_NOEXIST);

        // 如果设置了min_duration_ns，则不记录exec event
        if (!min_duration_ns) {
            return 0;
        }

        rb_ev = bpf_ringbuf_reserve(&rb, sizeof(*sp_ev), 0);
        if (!rb_ev) {
            return 0;
        }
        memcpy(rb_ev, &init_value, sizeof(*sp_ev));
        bpf_ringbuf_submit(rb_ev, 0);
    }

    return 0;
}

SEC("tp/sched/sched_process_exit")
__s32 handle_exit(struct trace_event_raw_sched_process_template *ctx) {
    struct task_struct      *task;
    struct sched_process_ev *sp_ev, *rb_ev;
    pid_t                    pid, tid;
    __u64                    id, ts, *start_ns, duration_ns = 0;

    pid = xmonitor_get_pid();
    tid = xmonitor_get_tid();

    // ignore thread exits
    if (pid != tid) {
        return 0;
    }

    sp_ev = bpf_map_lookup_elem(&exec_start, &pid);
    if (sp_ev) {
        sp_ev->duration_ns = bpf_ktime_get_ns() - sp_ev->start_ns;

        if (min_duration_ns && sp_ev->duration_ns < min_duration_ns) {
            bpf_map_delete_elem(&exec_start, &pid);
            return 0;
        }

        rb_ev = bpf_ringbuf_reserve(&rb, sizeof(*sp_ev), 0);
        if (!rb_ev) {
            bpf_map_delete_elem(&exec_start, &pid);
            return 0;
        }

        memcpy(rb_ev, sp_ev, sizeof(*sp_ev));
        rb_ev->exit_event = true;
        rb_ev->exit_code = (BPF_CORE_READ(task, exit_code) >> 8) & 0xff;

        bpf_ringbuf_submit(rb_ev, 0);
    }
    return 0;
}

char _license[] SEC("license") = "GPL";
