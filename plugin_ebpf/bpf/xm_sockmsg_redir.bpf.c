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
	key->family = AF_INET;
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
	struct sock_key key = {};
	uint64_t end = (uint64_t)msg->data_end;
	uint64_t start = (uint64_t)msg->data;

	__xm_extract_key_from_msg(msg, &key);
	bpf_printk("msg length: %llu, local %pI4:%u -> remote %pI4:%u",
		   (end - start), &key.sip4, key.sport, &key.dip4, key.dport);
	return SK_PASS;
}

char _license[] SEC("license") = "GPL";

/*

bpftool prog load .output/xm_sockmsg_redir.bpf.o /sys/fs/bpf/xm_sockmsg_redir xm_sock_redir_hash type sk_msg map name xm_sock_redir_hash /sys/fs/bpf/sock_redir_map
bpftool cgroup attach /tmp/cgroupv2/foo msg_verdict  pinned /sys/fs/bpf/xm_sockmsg_redir
*/
