/*
 * @Author: CALM.WU
 * @Date: 2022-02-04 17:00:21
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-02-15 15:18:10
 */

// https://mp.weixin.qq.com/s/fX4HyWdY9AalQLpj5zhoYw

#include <vmlinux.h>
#include <bpf/bpf_endian.h>
#include "common.h"
#include "xdp_stats_kern.h"
#include "parsing_helpers.h"

const volatile char target_name[16] = { 0 };

// ip协议包数量统计
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __type(key, __u32);  // 这里我是用__u8的时候，创建map会报错，libbpf: Error in
                         // bpf_create_map_xattr(ipproto_rx_cnt_map):Invalid argument(-22). Retrying
                         // without BTF.
    __type(value, __u64);
    __uint(max_entries, 256);
} ipproto_rx_cnt_map SEC(".maps");

SEC("xdp") __s32 xdp_prog_simple(struct xdp_md *ctx) {
    // context 对象 struct xdp_md *ctx 中有包数据的 start/end 指针，可用于直接访问包数据
    void *data     = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    __s32 pkt_sz   = data_end - data;

    struct ethhdr *eth    = (struct ethhdr *)data;
    __u64          nh_off = sizeof(*eth);
    // context 对象 struct xdp_md *ctx 中有包数据的 start/end 指针，可用于直接访问包数据
    if (data + nh_off > data_end) {
        return __xdp_stats_record_action(ctx, XDP_DROP);
    }

    // 协议类型
    __u16 h_proto = eth->h_proto;
    if (__proto_is_vlan(h_proto)) {
        // 判断是否是 VLAN 包
        struct vlan_hdr *vhdr;
        vhdr = (struct vlan_hdr *)(data + nh_off);  // vlan是二次打包的
        // 修改数据偏移，跳过vlan hdr，指向实际的数据包头
        nh_off += sizeof(struct vlan_hdr);
        if (data + nh_off > data_end) {
            return XDP_DROP;
        }
        // network-byte-order 这才是实际的协议，被vlan承载的
        h_proto = vhdr->h_vlan_encapsulated_proto;
    }

    bpf_printk("'%s' eth_type:0x%x\n", target_name, bpf_ntohs(h_proto));

    __u32           ip_proto = IPPROTO_UDP;
    struct iphdr *  iphdr;
    struct ipv6hdr *ipv6hdr;

    struct hdr_cursor nh = { .pos = data + nh_off };

    /* Extract L4 protocol */
    if (h_proto == bpf_htons(ETH_P_IP)) {
        // 返回ipv4包承载的协议类型
        ip_proto = (__u32)__parse_ip4hdr(&nh, data_end, &iphdr);
    } else if (h_proto == bpf_htons(ETH_P_IPV6)) {
        // 返回ipv6包承载的协议类型
        ip_proto = (__u32)__parse_ip6hdr(&nh, data_end, &ipv6hdr);
    } else {
        // 其他协议
        ip_proto = 0;
    }

    __u64 *rx_cnt = bpf_map_lookup_elem(&ipproto_rx_cnt_map, &ip_proto);
    if (rx_cnt) {
        *rx_cnt += 1;
    } else {
        __u64 init_value = 1;
        bpf_map_update_elem(&ipproto_rx_cnt_map, &ip_proto, &init_value, BPF_NOEXIST);
    }

    switch (ip_proto) {
    case IPPROTO_ICMP:
        bpf_printk("'%s' ICMP rc_cxt = %lu\n", target_name, (rx_cnt) ? *rx_cnt : 1);
    case IPPROTO_TCP:
        bpf_printk("'%s' TCP rc_cxt = %lu\n", target_name, (rx_cnt) ? *rx_cnt : 1);
    case IPPROTO_UDP:
        bpf_printk("'%s' UDP rc_cxt = %lu\n", target_name, (rx_cnt) ? *rx_cnt : 1);
    default:
        bpf_printk("'%s' OTHER proto: %u rc_cxt = %lu\n", target_name, ip_proto,
                   (rx_cnt) ? *rx_cnt : 1);
    }

    return __xdp_stats_record_action(ctx, XDP_PASS);
}

char _license[] SEC("license") = "GPL";