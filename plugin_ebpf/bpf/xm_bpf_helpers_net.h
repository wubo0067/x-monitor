/*
 * @Author: CALM.WU
 * @Date: 2022-02-10 16:37:40
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2025-09-26 17:53:34
 */
#pragma once

#include <vmlinux.h>
#include <bpf/bpf_endian.h>

#define AF_LOCAL       1 /* POSIX name for AF_UNIX	*/
#define AF_INET	       2 /* Internet IP Protocol 	*/
#define AF_INET6       10 /* IP version 6			*/
#define IPPROTO_ICMPV6 58 /* ICMPv6			*/

#ifndef ETH_HLEN
#define ETH_HLEN 14 /* Total octets in header.	 */
#endif

#ifndef ETH_P_8021Q
#define ETH_P_8021Q 0x8100 /* 802.1Q VLAN Extended Header  */
#endif

#ifndef ETH_P_8021AD
#define ETH_P_8021AD 0x88A8 /* 802.1ad Service VLAN */
#endif

#ifndef ETH_P_IP
#define ETH_P_IP 0x0800 /* Internet Protocol packet */
#endif

#ifndef ETH_P_IPV6
#define ETH_P_IPV6 0x86DD /* Internet Protocol Version 6 packet */
#endif

#define ETH_P_MPLS_UC 0x8847 /* MPLS Unicast traffic		*/
#define ETH_P_MPLS_MC 0x8848 /* MPLS Multicast traffic	*/

#define IP_CSUM_OFF  offsetof(struct iphdr, check)
#define IP_DST_OFF   offsetof(struct iphdr, daddr)
#define IP_SRC_OFF   offsetof(struct iphdr, saddr)
#define IP_PROTO_OFF offsetof(struct iphdr, protocol)
#define TCP_CSUM_OFF offsetof(struct tcphdr, check)
#define UDP_CSUM_OFF offsetof(struct udphdr, check)

#define cursor_advance(_cursor, _len)                                          \
	({                                                                     \
		void *_tmp = _cursor;                                          \
		_cursor += _len;                                               \
		_tmp;                                                          \
	})

struct hdr_cursor {
	void *pos;
};

static __always_inline __s32 __xm_proto_is_vlan(__u16 h_proto)
{
	return !!(h_proto == bpf_htons(ETH_P_8021Q) ||
		  h_proto == bpf_htons(ETH_P_8021AD));
}

// 返回 IP 包承载的具体协议类型，tcp、udp、icmp 等
static __u8 __xm_parse_ip4hdr(struct hdr_cursor *nh, void *data_end,
			      struct iphdr **iphdr)
{
	struct iphdr *iph = nh->pos;
	int hdrsize;
	const __u8 ERROR_CODE = 255; // 明确的错误码，对应 -1 的无符号转换

	if ((void *)(iph + 1) > data_end)
		return ERROR_CODE;

	hdrsize = iph->ihl * 4;
	/* Sanity check packet field is valid */
	if (hdrsize < sizeof(*iph))
		return ERROR_CODE;

	/* Variable-length IPv4 header, need to use byte-based arithmetic */
	if (nh->pos + hdrsize > data_end)
		return ERROR_CODE;

	nh->pos += hdrsize;
	*iphdr = iph;

	return iph->protocol;
}

static __u8 __xm_parse_ip6hdr(struct hdr_cursor *nh, void *data_end,
			      struct ipv6hdr **ip6hdr)
{
	struct ipv6hdr *ip6h = nh->pos;
	const __u8 ERROR_CODE = 255; // 明确的错误码，对应 -1 的无符号转换

	/* Pointer-arithmetic bounds check; pointer +1 points to after end of
     * thing being pointed to. We will be using this style in the remainder
     * of the tutorial.
     */
	if ((void *)(ip6h + 1) > data_end)
		return ERROR_CODE;

	nh->pos = ip6h + 1;
	*ip6hdr = ip6h;

	return ip6h->nexthdr;
}
static __s32 __xm_get_dport(void *trans_data, void *data_end, __u8 protocol)
{
	struct tcphdr *th;
	struct udphdr *uh;

	/* 检查输入指针有效性 */
	if (!trans_data || !data_end)
		return -1;

	switch (protocol) {
	case IPPROTO_TCP:
		th = (struct tcphdr *)trans_data;
		if ((void *)(th + 1) > data_end)
			return -1;
		return th->dest;
	case IPPROTO_UDP:
		uh = (struct udphdr *)trans_data;
		if ((void *)(uh + 1) > data_end)
			return -1;
		return uh->dest;
	default:
		return -1;
	}
}

// 解析包头得到 ethertype
static bool __xm_parse_eth(struct ethhdr *eth, void *data_end, __u16 *eth_type)
{
	__u64 offset;

	if (!eth || !eth_type || !data_end)
		return false;

	offset = sizeof(*eth);
	if ((void *)eth + offset > data_end)
		return false;

	*eth_type = eth->h_proto;
	return true;
}

static __s32 __xm_get_ip(struct sk_buff *skb)
{
	char *hdr_hdr;
	__u16 mac_hdr;
	__u16 net_hdr;

	bpf_core_read(&hdr_hdr, sizeof(hdr_hdr), &skb->head);
	bpf_core_read(&mac_hdr, sizeof(mac_hdr), &skb->mac_header);
	bpf_core_read(&net_hdr, sizeof(net_hdr), &skb->network_header);

	if (net_hdr == 0) {
		net_hdr = mac_hdr + 14 /* MAC header size */;
	}

	char *ipaddr = hdr_hdr + net_hdr;

	__u8 ip_vers;
	bpf_core_read(&ip_vers, sizeof(ip_vers), ipaddr);
	ip_vers = ip_vers >> 4 & 0xf;

	if (ip_vers == 4) {
		struct iphdr iph_hdr;
		bpf_core_read(&iph_hdr, sizeof(iph_hdr), ipaddr);

		return iph_hdr.daddr;
	}

	return -1;
}

#define XDP_UNKNOWN XDP_REDIRECT + 1
#ifndef XDP_ACTION_MAX
#define XDP_ACTION_MAX (XDP_UNKNOWN + 1)
#endif

static const char *xdp_action_names[XDP_ACTION_MAX] = {
	[XDP_ABORTED] = "XDP_ABORTED",	 [XDP_DROP] = "XDP_DROP",
	[XDP_PASS] = "XDP_PASS",	 [XDP_TX] = "XDP_TX",
	[XDP_REDIRECT] = "XDP_REDIRECT", [XDP_UNKNOWN] = "XDP_UNKNOWN",
};

static const char *__xm_xdp_action_name(uint32_t action)
{
	if (action < XDP_ACTION_MAX)
		return xdp_action_names[action];
	return NULL;
}
