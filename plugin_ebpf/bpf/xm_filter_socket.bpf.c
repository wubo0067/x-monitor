/*
 * @Author: CALM.WU
 * @Date: 2022-08-11 11:01:21
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2025-10-11 15:10:36
 */

#include <vmlinux.h>
#include "../bpf_and_user.h"
#include "xm_bpf_helpers_common.h"
#include "xm_bpf_helpers_maps.h"
#include "xm_bpf_helpers_net.h"

// 用来设置尾调函数句柄的 map
BPF_PROG_ARRAY(xm_filter_sock_jmp_table, 8);

#define PARSE_VLAN 1
#define PARSE_MPLS 2
#define PARSE_IP   3
#define PARSE_IPV6 4

struct flow_key_record {
	__be32 src; // BIG
		// ENDIAN，UDP/TCP/IP
		// 协议规定：把接收到的第一个字节当作高位字节看待
	__be32 dst;
	union {
		__be32 ports;
		__be16 port16[2];
	};
	__u32 ip_proto;
};

static inline void parse_eth_proto(struct __sk_buff *skb, __u16 eth_proto)
{
	switch (eth_proto) {
	case ETH_P_8021Q:
	case ETH_P_8021AD:
		bpf_tail_call(skb, &xm_filter_sock_jmp_table, PARSE_VLAN);
		break;
	case ETH_P_MPLS_UC:
	case ETH_P_MPLS_MC:
		bpf_tail_call(skb, &xm_filter_sock_jmp_table, PARSE_MPLS);
		break;
	case ETH_P_IP:
		bpf_tail_call(skb, &xm_filter_sock_jmp_table, PARSE_IP);
		break;
	case ETH_P_IPV6:
		bpf_tail_call(skb, &xm_filter_sock_jmp_table, PARSE_IPV6);
		break;
	}
}

SEC("socket/1")
__s32 xm_parse_vlan(struct __sk_buff *skb)
{
	return 0;
}

SEC("socket/2")
__s32 xm_parse_mpls(struct __sk_buff *skb)
{
	return 0;
}

SEC("socket/3")
__s32 xm_parse_ip(struct __sk_buff *skb)
{
	return 0;
}

SEC("socket/4")
__s32 xm_parse_ipv6(struct __sk_buff *skb)
{
	return 0;
}

// 用来过滤链路层的数据包 https://eunomia.dev/zh/tutorials/23-http/
SEC("socket/0")
__s32 xm_main_socket_prog(struct __sk_buff *skb)
{
	__u32 nhoff = ETH_HLEN;

	// 读取 以太帧 eth_hdr 信息
	struct ethhdr eth_hdr;
	if (bpf_skb_load_bytes(skb, 0, &eth_hdr, sizeof(eth_hdr)) < 0) {
		bpf_printk("xmonitor read eth_hdr failed.");
	} else {
		__u16 eth_proto = bpf_ntohs(eth_hdr.h_proto);
		skb->cb[0] = nhoff;
		parse_eth_proto(skb, eth_proto);
	}
	return 0;
}

// ** GPL
char LICENSE[] SEC("license") = "Dual BSD/GPL";
