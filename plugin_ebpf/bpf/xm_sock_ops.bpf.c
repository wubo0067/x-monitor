/*
 * @Author: CALM.WU
 * @Date: 2025-09-17 14:25:05
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2025-09-17 17:26:10
 */

#include <vmlinux.h>
#include "xm_bpf_helpers_common.h"
#include "xm_bpf_helpers_net.h"

//static __always_inline void __xm_extract_key4_from_ops()

/*
	为 socket 消息重定向，记录 socket 的主动、被动连接、断开事件
*/
SEC("sockops/redir") int32_t xm_sockmap_redir(struct bpf_sock_ops *skops)
{
	uint32_t remote_ip, local_ip;
	uint16_t remote_port, local_port;
	int32_t op = (int32_t)skops->op;

	switch (op) {
	case BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB: {
		// 主动建立连接
		remote_ip = bpf_ntohl(skops->remote_ip4);
		remote_port = skops->remote_port;

		bpf_printk("Active connection established to %pI4:%d",
			   &remote_ip, bpf_ntohs(remote_port));

		bpf_sock_ops_cb_flags_set(skops, BPF_SOCK_OPS_STATE_CB_FLAG);
		break;
	}
	case BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB: {
		// 被动建立连接
		remote_ip = bpf_ntohl(skops->remote_ip4);
		remote_port = skops->remote_port;

		bpf_printk("Passive connection established from %pI4:%d",
			   &remote_ip, bpf_ntohs(remote_port));
		break;
	}
	case BPF_SOCK_OPS_STATE_CB:
		// 连接状态变化，判断连接是否断开
		/*
		在 BPF_SOCK_OPS_STATE_CB 回调里，skops->args[0] = old_state，skops->args[1] = new_state。
		你判断到 skops->args[1] == BPF_TCP_CLOSE 只能说明 TCP 连接对象已经进入最终的 CLOSE 状态，
		但无法直接断定“应用刚刚调用了 close()”。

		只看 new_state == CLOSE 不能区分是主动 close()、被动关闭结束，还是异常复位。
		若要判断“应用主动调用 close()”，应在更早的状态跃迁处判断：
		old=ESTABLISHED, new=FIN_WAIT1 → 本端主动关闭（应用 close()/shutdown(SHUT_WR)）
		old=ESTABLISHED, new=CLOSE_WAIT → 对端先发 FIN（被动关闭）
		最终 CLOSE 只是生命周期终点，不含关闭方向与原因信息

		!!skops->args[1] == BPF_TCP_CLOSE 只能说明“连接对象已终结”
		*/
		if (skops->args[1] == BPF_TCP_CLOSE) {
			local_ip = bpf_ntohl(skops->local_ip4);
			remote_ip = bpf_ntohl(skops->remote_ip4);

			local_port = skops->local_port;
			remote_port = skops->remote_port;

			bpf_printk("Connection closed %pI4:%d <-> %pI4",
				   &local_ip, bpf_ntohs(local_port),
				   &remote_ip);
			break;
		}
	case BPF_SOCK_OPS_TCP_LISTEN_CB:
		/*
		向内核登记/设置该 socket 的 BPF 回调标志（cb flags），
		请求内核在该 socket 发生对应事件时调用相应的 BPF sockops 回调（即让该 socket“订阅”某类事件）
		*/
		bpf_sock_ops_cb_flags_set(skops, BPF_SOCK_OPS_STATE_CB_FLAG);
		break;
	default:
		break;
	}
	return 0;
}

char _license[] SEC("license") = "GPL";

/*
/home/calmwu/Program/bpftool/src/bpftool prog load .output/xm_sock_ops.bpf.o /sys/fs/bpf/bpf_sockops type sockops

bpftool cgroup attach /tmp/cgroupv2/foo cgroup_sock_ops pinned /sys/fs/bpf/bpf_sockops
*/