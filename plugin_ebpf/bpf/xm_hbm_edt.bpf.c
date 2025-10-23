/*
 * @Author: CALM.WU
 * @Date: 2025-10-11 14:43:59
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2025-10-11 15:12:16
 */

#include <vmlinux.h>
#include "xm_bpf_helpers_common.h"
#include "xm_bpf_helpers_maps.h"

#define DROP_PKT 0
#define KEEP_PKT 1
#define CRW	 2

// Time base accounting for fq's EDT
#define BURST_SIZE_NS  100000 // 100us 的突发流量
#define MARK_THRESH_NS 50000 // 50us 的标记阈值
#define DROP_THRESH_NS 500000 // 500us 的丢弃阈值

/*

Mbps	MB/s	B/ns
1		0.12	0.00012
50		6.25	0.00625
100		12.50	0.01250
150		18.75	0.01875
200		25.00	0.02500
250		31.25	0.03125
300		37.50	0.03750
400		50.00	0.05000
500		62.50	0.06250
600		75.00	0.07500
700		87.50	0.08750
800		100.00	0.10000
900		112.50	0.11250
1000    125.00	0.12500
*/

/*
宏定义了大数据包的丢弃阈值，它基于标准的丢包阈值 DROP_THRESH_NS 减去 20,000 纳秒（20 微秒）。
这种设计体现了对大数据包更严格的处理策略：
	由于大数据包占用更多的网络带宽和传输时间，系统需要更早地开始丢弃这些包以防止队列拥塞。
	20 微秒的提前量为系统提供了额外的缓冲空间，确保在网络负载增加时能够更及时地响应。
*/
#define LARGE_PKT_DROP_THRESH_NS (DROP_THRESH_NS - 20000)

#define LARGE_PKT_THRESH 120

/*
	宏计算了 ECN（Explicit Congestion Notification）标记区域的大小，
	这是通过大数据包丢弃阈值减去标记阈值 MARK_THRESH_NS 得出的。
	这个区域定义了一个"警告窗口"：当数据包的延迟落在这个范围内时，
	系统会对其进行 ECN 标记而不是直接丢弃。这种分层处理机制允许接收端和发送端协作进行拥塞控制，
	在完全丢包之前给予网络协议栈调整传输速率的机会。
*/
#define MARK_REGION_SIZE_NS (LARGE_PKT_DROP_THRESH_NS - MARK_THRESH_NS)

// 保存每个 cgroup 的 hbm edt 信息
struct hbm_edt_info {
	struct bpf_spin_lock lock;
	uint64_t last_time; // 下一个包发送的时间 In ns
	uint32_t rate; // 带宽，单位是 Mbps
};

// 全局 hbm edt 统计信息
struct hbm_edt_stats {
	uint32_t custom_rate; // 带宽 Mbps, 多少 bit 每秒
	uint32_t stats : 1, // 统计标志，默认值：1
		no_loopback : 1, // 对 loopback 不使用 hbm edt, 默认值：1
		no_cn : 1, // 1: 不发送 cn，默认值：0
		verbose : 1; // 1: 打印日志，0: 不打印，默认值：0

	uint64_t bytes_total;
	uint64_t pkts_total;
	// drop flag
	uint64_t bytes_dropped;
	uint64_t pkts_dropped;
	// congestion flag
	uint64_t bytes_marked;
	uint64_t pkts_marked;
	/*
	连接健康度评估：
	cwnd > 10 packets:
	├── 连接健康，网络状况良好
	└── 可以支持高带宽应用
	cwnd < 5 packets:
	├── 连接受限，可能有问题
	└── 需要调查网络或应用问题
	cwnd 波动大：
	├── 网络不稳定
	└── 可能有间歇性拥塞

	故障模式识别：

	所有连接 cwnd 突然下降：
	├── 可能是网络链路拥塞
	└── 需要检查上游网络

	特定服务 cwnd 异常：
	├── 可能是应用层问题
	└── 需要检查服务器性能
	*/
	uint64_t sum_cwnd; // 拥塞窗口总和，单位是 packets
	uint64_t sum_rtt; // 往返时延总和，用于计算平均 RTT
	uint64_t sum_cwnd_cnt; // 统计拥塞窗口的次数，平均拥塞窗口 = sum_cwnd / sum_cwnd_cnt

