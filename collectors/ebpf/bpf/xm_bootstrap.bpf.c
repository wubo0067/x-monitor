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
    __type(value, __u64);
} __exec_start_hsmap SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);   // 所有cpu共享的大小，位是字节，必须是内核页大小（ 几乎永远是
                                       // 4096）的倍数，也必须是 2 的幂次。
} __bs_ev_rbmap SEC(".maps");

// const volatile part is important, it marks the variable as read-only for BPF code and user-space
// code
// volatile is necessary to make sure Clang doesn't optimize away the variable altogether, ignoring
// user-space provided value.
const volatile __u64 min_duration_ns = 0;

SEC("tp/sched/sched_process_exec")
__s32 handle_exec(struct trace_event_raw_sched_process_exec *ctx) {
    struct task_struct  *task;
    __u16                fname_off;
    struct bootstrap_ev *bs_ev;
    pid_t                pid;
    __u64                start_ns;
    kuid_t               uid;

    pid = xmonitor_get_pid();
    start_ns = bpf_ktime_get_ns();

    void *value = bpf_map_lookup_elem(&__exec_start_hsmap, &pid);
    if (!value) {
        // 在ringbuffer中保留空间
        bs_ev = bpf_ringbuf_reserve(&__bs_ev_rbmap, sizeof(struct bootstrap_ev), 0);
        if (!bs_ev) {
            return 0;
        }

        bpf_map_update_elem(&__exec_start_hsmap, &pid, &start_ns, BPF_ANY);

        task = (struct task_struct *)bpf_get_current_task();
        bs_ev->exit_event = false;
        bs_ev->pid = pid;
        bs_ev->ppid = BPF_CORE_READ(task, real_parent, tgid);
        bs_ev->uid = BPF_CORE_READ(task, real_cred, uid.val);
        bs_ev->start_ns = start_ns;
        bpf_get_current_comm(&bs_ev->comm, sizeof(bs_ev->comm));
        // 难道高字节是偏移？
        fname_off = ctx->__data_loc_filename & 0xFFFF;
        bpf_core_read_str(&bs_ev->filename, sizeof(bs_ev->filename), (void *)ctx + fname_off);

        bpf_ringbuf_submit(bs_ev, 0);
    }

    return 0;
}

SEC("tp/sched/sched_process_exit")
__s32 handle_exit(struct trace_event_raw_sched_process_template *ctx) {
    struct task_struct  *task;
    struct bootstrap_ev *bs_ev;
    pid_t                pid, tid;
    __u64               *start_ns, duration_ns = 0;

    pid = xmonitor_get_pid();
    tid = xmonitor_get_tid();

    // ignore thread exits
    if (pid != tid) {
        return 0;
    }

    start_ns = bpf_map_lookup_elem(&__exec_start_hsmap, &pid);
    if (start_ns) {
        duration_ns = bpf_ktime_get_ns() - *start_ns;

        if (min_duration_ns && duration_ns < min_duration_ns) {
            bpf_map_delete_elem(&__exec_start_hsmap, &pid);
            return 0;
        }

        bs_ev = bpf_ringbuf_reserve(&__bs_ev_rbmap, sizeof(struct bootstrap_ev), 0);
        if (bs_ev) {
            task = (struct task_struct *)bpf_get_current_task();

            bs_ev->exit_event = true;
            bs_ev->pid = pid;
            bs_ev->duration_ns = duration_ns;
            bs_ev->exit_code = (BPF_CORE_READ(task, exit_code) >> 8) & 0xff;

            bpf_ringbuf_submit(bs_ev, 0);
        }
        bpf_map_delete_elem(&__exec_start_hsmap, &pid);
    }
    return 0;
}

char _license[] SEC("license") = "GPL";
