/*
 * @Author: CALM.WU 
 * @Date: 2021-11-02 14:01:24 
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-11-25 16:30:19
 */
#include "xmbpf_helper.h"

// struct cachestat_key {
//     __u32 pid; // 进程ID
// };

struct cachestat_value {
    __u64 add_to_page_cache_lru;
    __u64 ip_add_to_page_cache; // IP寄存器的值
    __u64 mark_page_accessed;
    __u64 ip_mark_page_accessed; // IP寄存器的值
    __u64 account_page_dirtied;
    __u64 ip_account_page_dirtied; // IP寄存器的值
    __u64 mark_buffer_dirty;
    __u64 ip_mark_buffer_dirty; // IP寄存器的值
    __u32 uid;                  // 用户ID
    char  comm[TASK_COMM_LEN];  // 进程名
};

#define CACHE_STATE_MAX_SIZE 1024

#if (LINUX_VERSION_CODE > KERNEL_VERSION(5, 4, 14))

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, CACHE_STATE_MAX_SIZE);
    __type(key, __u32);
    __type(value, struct cachestat_value);
} cachestat_map SEC(".maps");

#else

struct bpf_map_def SEC("maps") cachestat_map = {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0))
    .type        = BPF_MAP_TYPE_HASH,
#else
    .type = BPF_MAP_TYPE_PERCPU_HASH,
#endif
    .key_size    = sizeof(__u32),
    .value_size  = sizeof(struct cachestat_value),
    .max_entries = CACHE_STATE_MAX_SIZE,
};

#endif

#define FILTER_SYMBOL_COUNT 4

// static char __filter_pcomm[FILTER_PCOMM_COUNT][16] = {
//     "ksmtuned",
// };

static const struct ftiler_symbol {
    char  name[32];
    __s32 length;
} symbols[FILTER_SYMBOL_COUNT] = {
    {
        .name   = "ksmtuned",
        .length = 8,
    },
    {
        .name   = "pmie_check",
        .length = 10,
    },
    {
        .name   = "polkitd",
        .length = 7,
    },
    {
        .name   = "pmdadm",
        .length = 6,
    },
};

static __s32 filter_out_symbol(char comm[TASK_COMM_LEN])
{
    __s32 i, j, find;

    for (i = 0; i < FILTER_SYMBOL_COUNT; i++) {
        find = 1;
        for (j = 0; j < symbols[i].length && j < TASK_COMM_LEN; j++) {
            if (comm[j] != symbols[i].name[j]) {
                find = 0;
                break;
            }
        }
        if (find) {
            printk("filter out '%s'\n", comm);
            break;
        }
    }
    return find;
#if 0
    return 0;
#endif
}

/************************************************************************************
 *
 *                                   Probe Section
 *
 ***********************************************************************************/
SEC("kprobe/add_to_page_cache_lru")
__s32 xmonitor_bpf_add_to_page_cache_lru(struct pt_regs *ctx)
{
    __s32                   ret = 0;
    __u32                   pid;
    struct cachestat_value *fill;

    // 得到进程ID
    pid = xmonitor_get_pid();

    fill = bpf_map_lookup_elem(&cachestat_map, &pid);
    if (fill) {
        xmonitor_update_u64(&fill->add_to_page_cache_lru, 1);
        // 有可能因为execve导致comm改变
        bpf_get_current_comm(&fill->comm, sizeof(fill->comm));
        printk("xmonitor update add_to_page_cache_lru pcomm: '%s' pid: %d value: %lu",
               fill->comm, pid, fill->add_to_page_cache_lru);

    } else {
        struct cachestat_value init_value = {
            .add_to_page_cache_lru = 1,
        };
        init_value.ip_add_to_page_cache = PT_REGS_IP(ctx);
        init_value.uid                  = (uid_t)bpf_get_current_uid_gid();
        // 得到应用程序名
        bpf_get_current_comm(&init_value.comm, sizeof(init_value.comm));

        ret =
            bpf_map_update_elem(&cachestat_map, &pid, &init_value, BPF_NOEXIST);
        if (0 == ret) {
            printk(
                "xmonitor add_to_page_cache_lru add new pcomm: '%s' pid: %d successed",
                init_value.comm, pid);
        }
    }
    return 0;
}

SEC("kprobe/mark_page_accessed")
__s32 xmonitor_bpf_mark_page_accessed(struct pt_regs *ctx)
{
    __s32                   ret = 0;
    __u32                   pid;
    struct cachestat_value *fill;

    // 得到进程ID
    pid = xmonitor_get_pid();

    fill = bpf_map_lookup_elem(&cachestat_map, &pid);
    if (fill) {
        xmonitor_update_u64(&fill->mark_page_accessed, 1);
        // 有可能因为execve导致comm改变
        bpf_get_current_comm(&fill->comm, sizeof(fill->comm));
        printk("xmonitor update mark_page_accessed pcomm: '%s' pid: %d value: %lu", fill->comm,
               pid, fill->mark_page_accessed);

    } else {
        struct cachestat_value init_value = {
            .mark_page_accessed = 1,
        };
        init_value.ip_mark_page_accessed = PT_REGS_IP(ctx);
        init_value.uid                   = (uid_t)bpf_get_current_uid_gid();
        // 得到应用程序名
        bpf_get_current_comm(&init_value.comm, sizeof(init_value.comm));

        ret =
            bpf_map_update_elem(&cachestat_map, &pid, &init_value, BPF_NOEXIST);
        if (0 == ret) {
            printk(
                "xmonitor mark_page_accessed add new pcomm: '%s' pid: %d successed",
                init_value.comm, pid);
        }
    }
    return 0;
}

