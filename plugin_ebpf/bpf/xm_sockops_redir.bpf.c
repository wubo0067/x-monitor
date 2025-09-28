/*
 * @Author: CALM.WU
 * @Date: 2025-09-17 14:25:05
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2025-09-26 17:53:59
 */

#include <vmlinux.h>
#include "xm_bpf_helpers_common.h"
#include "xm_bpf_helpers_net.h"
#include "xm_sock_redir.h"

static __always_inline void
	__xm_extract_key4_from_ops(struct bpf_sock_ops *skops,
				   struct sock_key *key)
{
	// sock redir 只能做本机做重定向
	key->family = AF_LOCAL;
	key->sip4 = skops->local_ip4;
	key->dip4 = skops->remote_ip4;
	key->sport = skops->local_port; // host byte order
	key->dport = bpf_ntohl(skops->remote_port);
}

/*
	为 socket 消息重定向，记录 socket 的主动、被动连接、断开事件
*/
SEC("sockops/xm_sockops_redir")
int32_t xm_sockops_redir(struct bpf_sock_ops *skops)
{
	struct sock_key key = {};
	int32_t op = (int32_t)skops->op;
	int32_t ret = 0;

	switch (op) {
	case BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB:
		// 触发时机：主动发起连接的一方（客户端）在 TCP 三次握手完成、连接已建立 后触发。

		if (skops->family == AF_INET) {
			__xm_extract_key4_from_ops(skops, &key);

			// 判断是本机两个 socket 之间通讯
			if (key.sip4 == key.dip4) {
				ret = bpf_sock_hash_update(skops,
							   &xm_sock_redir_hash,
							   &key, BPF_NOEXIST);
				if (ret != 0) {
					bpf_printk(
						"xm_sock_redir_hash update failed: %d",
						ret);
				} else {
					bpf_printk(
						"ActiveEstab: local %pI4:%u -> remote %pI4:%u",
						&key.sip4, key.sport, &key.dip4,
						key.dport);
					bpf_sock_ops_cb_flags_set(
						skops,
						BPF_SOCK_OPS_STATE_CB_FLAG);
				}
			}
		} else {
			bpf_printk("Not AF_INET family: %d", skops->family);
		}
		break;
	case BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB:
		// 触发时机：被动接受连接的一方（服务端）在 TCP 三次握手完成、连接已建立 后触发
		if (skops->family == AF_INET) {
			__xm_extract_key4_from_ops(skops, &key);

			if (key.sip4 == key.dip4) {
				// 本机两个 socket 之间通讯
				ret = bpf_sock_hash_update(skops,
							   &xm_sock_redir_hash,
							   &key, BPF_NOEXIST);
				if (ret != 0) {
					bpf_printk(
						"xm_sock_redir_hash update failed: %d",
						ret);
				} else {
					bpf_printk(
						"PassiveEstab: local %pI4:%u -> remote %pI4:%u",
						&key.sip4, key.sport, &key.dip4,
						key.dport);
				}
			}
		} else {
			bpf_printk("Not AF_INET family: %d", skops->family);
		}
		break;
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
			__xm_extract_key4_from_ops(skops, &key);

			bpf_printk(
				"Connection closed. local %pI4:%u <-> remote %pI4:%u",
				&key.sip4, key.sport, &key.dip4, key.dport);
			// !! 不需要主动删除，内核会自动删除
			// close() → sock_close() → sock_map_close()
			// ret = bpf_map_delete_elem(&xm_sock_redir_hash, &key);
			// if (ret != 0) {
			// 	bpf_printk(
			// 		"xm_sock_redir_hash delete failed: %d",
			// 		ret);
			// }
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
rm -rf /tmp/cgroupv2
mkdir -p /tmp/cgroupv2
mount -t cgroup2 none /tmp/cgroupv2
mkdir -p /tmp/cgroupv2/foo

#加载，绑定
/home/calmwu/Program/bpftool/src/bpftool prog load .output/xm_sockops_redir.bpf.o /sys/fs/bpf/xm_bpf_sockredir type sockops
bpftool cgroup attach /tmp/cgroupv2/foo cgroup_sock_ops pinned /sys/fs/bpf/xm_bpf_sockredir

echo $$ >> /tmp/cgroupv2/foo/cgroup.procs

启动服务器：socat TCP4-LISTEN:1000,fork exec:cat
启动客户端：nc localhost 1000


bpftool cgroup detach  /tmp/cgroupv2/foo cgroup_sock_ops pinned /sys/fs/bpf/xm_bpf_sockredir
rm /sys/fs/bpf/xm_bpf_sockredir


<...>-58706   [001] ....1.1  8308.261849: bpf_trace_printk: Active: local 192.168.14.132:52510 -> remote 0.0.0.0:1000
<...>-58706   [001] ...s3.1  8308.261871: bpf_trace_printk: Passive: local 192.168.14.132:1000 -> remote 192.168.14.132:52510
socat-58707   [000] ...s3.1  8329.803381: bpf_trace_printk: Connection closed 192.168.14.132:52510 <-> 192.168.14.132:1000
socat-58707   [000] ....1.1  8329.803393: bpf_trace_printk: Connection closed 192.168.14.132:1000 <-> 192.168.14.132:52510
socat-58702   [001] ....1.1  8344.924579: bpf_trace_printk: Connection closed 192.168.14.132:1000 <-> 0.0.0.0:0
*/