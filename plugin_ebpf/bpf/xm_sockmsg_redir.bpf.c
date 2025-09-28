/*
 * @Author: CALM.WU
 * @Date: 2025-09-26 15:25:17
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2025-09-26 16:30:35
 */

#include <vmlinux.h>
#include "xm_bpf_helpers_common.h"
#include "xm_bpf_helpers_net.h"
#include "xm_sock_redir.h"

static __always_inline void __xm_extract_key_from_msg(struct sk_msg_md *msg,
						      struct sock_key *key)
{
	key->family = AF_LOCAL;
	key->sip4 = msg->local_ip4;
	key->dip4 = msg->remote_ip4;
	key->sport = msg->local_port; // host byte order
	key->dport = bpf_ntohl(msg->remote_port);
}

/*
	拦截 sendmsg, sendfile
*/
SEC("sk_msg/xm_sockops_redir") int32_t xm_sockmsg_redir(struct sk_msg_md *msg)
{
	int32_t verdict = SK_PASS;
	struct bpf_sock *sk;
	struct sock_key key = {};
	uint64_t end = (uint64_t)msg->data_end;
	uint64_t start = (uint64_t)msg->data;

	__xm_extract_key_from_msg(msg, &key);
	bpf_printk(
		"msg length: %llu, family: %u, local %pI4:%u -> remote %pI4:%u",
		(end - start), key.family, &key.sip4, key.sport, &key.dip4,
		key.dport);

	// 实际上，对于 sk_msg 程序，你不需要先查找再重定向。可以直接使用 bpf_msg_redirect_hash，它会内部处理引用：
	// 如果 key 存在直接尝试重定向，如果 key 不存在会返回错误
	// 这个 eBPF 辅助函数只能用于在本机（即同一个 Linux 主机）内部的两个 Socket 之间重定向消息。
	// 是为了高性能地实现本地数据平面转发而设计的。
	// 加速本地通信：允许在一个 eBPF 程序中，将一个 Socket 接收到的消息直接转发给另一个位于同一台机器上的进程所拥有的 Socket。
	// 避免完整的内核网络协议栈处理
	verdict = bpf_msg_redirect_hash(msg, &xm_sock_redir_hash, &key,
					BPF_F_INGRESS);
	if (verdict == SK_PASS) {
		// 重定向成功
		bpf_printk("Redirected successfully!");
	} else { // verdict == SK_DROP
		// 重定向失败（找不到目标或其他错误）
		bpf_printk("Redirect failed, dropping message");
	}
	// 如果重定向失败 (例如，Map 中没有找到 Key 对应的 Socket)，
	// 我们希望让消息按默认路径继续发送。
	// **最关键的步骤：** 再次返回 SK_PASS，告诉内核让消息继续传递。
	return SK_PASS;
}

char _license[] SEC("license") = "GPL";

/*

bpftool prog load .output/xm_sockmsg_redir.bpf.o /sys/fs/bpf/xm_sockmsg_redir xm_sock_redir_hash type sk_msg map name xm_sock_redir_hash /sys/fs/bpf/sock_redir_map
bpftool cgroup attach /tmp/cgroupv2/foo msg_verdict  pinned /sys/fs/bpf/xm_sockmsg_redir
*/
