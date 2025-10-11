/*
 * @Author: CALM.WU
 * @Date: 2025-10-11 14:43:59
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2025-10-11 15:12:16
 */

#include <vmlinux.h>
#include "xm_bpf_helpers_common.h"
#include "xm_bpf_helpers_maps.h"

struct hbm_edt {
	struct bpf_spin_lock lock;
	uint64_t last_time; // 下一个包发送的时间 In ns
	uint32_t rate; // 带宽 MBps In bytes per NS << 20
};

BPF_CGROUP_STORAGE(xm_hbm_edt_storage, struct hbm_edt);

#define DROP_PKT    0
#define KEEP_PKT    1
#define DROP_PKT_CN 2
#define KEEP_PKT_CN 3

/*
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
	return ret;
}

char _license[] SEC("license") = "GPL";