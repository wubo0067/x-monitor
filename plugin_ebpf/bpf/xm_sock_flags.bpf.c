/*
 * @Author: CALM.WU
 * @Date: 2025-09-10 14:16:05
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2025-09-10 14:40:41
 */

#include <vmlinux.h>
#include "xm_bpf_helpers_common.h"

#define AF_INET	       2 /* Internet IP Protocol 	*/
#define AF_INET6       10 /* IP version 6			*/
#define IPPROTO_ICMPV6 58 /* ICMPv6			*/

/*
strace -e socket ping -c1 -w1 172.16.1.100
socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP) = 3
socket(AF_INET6, SOCK_DGRAM, IPPROTO_ICMPV6) = 4
socket(AF_INET, SOCK_DGRAM, IPPROTO_IP) = 5


strace -e socket ping6 -c1 -w1 2401:db00::1
socket(AF_INET6, SOCK_DGRAM, IPPROTO_ICMPV6) = 3
socket(AF_INET6, SOCK_DGRAM, IPPROTO_IP) = 4
*/

SEC("cgroup/sock1")
int32_t xm_sock_prog1(struct bpf_sock *sk)
{
	uint64_t gid_uid = bpf_get_current_uid_gid();
	uint32_t uid = gid_uid & 0xFFFFFFFF;
	uint32_t gid = gid_uid >> 32;

	bpf_printk(
		"xm_sock_prog1 socket: family:'%d', type:'%d', protocol:'%d'",
		sk->family, sk->type, sk->protocol);
	bpf_printk("socket: uid:'%u', gid:'%u'", uid, gid);

	/*
	block PF_INET6, SOCK_RAW, IPPROTO_ICMPV6 sockets
	ie., make ping6 fail
	*/
	if (sk->family == AF_INET6 &&
	    (sk->type == SOCK_RAW || sk->type == SOCK_DGRAM) &&
	    sk->protocol == IPPROTO_ICMPV6) {
		bpf_printk("block PF_INET6, SOCK_RAW, IPPROTO_ICMPV6 socket");
		// 返回 0 表示拒绝
		return 0;
	}

	// 返回 1 表示放行
	return 1;
}

SEC("cgroup/sock2")
int32_t xm_sock_prog2(struct bpf_sock *sk)
{
	bpf_printk(
		"xm_sock_prog2 socket: family:'%d', type:'%d', protocol:'%d'",
		sk->family, sk->type, sk->protocol);
	/*
	block PF_INET, SOCK_RAW, IPPROTO_ICMP sockets
	ie. make ping fail
	*/
	if (sk->family == AF_INET &&
	    (sk->type == SOCK_RAW || sk->type == SOCK_DGRAM) &&
	    sk->protocol == IPPROTO_ICMP) {
		bpf_printk("block PF_INET, SOCK_RAW, IPPROTO_ICMP socket");
		// 返回 0 表示拒绝
		return 0;
	}
	return 1;
}

char _license[] SEC("license") = "GPL";