	uint64_t pkts_ecn_ce; // ECN CE 标记的包数量
	uint64_t return_val_count[4]; // 不同返回值的计数
};

BPF_CGROUP_STORAGE(xm_hbm_edt_info_storage, struct hbm_edt_info);
BPF_HASH(xm_hbm_edt_stats_hash, uint64_t, struct hbm_edt_stats,
	 100); // key 是 cgroup id

struct hbm_skb_info {
	int32_t cwnd; // 拥塞窗口
	int32_t rtt; // 往返时延
	int32_t packets_out; // TCP 发送但尚未收到 ACK 的数据包数量，1: 可能是连接刚建立（SYN-ACK 阶段）
	bool is_ip; // 是否是 IP 包
	bool is_tcp; // 是否是 TCP 包
	int16_t ecn; // ECN 支持
};

static int32_t __xm_get_tcp_info(struct __sk_buff *skb,
				 struct hbm_skb_info *hsi)
{
	struct bpf_sock *sk;
	struct bpf_tcp_sock *tp;

	sk = skb->sk;
	if (sk) {
		// 获取完整套接字
		sk = bpf_sk_fullsock(sk);
		if (sk) {
			if (sk->protocol == IPPROTO_TCP) {
				// 仅对 TCP 套接字获取信息
				tp = bpf_tcp_sock(sk);
				if (tp) {
					// 对方能处理多少数据
					hsi->cwnd =
						tp->snd_cwnd; /* Sending congestion window, 用来统计平均拥塞窗口 */
					hsi->rtt = tp->srtt_us >>
						   3; // srtt_us 左移 3 位
					hsi->packets_out = tp->packets_out;
					return 0;
				}
			}
		}
	}
	hsi->cwnd = 0;
	hsi->rtt = 0;
	hsi->packets_out = 0;
	return -1;
}

static void __xm_get_hbm_skb_info(struct __sk_buff *skb,
				  struct hbm_skb_info *hsi)
{
	void *data = (void *)(long)(skb->data);
	void *data_end = (void *)(long)(skb->data_end);
	struct iphdr *iph = (struct iphdr *)(data);
	struct ipv6hdr *ip6h;

	hsi->cwnd = 0;
	hsi->rtt = 0;
	hsi->is_ip = false;
	hsi->is_tcp = false;
	hsi->ecn = 0;

	// !! 边界检查：至少需要 1 字节来读取 IP 版本，不检查 load 会失败
	if (data + 1 > data_end) {
		return;
	}

	if (iph->version == 6) {
		// ipv6
		ip6h = (struct ipv6hdr *)(data);
		// 检查完整的 IPv6 header
		if ((void *)(ip6h + 1) > data_end) {
			return;
		}

		hsi->is_ip = true;
		hsi->is_tcp = (ip6h->nexthdr == IPPROTO_TCP);
		hsi->ecn = (ip6h->flow_lbl[0] >> 4) & INET_ECN_MASK;
	} else if (iph->version == 4) {
		// IPv4
		iph = (struct iphdr *)data;
		// 检查完整的 IPv4 header（至少 20 字节）
		if ((void *)(iph + 1) > data_end) {
			return;
		}
		hsi->is_ip = true;
		hsi->is_tcp = (iph->protocol == IPPROTO_TCP);
		hsi->ecn = (iph->tos) & INET_ECN_MASK;
	}

	if (hsi->is_tcp) {
		__xm_get_tcp_info(skb, hsi);
	}
	return;
}