SEC("kprobe/account_page_dirtied")
__s32 xmonitor_bpf_account_page_dirtied(struct pt_regs *ctx)
{
    __s32                   ret = 0;
    __u32                   pid;
    struct cachestat_value *fill;

    // 得到进程ID
    pid = xmonitor_get_pid();

    fill = bpf_map_lookup_elem(&cachestat_map, &pid);
    if (fill) {
        xmonitor_update_u64(&fill->account_page_dirtied, 1);
        bpf_get_current_comm(&fill->comm, sizeof(fill->comm));
        printk("xmonitor update account_page_dirtied pcomm: '%s' pid: %d value: %lu",
               fill->comm, pid, fill->account_page_dirtied);

    } else {
        struct cachestat_value init_value = {
            .account_page_dirtied = 1,
        };
        init_value.ip_account_page_dirtied = PT_REGS_IP(ctx);
        init_value.uid                     = (uid_t)bpf_get_current_uid_gid();
        // 得到应用程序名
        bpf_get_current_comm(&init_value.comm, sizeof(init_value.comm));

        ret =
            bpf_map_update_elem(&cachestat_map, &pid, &init_value, BPF_NOEXIST);
        if (0 == ret) {
            printk(
                "xmonitor account_page_dirtied add new pcomm: '%s' pid: %d successed",
                init_value.comm, pid);
        }
    }
    return 0;
}

SEC("kprobe/mark_buffer_dirty")
__s32 xmonitor_bpf_mark_buffer_dirty(struct pt_regs *ctx)
{
    __s32                   ret = 0;
    __u32                   pid;
    struct cachestat_value *fill;

    // 得到进程ID
    pid = xmonitor_get_pid();

    fill = bpf_map_lookup_elem(&cachestat_map, &pid);
    if (fill) {
        xmonitor_update_u64(&fill->mark_buffer_dirty, 1);
        // 有可能因为execve导致comm改变
        bpf_get_current_comm(&fill->comm, sizeof(fill->comm));
        printk("xmonitor update mark_buffer_dirty pcomm: '%s' pid: %d value: %lu", fill->comm,
               pid, fill->mark_buffer_dirty);
        //bpf_map_update_elem(&cachestat_map, &pid, fill, BPF_ANY);
    } else {
        struct cachestat_value init_value = {
            .mark_buffer_dirty = 1,
        };
        init_value.ip_mark_buffer_dirty = PT_REGS_IP(ctx);
        init_value.uid                  = (uid_t)bpf_get_current_uid_gid();
        // 得到应用程序名
        bpf_get_current_comm(&init_value.comm, sizeof(init_value.comm));

        ret =
            bpf_map_update_elem(&cachestat_map, &pid, &init_value, BPF_NOEXIST);
        if (0 == ret) {
            printk(
                "xmonitor mark_buffer_dirty add new pcomm: '%s' pid: %d successed",
                init_value.comm, pid);
        }
    }
    return 0;
}

// #define PROG(tpfn)                                                                                \
//     __s32 __##tpfn(void *ctx)                                                                     \
//     {                                                                                             \
//         __s32 pid = xmonitor_get_pid();                                                           \
//         char  comm[TASK_COMM_LEN];                                                                \
//         bpf_get_current_comm(&comm, sizeof(comm));                                                \
//                                                                                                   \
//         __s32 ret = bpf_map_delete_elem(&cachestat_map, &pid);                                    \
//         if (0 == ret) {                                                                           \
//             struct task_struct *task =                                                            \
//                 (struct task_struct *)bpf_get_current_task();                                     \
//             __s32 exit_code;                                                                      \
//             bpf_probe_read_kernel(&exit_code, sizeof(exit_code),                                  \
//                                   &task->exit_code);                                              \
//                                                                                                   \
//             printk(                                                                               \
//                 "xmonitor pcomm: '%s' pid: %d exit_code: %d. remove element from cachestat_map.", \
//                 comm, pid, exit_code);                                                            \
//         }                                                                                         \
//         return 0;                                                                                 \
//     }

SEC("tracepoint/sched/sched_process_exit")
PROCESS_EXIT_BPF_PROG(xmonitor_bpf_cs_sched_process_exit, cachestat_map)

SEC("tracepoint/sched/sched_process_free")
PROCESS_EXIT_BPF_PROG(xmonitor_bpf_cs_sched_process_free, cachestat_map)

char           _license[] SEC("license") = "GPL";
__u32 _version SEC("version")            = LINUX_VERSION_CODE;