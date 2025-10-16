/*
 * @Author: CALM.WU
 * @Date: 2022-02-04 17:00:21
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2025-09-04 10:36:58
 */

// https://mp.weixin.qq.com/s/fX4HyWdY9AalQLpj5zhoYw

#include <vmlinux.h>
#include <bpf/bpf_endian.h>
#include "xm_bpf_helpers_common.h"
#include "xm_xdp_stats_kern.h"
#include "xm_bpf_helpers_net.h"

const volatile char __identify[16] = "xm_xdp";

// ip 协议包数量统计
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__type(key,
	       __u32); // 这里我是用__u8 的时候，创建 map 会报错，libbpf: Error
		// in bpf_create_map_xattr(ipproto_rx_cnt_map):Invalid
		// argument(-22). Retrying without BTF.
	__type(value, __u64);
	__uint(max_entries, 256);
} ipproto_rx_cnt_map SEC(".maps");

SEC("xdp/simple") __s32 xdp_prog_simple(struct xdp_md *ctx)
{
	// context 对象 struct xdp_md *ctx 中有包数据的 start/end
	// 指针，可用于直接访问包数据
	void *data = (void *)(long)ctx->data;
	void *data_end = (void *)(long)ctx->data_end;
	__s32 pkt_sz = data_end - data;

	struct ethhdr *eth = (struct ethhdr *)data;
	__u64 nh_off = sizeof(*eth);
	// context 对象 struct xdp_md *ctx 中有包数据的 start/end
	// 指针，可用于直接访问包数据
	if (data + nh_off > data_end) {
		return __xdp_stats_record_action(ctx, XDP_DROP);
	}

	// 表示以太网帧所承载的上层协议类型，定义在<linux/if_ether.h>，以 ETH_P_*宏表示
	/*
		宏定义 (bpf_htons 转换后)	十六进制值	协议名称	描述
		bpf_htons(ETH_P_IP)	0x0800	IPv4	互联网协议第四版
		bpf_htons(ETH_P_IPV6)	0x86DD	IPv6	互联网协议第六版
		bpf_htons(ETH_P_ARP)	0x0806	ARP	地址解析协议
		bpf_htons(ETH_P_RARP)	0x8035	RARP	反向地址解析协议
		bpf_htons(ETH_P_8021Q)	0x8100	802.1Q VLAN	虚拟局域网标签
		bpf_htons(ETH_P_8021AD)	0x88A8	802.1ad (QinQ)	双层 VLAN 标签
		bpf_htons(ETH_P_PPP_DISC)	0x8863	PPPoE Discovery	PPPoE 发现阶段
		bpf_htons(ETH_P_PPP_SES)	0x8864	PPPoE Session	PPPoE 会话阶段
		bpf_htons(ETH_P_MPLS_UC)	0x8847	MPLS Unicast	多协议标签交换（单播）
		bpf_htons(ETH_P_MPLS_MC)	0x8848	MPLS Multicast	多协议标签交换（多播）
		bpf_htons(ETH_P_LLDP)	0x88CC	LLDP	链路层发现协议
		bpf_htons(ETH_P_EAPOL)	0x888E	EAP over LAN	局域网上的可扩展认证协议
	*/
	__u16 h_proto = eth->h_proto;
	if (__xm_proto_is_vlan(h_proto)) {
		// 判断是否是 VLAN 包
		struct vlan_hdr *vhdr;
		vhdr = (struct vlan_hdr *)(data + nh_off); // vlan 是二次打包的
		// 修改数据偏移，跳过 vlan hdr，指向实际的数据包头
		nh_off += sizeof(struct vlan_hdr);
		if (data + nh_off > data_end) {
			return XDP_DROP;
		}
		// network-byte-order 这才是实际的协议，被 vlan 承载的
		h_proto = vhdr->h_vlan_encapsulated_proto;
	}

	__u32 ip_proto = IPPROTO_UDP;
	struct iphdr *iphdr;
	struct ipv6hdr *ipv6hdr;

	struct hdr_cursor nh = { .pos = data + nh_off };

	/* Extract L4 protocol */
	if (h_proto == bpf_htons(ETH_P_IP)) {
		// 返回 ipv4 包承载的协议类型
		ip_proto = (__u32)__xm_parse_ip4hdr(&nh, data_end, &iphdr);
	} else if (h_proto == bpf_htons(ETH_P_IPV6)) {
		// 返回 ipv6 包承载的协议类型
		ip_proto = (__u32)__xm_parse_ip6hdr(&nh, data_end, &ipv6hdr);
	} else {
		// 其他协议
		ip_proto = 0;
	}

	__u64 *rx_cnt = bpf_map_lookup_elem(&ipproto_rx_cnt_map, &ip_proto);
	if (rx_cnt) {
		*rx_cnt += 1;
	} else {
		__u64 init_value = 1;
		bpf_map_update_elem(&ipproto_rx_cnt_map, &ip_proto, &init_value,
				    BPF_NOEXIST);
	}

	switch (ip_proto) {
	case IPPROTO_ICMP:
		bpf_printk(
			"eth_frame proto:0x%x, pkg size:'%d', ICMP rc_cxt = %lu",
			bpf_ntohs(h_proto), pkt_sz, (rx_cnt) ? *rx_cnt : 1);
	case IPPROTO_TCP:
		bpf_printk(
			"eth_frame proto:0x%x, pkg size:'%d', TCP rc_cxt = %lu",
			bpf_ntohs(h_proto), pkt_sz, (rx_cnt) ? *rx_cnt : 1);
	case IPPROTO_UDP:
		bpf_printk(
			"eth_frame proto:0x%x, pkg size:'%d', UDP rc_cxt = %lu",
			bpf_ntohs(h_proto), pkt_sz, (rx_cnt) ? *rx_cnt : 1);
	default:
		bpf_printk(
			"eth_frame proto:0x%x, pkg size:'%d', unknown ip proto:'%u'\n",
			bpf_ntohs(h_proto), pkt_sz, ip_proto);
	}

	return __xdp_stats_record_action(ctx, XDP_PASS);
}

char _license[] SEC("license") = "GPL";

/*
使用：
1: bpftool prog load ./xm_xdp_pass.bpf.o /sys/fs/bpf/xm_xdp_pass type xdp
2：查看 prog id
	⚡ root@localhost  /home/calmwu/Program/x-monitor/plugin_ebpf/bpf/.output   main ±  bpftool prog
	49: xdp  name xdp_prog_simple  tag 94ce557b4142a280  gpl
		loaded_at 2025-08-15T17:52:52+0800  uid 0
		xlated 1256B  jited 734B  memlock 4096B  map_ids 53,54,52
		btf_id 61
3：附加到网卡上：bpftool net attach xdp id 49 dev ens160
4：查看 tracelog：bpftool prog tracelog
5: 分离 prog：bpftool net detach xdp dev ens160
6：删除 pin 文件：rm /sys/fs/bpf/xm_xdp_pass
*/