static void __xm_hbm_edt_update_stats(struct hbm_edt_stats *hes,
				      uint32_t skb_len, uint64_t now_ns,
				      bool drop_flag, bool congestion_flag,
				      bool ecn_ce_flag,
				      const struct hbm_skb_info *hsi,
				      int32_t ret)
{
	if (hes != NULL && hes->stats) {
		// 递增总字节数
		__sync_fetch_and_add(&(hes->bytes_total), skb_len);
		// 递增总包数
		__sync_fetch_and_add(&(hes->pkts_total), 1);
		// 拥塞统计
		if (congestion_flag) {
			__sync_fetch_and_add(&(hes->bytes_marked), skb_len);
			__sync_fetch_and_add(&(hes->pkts_marked), 1);
		}
		// 丢包统计
		if (drop_flag) {
			__sync_fetch_and_add(&(hes->bytes_dropped), skb_len);
			__sync_fetch_and_add(&(hes->pkts_dropped), 1);
		}
		// ecn ce 标记统计
		if (ecn_ce_flag) {
			__sync_fetch_and_add(&(hes->pkts_ecn_ce), 1);
		}
		// Sending congestion window 累计
		if (hsi->cwnd > 0) {
			__sync_fetch_and_add(&(hes->sum_cwnd), hsi->cwnd);
			__sync_fetch_and_add(&(hes->sum_cwnd_cnt), 1);
		}
		// 往返时延累计
		if (hsi->rtt) {
			__sync_fetch_and_add(&(hes->sum_rtt), hsi->rtt);
		}

		if (ret == 0) {
			//0 : drop packet
			__sync_fetch_and_add(&(hes->return_val_count[0]), 1);
		} else if (ret == 1) {
			// 1: keep packet
			__sync_fetch_and_add(&(hes->return_val_count[1]), 1);
		} else if (ret == 2) {
			// 2: drop packet and cn
			__sync_fetch_and_add(&(hes->return_val_count[2]), 1);
		} else if (ret == 3) {
			// 3: keep packet and cn
			__sync_fetch_and_add(&(hes->return_val_count[3]), 1);
		}
	}
}
/*
cgroup_skb 程序运行在 L3 层

 bpf.h BPF_PROG_CGROUP_INET_EGRESS_RUN_ARRAY
 * Hence, new allowed return values of CGROUP EGRESS BPF programs are:
 *   0: drop packet
 *   1: keep packet
 *   2: drop packet and cn
 *   3: keep packet and cn
*/

