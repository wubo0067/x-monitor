/*
 * @Author: CALM.WU
 * @Date: 2023-02-22 14:01:14
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2023-12-13 14:44:24
 */

// https://github.com/wubo0067/x-monitor/blob/feature-xm-ebpf-collector/doc/cachestat.md

#include <vmlinux.h>
#include "../bpf_and_user.h"
#include "xm_bpf_helpers_common.h"
#include "xm_bpf_helpers_maps.h"

// extern int LINUX_KERNEL_VERSION __kconfig;

// __s64 xm_cs_total = 0; /* mark_page_accessed() - number of
// mark_buffer_dirty() */
// __s64 xm_cs_misses = 0; /* add_to_page_cache_lru() - number of
//                                   * account_page_dirtied() */
// __s64 xm_cs_mbd = 0; /* total of mark_buffer_dirty events */

BPF_HASH(xm_page_cache_ops_count, __u64, __u64, 4);

#define PRINT_MACRO(x) #x "=" __stringify(x)
#pragma message(PRINT_MACRO(LINUX_VERSION_CODE))

SEC("kprobe/add_to_page_cache_lru")
__s32 BPF_KPROBE(xm_kp_cs_atpcl) {
    // 某些架构上，kprobe 触发时报告的 ip 可能不准确，通常，报告的 IP
    // 可能指向指令之后的位置，而不是指令本身
    // KPROBE_REGS_IP_FIX 通过对捕获的 IP
    // 值进行适当的调整来解决这个问题，调整通常涉及将 IP 值回退一定数量的字节
    // 对于需要精确指令级分析的场景至关重要
    __u64 ip = KPROBE_REGS_IP_FIX(PT_REGS_IP_CORE(ctx));
    __xm_bpf_map_increment(&xm_page_cache_ops_count, &ip, 1);
    //__u64 val = (__u64)__xm_bpf_map_increment(&xm_page_cache_ops_count, &ip,
    // 1);
    // bpf_printk("xm_ebpf_exporter, cachestat add_to_page_cache_lru ip: 0x%lx,
    // "
    //            "val: %lu",
    //            ip, val);
    //__sync_fetch_and_add(&xm_cs_misses, 1);
    return 0;
}

SEC("kprobe/mark_page_accessed")
__s32 BPF_KPROBE(xm_kp_cs_mpa) {
    __u64 ip = PT_REGS_IP(ctx);
    __xm_bpf_map_increment(&xm_page_cache_ops_count, &ip, 1);
    //__sync_fetch_and_add(&xm_cs_total, 1);
    return 0;
}

SEC("kprobe/folio_account_dirtied")
__s32 BPF_KPROBE(xm_kp_cs_fad) {
    __u64 ip = PT_REGS_IP(ctx);
    __xm_bpf_map_increment(&xm_page_cache_ops_count, &ip, 1);
    return 0;
}

// 这个函数在 Linux 5.0 版本中被移除了 2。取而代之的是
// account_page_dirtied_in_file 函数 3。所以，如果你的内核版本是 5.0
// 或更高，你就不能使用 account_page_dirtied 函数了。需要在加载是进行判断
SEC("kprobe/account_page_dirtied")
__s32 BPF_KPROBE(xm_kp_cs_apd) {
    __u64 ip = PT_REGS_IP(ctx);
    __xm_bpf_map_increment(&xm_page_cache_ops_count, &ip, 1);
    //__sync_fetch_and_add(&xm_cs_misses, -1);
    return 0;
}

SEC("kprobe/mark_buffer_dirty")
__s32 BPF_KPROBE(xm_kp_cs_mbd) {
    __u64 ip = PT_REGS_IP(ctx);
    __xm_bpf_map_increment(&xm_page_cache_ops_count, &ip, 1);
    //__u64 val = (__u64)__xm_bpf_map_increment(&xm_page_cache_ops_count, &ip,
    // 1);
    // bpf_printk("xm_ebpf_exporter, cachestat mark_buffer_dirty ip: 0x%lx, val:
    // "
    //            "%lu",
    //            ip, val);
    // __sync_fetch_and_add(&xm_cs_total, -1);
    // __sync_fetch_and_add(&xm_cs_mbd, 1);
    return 0;
}

char _license[] SEC("license") = "GPL";