/*
 * @Author: CALM.WU
 * @Date: 2021-12-16 11:03:20
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-12-16 11:57:12
 */

#include "bpf_legacy.h"  // load_byte
#include "xmbpf_helper.h"
#include <uapi/linux/if_ether.h>
#include <uapi/linux/if_packet.h>
#include <uapi/linux/ip.h>  // struct iphdr

#if (LINUX_VERSION_CODE > KERNEL_VERSION(5, 4, 14))

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(key_size, sizeof(__s32));    // key是协议类型，如TCP、UDP、ICMP等
    __uint(value_size, sizeof(__u64));  // value是协议包的计数
    __uint(max_entries, 256);
} proto_packets_count_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(key_size, sizeof(__s32));    // key是协议类型，如TCP、UDP、ICMP等
    __uint(value_size, sizeof(__u64));  // 入口流量字节数
    __uint(max_entries, 256);
} proto_in_bytes_map SEC(".maps");

#else

struct bpf_map_def SEC(".maps") proto_packets_count_map = {
    .type        = BPF_MAP_TYPE_ARRAY,
    .key_size    = sizeof(__s32),
    .value_size  = sizeof(__u64),
    .max_entries = 256,
};

struct bpf_map_def SEC(".maps") proto_in_bytes_map = {
    .type        = BPF_MAP_TYPE_ARRAY,
    .key_size    = sizeof(__s32),
    .value_size  = sizeof(__u64),
    .max_entries = 256,
};

#endif

// 使用BPF_PROG_TYPE_SOCKET_FILTER类型的程序，抓取网络层的数据包
SEC("socket1") __s32 xmonitor_bpf_proto_statistics(struct __sk_buff *skb) {
    printk("xmonitor skb .len %d .hash %d .protocol %d\n", skb->len, skb->hash, skb->protocol);

    // 使用load_byte从skb_buff结构中获取协议信息，为什么不直接从skb结构中获取呢？
    // ETH_HELN 太网帧的头部长度，ETH_HLEN = 14
    __u32 proto = load_byte(skb, ETH_HLEN + offsetof(struct iphdr, protocol));

    printk("xmonitor load_byte proto %d\n", proto);

    __u64  init_count         = 1;
    __u64 *proto_packer_count = bpf_map_lookup_elem(&proto_packets_count_map, &proto);
    if (proto_packer_count) {
        (*proto_packer_count)++;
    }
    else {
        proto_packer_count = &init_count;
    }
    bpf_map_update_elem(&proto_packets_count_map, &proto, proto_packer_count, BPF_ANY);

    // 统计各种协议入口流量
    if (skb->pkt_type != PACKET_OUTGOING)
        return 0;

    __u64 *proto_in_bytes = bpf_map_lookup_elem(&proto_in_bytes_map, &proto);
    if (proto_in_bytes) {
        // 直接加，不用update map
        xmonitor_update_u64(proto_in_bytes, skb->len);
    }
    else {
        __u64 init_in_bytes = ( __u64 )skb->len;
        bpf_map_update_elem(&proto_in_bytes_map, &proto, &init_in_bytes, BPF_NOEXIST);
    }
    return 0;
}

char  _license[] SEC("license") = "GPL";
__u32 _version SEC("version")   = LINUX_VERSION_CODE;