SEC("cgroup_skb/egress/xm_hbm_edt")
int32_t xm_hbm_edt_out(struct __sk_buff *skb)
{
	int32_t ret = KEEP_PKT;
	int32_t hbm_edt_stats_idx = 0;
	uint64_t skb_len = skb->len;
	uint32_t rand_len;
	struct hbm_edt_stats *hes = NULL;
	struct hbm_edt_info *hei = NULL;
	struct hbm_skb_info hsi = { 0 };
	uint64_t cgid, now_ns, send_time, delay_ns;
	int64_t delta;
	bool drop_flag = false;
	bool cwr_flag = false; //congestion window reduce flag
	bool congestion_flag = false; // congestion flag
	bool ecn_ce_flag = false; // ecn ce flag

	// stat -Lc %i /tmp/cgroupv2/foo 获取 cgroup id
	cgid = bpf_skb_cgroup_id(skb);

	// 查询用户定义数据
	hes = (struct hbm_edt_stats *)bpf_map_lookup_elem(
		&xm_hbm_edt_stats_hash, &cgid);

	// 判断 lookback traffic 是否不使用 hbm edt 做带宽限制
	if (hes != NULL && hes->no_loopback && (skb->ifindex == 1)) {
		return KEEP_PKT;
	}

	// 获取 skb 包信息
	__xm_get_hbm_skb_info(skb, &hsi);

	// 获取 cgroup 对应的 hbm edt 信息
	hei = bpf_get_local_storage(&xm_hbm_edt_info_storage, 0);
	if (hei == NULL) {
		// 获取失败，放行
		return KEEP_PKT;
	}

	now_ns = bpf_ktime_get_ns();
	if (hei->last_time == 0 || hei->rate == 0) {
		// 第一次获取，初始化 该 cgroup 的 hbm edt 信息
		// 默认采用 1000Mbps, 1Gbps = 125MB/s，采用的是 Q25.7 定点表示，128(=2⁷) 倍存储，保留 7 位小数精度
		// hei->rate = (hei->rate == 0 ? 100 * 128 : hei->rate);
		// hei->last_time = (hei->last_time == 0 ? now_ns - BURST_SIZE_NS :
		// 					hei->last_time);
		// 不使用 Q25.7 定点表示
		hei->rate = (hei->rate == 0 ? 100 : hei->rate);
		hei->last_time = (hei->last_time == 0 ? now_ns - BURST_SIZE_NS :
							hei->last_time);
		bpf_printk(
			"Initializing cgroup:'%lu' hbm edt info, rate:%d Mbps, last_time:%llu\n",
			cgid, hei->rate, hei->last_time);
	}

	// bpf_printk(
	// 	"xm_hbm_edt rate:%d Mbps, now_ns:%llu ns, last_time:%llu ns\n",
	// 	hei->rate / 128, now_ns, hei->last_time);
	// 开始临界区，因为多个 cpu 可能同时处理同一个 cgroup 的数据包，需要保护 hei->last_time 不被并发修改
	bpf_spin_lock(&hei->lock);

	/*
	hei->lasttime: 上一个数据包的预定发送时间（未来时间戳）
	delta > 0: 债务，表示上一个包还没到发送时间，新包需要排队等待，如果下一个包发送的时间比当前时间晚，有包堆积
	delta < 0: 信用，表示距离上次发送已经过了一段时间，积累了可用带宽（允许突发
	delta ≈ 0: 刚好在速率限制的边界

	如果 lasttime = 1000ns, now_ns = 900ns → delta = 100ns（需要等 100ns）
	如果 lasttime = 1000ns, now_ns = 1200ns → delta = -200ns（有 200ns 的信用可用）
	*/
	delta = hei->last_time - now_ns;

	// 判断是否长时间没有发送包，导致信用堆积超过了突发的限制
	if (delta < -BURST_SIZE_NS) {
		delta = -BURST_SIZE_NS;
		hei->last_time = now_ns - BURST_SIZE_NS;
	}
	// 包发送的时间
	send_time = hei->last_time;
	/* 计算发送这个包 skb_len 需要的时间，单位纳秒
	!! rate 使用了定点数，放大了 128 倍
	1 Mbps = 1,000,000 bits/s = 125,000 bytes/s
	= 125,000 bytes/1,000,000,000 ns
	= 1 byte/8,000 ns 这个速率发送一个字节需要 8000ns
	= 0.000125 bytes/ns

	但 len / rate 时，会乘以 8000，约等于乘以 8192(2¹³)，也就是 len << 13

	举例：如果 rate = 1Mbps = 0.000125 bytes/ns
	skb_len / rate = skb_len * 8000ns 约等于 skb_len << 13, 在放大 128 倍，就是 skb_len << 20
	!! 内核不支持浮点数除法，只支持整数除法
	*/
	// 计算 skb_len 发送需要的纳秒
	/* bytes -> ns:
	 * time_s = (skb_len * 8) / (rate_Mbps * 1e6)
	 * time_ns = time_s * 1e9 = (skb_len * 8 * 1e3) / rate_Mbps = skb_len * 8000 / rate_Mbps
	 */
	delay_ns = (uint64_t)skb_len * 8000ULL / (uint64_t)hei->rate;

	__sync_fetch_and_add(&(hei->last_time), delay_ns);

	bpf_spin_unlock(&hei->lock);

	// bpf_printk(
	// 	"xm_hbm_edt skb_len:%llu, delta:%lld, last_time:%llu, delay_ns:%llu ns\n",
	// 	skb_len, delta, hei->last_time, delay_ns);

	// Set EDT of packet
	// 设置数据包的发送时间戳（EDT - Earliest Departure Time）
	// 关键机制：内核的 sch_fq (Fair Queue) qdisc 会读取这个时间戳
	// qdisc 会将数据包保持到 sendtime 到达时才真正发送
	// 这实现了精确的发送速率控制（pacing）
	/*
	eBPF 程序            内核 qdisc (sch_fq)
    │                    │
    ├─ 计算 sendtime      │
    ├─ skb->tstamp = sendtime
    │                    │
    └─ 返回 KEEP_PKT ──> │
                         ├─ 检查 skb->tstamp
                         ├─ if (now < tstamp)
                         │    等待...
                         └─ 到达时间 -> 发送
	*/
	skb->tstamp = send_time;

	/*
	检查是否要更新 rate，使用 user 定义的速率
	*/
	if (hes != NULL && hes->custom_rate != 0 &&
	    hes->custom_rate != hei->rate) {
		hei->rate = hes->custom_rate;
	}

	// 根据 delta 判断债务是否超过丢包的阈值，同时判断大包丢包逻辑（包的长度和债务时间更短）
	if (delta > DROP_THRESH_NS ||
	    (delta > LARGE_PKT_DROP_THRESH_NS && skb_len > LARGE_PKT_THRESH)) {
		// 标记丢包
		drop_flag = true;
		// 判断是否是 tcp 且是否支持 ecn
		if (hsi.is_tcp && hsi.ecn == 0) {
			// 不支持 ecn，设置降低拥塞窗口 flag
			cwr_flag = true;
		}
	} else if (delta > MARK_THRESH_NS) {
		// 债务在 MARK_THRESH_NS（通常 50μs）和丢包阈值之间，拥塞标记阈值，进入"早期拥塞通知"区域，
		// 这是警告区域，尝试通过标记避免丢包
		if (hsi.is_tcp) {
			// 是 tcp，设置拥塞标志
			congestion_flag = true;
		} else {
			// 非 tcp 直接丢包
			drop_flag = true;
		}
	}

	/*拥塞处理逻辑
		当 congestion_flag == true 时，优先尝试 ECN 标记。
		如果 ECN 标记失败且是 TCP 包，则设置 cwr_flag = true，返回 NET_XMIT_CN。
		NET_XMIT_CN 的作用：

		不会直接缩小 cwnd。
		会触发内核调用 tcp_enter_cwr，从而启动拥塞窗口的调整过程。
		ECN 和 CWR 的关系：

		ECN 标记成功时，发送方会根据 ECN 标记主动调整拥塞窗口。
		如果 ECN 标记失败，则通过 NET_XMIT_CN 和 tcp_enter_cwr 实现拥塞控制。
		CWR 位的设置：

		内核不会直接设置 TCP 头部的 CWR 位。
		CWR 位由发送方在调整拥塞窗口后设置，用于通知接收方。
		通过这种设计，eBPF 程序可以灵活地配合内核和 TCP 协议栈，实现高效的拥塞控制。
	*/
	if (congestion_flag) {
		// 修改 IP 头部 ECN 字段
		if (bpf_skb_ecn_set_ce(skb)) {
			// 优先尝试 ECN（Explicit Congestion Notification）标记，
			// bpf_skb_ecn_set_ce:尝试在 IP 头设置 ECN 的 CE 位
			/*
			IP 头 ECN 字段（2 位）:
				00 - Non-ECT (不支持 ECN)
				01 - ECT(1) (支持 ECN)
				10 - ECT(0) (支持 ECN)
				11 - CE (拥塞经历) ← bpf_skb_ecn_set_ce 设置这个
			*/
			// 记录 ECN 标记成功，表示通过 ECN 成功通知了发送方拥塞情况
			ecn_ce_flag = true;
		} else {
			// tcp 协议包，ECN 标记失败，可能是因为内核没开启 tcp ecn 支持 net.ipv4.tcp_ecn=2
			// *通过一个随机化的概率机制来决定是否触发拥塞控制（CWR 标志）
			// 如果 delta 超过了一个动态计算的随机阈值（基于 MARK_THRESH_NS 和 MARK_REGION_SIZE_NS），则认为网络拥塞严重，触发拥塞控制 MARK_THRESH_NS + 随机值
			// 这种随机化机制可以避免过于频繁地触发拥塞控制，同时根据网络状况动态调整。
			if (hsi.is_tcp) {
				rand_len = bpf_get_prandom_u32();
				/*
				- MARK_THRESH_NS = 5,000 ns (标记阈值)
				- MARK_REGION_SIZE_NS = LARGE_PKT_DROP_THRESH_NS - MARK_THRESH_NS
									= (500,000 - 20,000) - 5,000 = 475,000 ns
				- rand % 475,000 = 0 到 474,999 的随机数
				因此随机阈值范围：[5,000, 479,999] ns
				*/
				if (delta >=
				    MARK_THRESH_NS +
					    (rand_len % MARK_REGION_SIZE_NS)) {
					cwr_flag = true;
				}
			} else if (skb_len > LARGE_PKT_THRESH) {
				// 保护小包，网络协议的控制包通常都是小包
				/*
					DNS 查询/响应：通常 < 512 bytes
					ICMP 包（如 ping）：通常 64-84 bytes
					NTP 时间同步：48 bytes
					SNMP 查询：< 1500 bytes
					VoIP 信令（如 SIP）：几百字节
					游戏控制包：通常很小

					小包对带宽的影响有限
					避免"拥塞雪崩"效应

					常见小包的大小：
					├── 最小 IP 包：20 (IP 头) + 数据 = 至少 28 bytes
					├── TCP SYN：40 bytes (IP 头 20 + TCP 头 20)
					├── TCP ACK：40-60 bytes
					├── DNS 查询：通常 < 100 bytes
					├── ICMP Echo：通常 64-84 bytes
					└── UDP 控制包：通常 < 100 bytes
				*/
				drop_flag = true;
				congestion_flag = false;
			}
		}
	}

	// 如果是 drop 包，hei->last_time 需要回退
	if (drop_flag) {
		__sync_fetch_and_add(&(hei->last_time), -delay_ns);
		// 设置返回值，让内核丢包
		ret = DROP_PKT;
	}

	// 当 cgroup_skb 程序返回值包含 2 时，内核会将该信息传递给 TCP 协议栈。
	// 从 Linux 5.3 开始，返回值新增了一个语义：
	// bit2 (值为 2) → 表示“拥塞发生 (congestion occurred)”。
	/*
		tcp_enter_cwr(sk);
		err = net_xmit_eval(err);
		#define net_xmit_eval(e) ((e) == NET_XMIT_CN ? 0 : (e))
	*/
	if (cwr_flag) {
		// 腰启动 congestion window reduce, 无论是 drop 还是 keep 包
		ret |= CRW;
	}
	// 打印输出各种 flag 的值
	if (hes != NULL && hes->verbose &&
	    (drop_flag || congestion_flag || ecn_ce_flag || cwr_flag)) {
		bpf_printk(
			"xm_hbm_edt cgid:%lu, drop:%d, congestion:%d, ecn-ce:%d, cwr:%d, ret:%d\n",
			cgid, drop_flag, congestion_flag, ecn_ce_flag, cwr_flag,
			ret);
	}

	__xm_hbm_edt_update_stats(hes, skb_len, now_ns, drop_flag,
				  congestion_flag, ecn_ce_flag, &hsi, ret);

	return ret;
}

