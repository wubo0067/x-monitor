/*
 * @Author: CALM.WU
 * @Date: 2024-02-29 17:04:07
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2025-09-17 14:28:38
 */

#include "vmlinux.h"
#include "../bpf_and_user.h"
#include "xm_bpf_helpers_math.h"
#include "xm_bpf_helpers_common.h"
#include "xm_bpf_helpers_maps.h"
#include "xm_bpf_helpers_hard.h"

//
struct xm_nfs_op_tracing_data {
	__u64 ts;
	struct xm_blk_num blk_num;
};

// 定义 ebpf map
// 用来记录 pid 的 nfs 操作的开始和结束，以及块设备号
BPF_PERCPU_HASH(xm_nfs_op_tracing_map, __u32, struct xm_nfs_op_tracing_data,
		10240);
// 支持 256 个 nfs 盘
BPF_PERCPU_HASH(xm_nfs_oplat_hist_map, struct xm_blk_num,
		struct xm_nfs_oplat_stat, 256);

// 初始化变量
static struct xm_blk_num __init_blk_num __attribute__((unused)) = {
	.major = 0,
	.minor = 0,
};

static struct xm_nfs_oplat_stat __init_nfs_oplat_stat
	__attribute__((unused)) = {
		.slots = { 0 },
	};

// const char *const xm_op_type_name[XM_NFS_OP_TYPE_MAX] = {
//     "xm_nfs_op_none", "xm_nfs_op_open", "xm_nfs_op_read", "xm_nfs_op_write",
//     "xm_nfs_op_getattr"
// };

static __always_inline void __record_nfs_op_entry(enum xm_nfs_op_type op_type,
						  struct inode *inode)
{
	struct xm_nfs_op_tracing_data data = { 0, { 0, 0 } };
	// 操作时间
	data.ts = bpf_ktime_get_ns();
	// 线程 id
	__u32 tid = __xm_get_tid();
	//
	__xm_get_blk_num(inode, &(data.blk_num));
	bpf_printk("tid:%d, op-type:%d,  blk-num:%d", op_type, op_type,
		   MKDEV(data.blk_num.major, data.blk_num.minor));

	// 记录到 xm_nfs_op_tracing_map 中
	bpf_map_update_elem(&xm_nfs_op_tracing_map, &tid, &data, BPF_ANY);
}

static __always_inline void __record_nfs_op_exit(enum xm_nfs_op_type op_type)
{
	// 线程 id
	__u32 tid = __xm_get_tid();
	// 查询 tracing map
	struct xm_nfs_op_tracing_data *data =
		bpf_map_lookup_elem(&xm_nfs_op_tracing_map, &tid);
	if (data) {
		struct xm_nfs_oplat_stat *stat =
			__xm_bpf_map_lookup_or_try_init(&xm_nfs_oplat_hist_map,
							&(data->blk_num),
							&__init_nfs_oplat_stat);
		if (stat) {
			// 计算耗时
			__u64 delta = bpf_ktime_get_ns() - data->ts;
			if ((__s64)delta >= 0) {
				// 转换为微秒
				delta /= 1000;
				// 时间 2 的取对数，求出槽位
				__u64 slot = __xm_log2l(delta);
				// 超过最大槽位，放到最后一个槽位
				if (slot >= XM_NFS_OP_STAT_SLOT_COUNT) {
					slot = XM_NFS_OP_STAT_SLOT_MAX_IDX;
				}
				// 根据操作类型计算 slot 最终位置
				if (op_type > XM_NFS_OP_TYPE_NONE &&
				    op_type < XM_NFS_OP_TYPE_MAX) {
					slot = (op_type -
						1) * XM_NFS_OP_STAT_SLOT_COUNT +
					       slot;
					if (slot <
					    XM_NFS_OP_STAT_TABLE_TOTAL_SLOT_COUNT) {
						// 记录到统计表中
						__sync_fetch_and_add(
							&stat->slots[slot], 1);
					}
					bpf_printk(
						"tid:%d, delta:%llu, slot:%llu",
						tid, delta, slot);
				}
			}
		}
		//  清理
		bpf_map_delete_elem(&xm_nfs_op_tracing_map, &tid);
	}
	return;
}

//-----open-----

SEC("kprobe/nfs_file_open")
__s32 BPF_KPROBE(xm_kp_nfs_file_open, struct inode *inode, struct file *filp)
{
	__record_nfs_op_entry(XM_NFS_OP_TYPE_OPEN, inode);
	return 0;
}

SEC("kretprobe/nfs_file_open")
__s32 BPF_KRETPROBE(xm_krp_nfs_file_open, __s64 ret)
{
	__record_nfs_op_exit(XM_NFS_OP_TYPE_OPEN);
	return 0;
}

//-----read-----

SEC("kprobe/nfs_file_read")
__s32 BPF_KPROBE(xm_kp_nfs_file_read, struct kiocb *iocb, struct iov_iter *to)
{
	struct inode *inode = 0;
	BPF_CORE_READ_INTO(&inode, iocb, ki_filp, f_inode);
	__record_nfs_op_entry(XM_NFS_OP_TYPE_READ, inode);
	return 0;
}

SEC("kretprobe/nfs_file_read")
__s32 BPF_KRETPROBE(xm_krp_nfs_file_read, __s64 ret)
{
	__record_nfs_op_exit(XM_NFS_OP_TYPE_READ);
	return 0;
}

//-----write-----

SEC("kprobe/nfs_file_write")
__s32 BPF_KPROBE(xm_kp_nfs_file_write, struct kiocb *iocb,
		 struct iov_iter *from)
{
	struct inode *inode = 0;
	BPF_CORE_READ_INTO(&inode, iocb, ki_filp, f_inode);
	__record_nfs_op_entry(XM_NFS_OP_TYPE_WRITE, inode);
	return 0;
}

SEC("kretprobe/nfs_file_write")
__s32 BPF_KRETPROBE(xm_krp_nfs_file_write, __s64 ret)
{
	__record_nfs_op_exit(XM_NFS_OP_TYPE_WRITE);
	return 0;
}

//-----getattr-----

SEC("kprobe/nfs_getattr")
__s32 BPF_KPROBE(xm_kp_nfs_getattr, struct path *path, struct kstat *stat,
		 __u32 request_mask, __u32 flags)
{
	struct inode *inode = 0;
	BPF_CORE_READ_INTO(&inode, path, dentry, d_inode);
	__record_nfs_op_entry(XM_NFS_OP_TYPE_GETATTR, inode);
	return 0;
}

SEC("kretprobe/nfs_getattr")
__s32 BPF_KRETPROBE(xm_krp_nfs_getattr, __s64 ret)
{
	__record_nfs_op_exit(XM_NFS_OP_TYPE_GETATTR);
	return 0;
}

char _license[] SEC("license") = "GPL";