/*
 * @Author: CALM.WU
 * @Date: 2022-02-15 10:10:44
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-02-15 15:18:03
 */

#pragma once

#include "../common_bpf_user.h"

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __type(key, __u32);
    __type(value, struct xdp_stats_datarec);
    __uint(max_entries, XDP_ACTION_MAX);
} xdp_stats_map SEC(".maps");

static __always_inline __u32 __xdp_stats_record_action(struct xdp_md *ctx, __u32 action) {
    if (action > XDP_ACTION_MAX)
        return XDP_ABORTED;

    struct xdp_stats_datarec *rec = bpf_map_lookup_elem(&xdp_stats_map, &action);
    if (!rec) {
        bpf_printk("xdp_stats_record_action find action:%d failed, so return xdp_aborted.\n",
                   action);
        return XDP_ABORTED;
    }

    rec->rx_packets++;
    rec->rx_bytes += ctx->data_end - ctx->data;

    return action;
}