char _license[] SEC("license") = "GPL";

/*
sysctl -w net.core.default_qdisc=fq

bpftool prog load .output/xm_hbm_edt.bpf.o /sys/fs/bpf/xm_hbm_edt type cgroup_skb/egress
bpftool cgroup attach /tmp/cgroupv2/foo cgroup_inet_egress pinned /sys/fs/bpf/xm_hbm_edt

bpftool cgroup list /tmp/cgroupv2/foo

socat TCP4-LISTEN:1000,bind=192.168.14.132,reuseaddr,fork exec:cat
nc 192.168.14.132 1000

iperf3 -s -B 172.24.48.251 -p 1000 -1
iperf3 -c 172.24.48.251 -p 1000 -i 0 -P 1 -f m -t 6

bpftool map dump name xm_hbm_edt_stat

*修改 hash map 中 cgroup 对应的带宽值
# custom_rate = 100 Mbps
bpftool map update name xm_hbm_edt_stat key hex 51 29 00 00 00 00 00 00 value hex 64 00 00 00 03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
[root@localhost ~]# printf "0x%x\n" 10577
0x2951

# custom_rate = 200 Mbps
bpftool map update name xm_hbm_edt_stat key hex 51 29 00 00 00 00 00 00 value hex c8 00 00 00 03 00 00 00

# custom_rate = 50 Mbps
bpftool map update name xm_hbm_edt_stat key hex 51 29 00 00 00 00 00 00 value hex 32 00 00 00 03 00 00 00


bpftool cgroup detach /tmp/cgroupv2/foo cgroup_inet_egress pinned /sys/fs/bpf/xm_hbm_edt
rm -rf /sys/fs/bpf/xm_hbm_edt
*/