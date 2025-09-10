/*
 * @Author: CALM.WU
 * @Date: 2025-09-04 10:36:06
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2025-09-10 14:16:58
 */

#include <vmlinux.h>
#include <bpf/bpf_endian.h>
#include "xm_bpf_helpers_common.h"

#define TC_ACT_OK 0

struct meta_info {
	uint32_t mark;
} __attribute__((aligned(4)));

SEC("xdp_mark")
int _xdp_mark(struct xdp_md *ctx)
{
	struct meta_info *meta;
	void *data, *data_end;
	int ret;

	/* Reserve space in-front of data pointer for our meta info.
	 * (Notice drivers not supporting data_meta will fail here!)
	 bpf_xdp_adjust_meta(ctx, -(int)sizeof(*meta)); 会把 ctx->data_meta 指针向前移动，
	 为 struct meta_info 预留空间
	 */
	ret = bpf_xdp_adjust_meta(ctx, -(int)sizeof(*meta));
	if (ret < 0)
		return XDP_ABORTED;

	/* Notice: Kernel-side verifier requires that loading of
	 * ctx->data MUST happen _after_ helper bpf_xdp_adjust_meta(),
	 * as pkt-data pointers are invalidated.  Helpers that require
	 * this are determined/marked by bpf_helper_changes_pkt_data()
	 */
	// 2. 重新计算指针
	//    BPF 验证器要求在使用前必须重新从 ctx 加载指针
	data = (void *)(unsigned long)ctx->data;

	/* Check data_meta have room for meta_info struct */
	meta = (void *)(unsigned long)ctx->data_meta;
	// 3. 边界检查（非常重要！）
	//    确保元数据指针在数据指针之前，防止内存越界
	if (meta + 1 > data)
		// 如果 meta + 1 > data，说明元数据区和数据区有重叠或空间不足，必须中止处理
		// 正常情况下，meta + 1 <= data，即元数据区完全在数据区之前，空间充足。
		return XDP_ABORTED;

	// 设置元数据标记为 42
	meta->mark = 42;

	return XDP_PASS;
}

SEC("tc_mark")
int _tc_mark(struct __sk_buff *ctx)
{
	void *data = (void *)(unsigned long)ctx->data;
	void *data_end = (void *)(unsigned long)ctx->data_end;
	void *data_meta = (void *)(unsigned long)ctx->data_meta;
	struct meta_info *meta = data_meta;

	/* Check XDP gave us some data_meta */
	if (meta + 1 > data) {
		ctx->mark = 41;
		/* Skip "accept" if no data_meta is avail */
		// 如果没有：ctx->mark = 41（设置默认标记 41
		return TC_ACT_OK;
	}

	/* Hint: See func tc_cls_act_is_valid_access() for BPF_WRITE access */
	// 如果有：ctx->mark = meta->mark（将 XDP 的标记 42 复制到 SKB）
	ctx->mark = meta->mark; /* Transfer XDP-mark to SKB-mark */

	return TC_ACT_OK;
}

char _license[] SEC("license") = "GPL";

/*
内核 4.4 及以上版本（需配套 iproute2 软件包）引入了 direct-action（简称 da）标志。
该标志指示内核使用动作返回值（如 TC_ACT_SHOT、TC_ACT_OK 等）作为分类器的判断依据

启用 direct-action 标志后，您无需分别附加过滤器和动作——单个过滤器即可完成两项操作。
这应该是 tc 进行 eBPF 编程时的推荐做法。


使用 xdp-tutorial 的 testenv 来创建测试环境
t setup --name=calmwu --legacy-ip

export DEV=calmwu/veth0
export FILE=/home/calmwu/Program/x-monitor/plugin_ebpf/bpf/.output/xm_xdp_skbmeta.bpf.o

# via TC command
tc qdisc del dev $DEV clsact 2> /dev/null
tc qdisc add dev $DEV clsact
tc filter  add dev $DEV ingress prio 1 handle 1 bpf da obj $FILE sec tc_mark
tc filter show dev $DEV ingress

输出如下
 ⚡ root@localhost  /home/calmwu/Program/xdp-tutorial/testenv   main ±  tc filter show dev $DEV ingress
filter protocol all pref 1 bpf chain 0
filter protocol all pref 1 bpf chain 0 handle 0x1 xm_xdp_skbmeta.bpf.o:[tc_mark] direct-action not_in_hw id 1 tag 5c31ff22f666d76f jited

# XDP via IP command:
ip link set dev $DEV xdp off
ip link set dev $DEV xdp obj $FILE sec xdp_mark

# Use iptable to "see" if SKBs are marked
# 匹配 mark 值为 41 的 ICMP 包，并记录日志
iptables -I INPUT -p icmp -m mark --mark 41 -j LOG --log-prefix "ICMP packet with mark 41: "

# 匹配 mark 值为 42 的 ICMP 包，并记录日志
iptables -I INPUT -p icmp -m mark --mark 42 -j LOG --log-prefix "ICMP packet with mark 42: "

如果仅仅不挂载 xdp，而只是挂载 tc ingress, 那么 iptables 就会打印出 41
[Mon Sep  8 14:42:22 2025] ICMP packet with mark 41: IN=ens160 OUT= MAC=00:0c:29:c4:3d:1b:00:50:56:c0:00:08:08:00 SRC=192.168.14.1 DST=192.168.14.131 LEN=60 TOS=0x00 PREC=0x00 TTL=128 ID=61777 PROTO=ICMP TYPE=8 CODE=0 ID=1 SEQ=22 MARK=0x29
[Mon Sep  8 14:42:23 2025] ICMP packet with mark 41: IN=ens160 OUT= MAC=00:0c:29:c4:3d:1b:00:50:56:c0:00:08:08:00 SRC=192.168.14.1 DST=192.168.14.131 LEN=60 TOS=0x00 PREC=0x00 TTL=128 ID=61778 PROTO=ICMP TYPE=8 CODE=0 ID=1 SEQ=23 MARK=0x29

iptables -v -nL 查看匹配的包数量和字节数量

dmesg 会输出如下信息
[Fri Sep  5 15:31:46 2025] ICMP packet with mark 42: IN=calmwu OUT= MAC=e2:af:26:e4:04:f5:fa:49:8b:51:dd:54:08:00 SRC=10.11.1.2 DST=10.11.1.1 LEN=84 TOS=0x00 PREC=0x00 TTL=64 ID=18803 DF PROTO=ICMP TYPE=8 CODE=0 ID=11044 SEQ=38 MARK=0x2a
[Fri Sep  5 15:31:47 2025] ICMP packet with mark 42: IN=calmwu OUT= MAC=e2:af:26:e4:04:f5:fa:49:8b:51:dd:54:08:00 SRC=10.11.1.2 DST=10.11.1.1 LEN=84 TOS=0x00 PREC=0x00 TTL=64 ID=19552 DF PROTO=ICMP TYPE=8 CODE=0 ID=11044 SEQ=39 MARK=0x2a
[Fri Sep  5 15:31:48 2025] ICMP packet with mark 42: IN=calmwu OUT= MAC=e2:af:26:e4:04:f5:fa:49:8b:51:dd:54:08:00 SRC=10.11.1.2 DST=10.11.1.1 LEN=84 TOS=0x00 PREC=0x00 TTL=64 ID=20090 DF PROTO=ICMP TYPE=8 CODE=0 ID=11044 SEQ=40 MARK=0x2a

# Hint: catch XDP_ABORTED errors via
perf record -e xdp:*
perf script

*/