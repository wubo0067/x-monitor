/*
 * @Author: CALM.WU
 * @Date: 2023-07-03 10:07:51
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2024-03-04 15:16:35
 */

#include <vmlinux.h>
#include <linux/version.h>
#include "../bpf_and_user.h"
#include "xm_bpf_helpers_common.h"
#include "xm_bpf_helpers_maps.h"
#include "xm_bpf_helpers_math.h"

#define MAX_REQ_COUNT 10240
#define REQ_OP_BITS   8
#define REQ_OP_MASK   ((1 << REQ_OP_BITS) - 1)

extern int LINUX_KERNEL_VERSION __kconfig;

// 是否按每个 request 的 cmd_flags 进行过滤
const volatile bool __filter_per_cmd_flag = false;
const volatile __s64 __request_min_latency_ns = 1000000; // 1ms
const volatile bool __is_old_api = true;

struct bio_req_stage {
	__u64 insert;
	__u64 issue;
};

struct bio_pid_data {
	pid_t tid; // 线程 id
	pid_t pid; // 进程 id
	char comm[TASK_COMM_LEN];
};

BPF_HASH(xm_bio_request_pid_data_map, struct request *, struct bio_pid_data,
	 10240);
BPF_HASH(xm_bio_request_start_map, struct request *, struct bio_req_stage,
	 10240);
BPF_HASH(xm_bio_info_map, struct xm_bio_key, struct xm_bio_data,
	 1024); // op 枚举乘以 8 个分区
struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 1 << 16); // 64k
} xm_bio_req_latency_event_ringbuf_map SEC(".maps");

// ** 类型标识，为了 bpf2go 程序生成 golang 类型
static struct xm_bio_key __zero_bio_key __attribute__((unused)) = {
	.major = 0,
	.first_minor = 0,
	.cmd_flags = 0,
};

const struct xm_bio_request_latency_evt_data *__unused_brl_evt
	__attribute__((unused));

static struct xm_bio_data __zero_bio_info = {
	.req_in_queue_latency_slots = { 0 },
	.req_latency_slots = { 0 },
	.bytes = 0,
	.sequential_count = 0,
	.random_count = 0,
};

static __always_inline __s32 xm_trace_request(struct request *rq, bool insert)
{
	struct bio_req_stage *stage_p = NULL, init_stage = { 0, 0 };
	__u64 now_ns = bpf_ktime_get_ns();

	// 查找
	stage_p = bpf_map_lookup_elem(&xm_bio_request_start_map, &rq);
	if (!stage_p) {
		// request 不存在
		stage_p = &init_stage;
	}

	if (insert) {
		// 记录 request 插入 dispatch 队列的时间
		// **实际过程有很多 request 直接 issue，没有经过 insert
		stage_p->insert = now_ns;
		// bpf_printk("xm_ebpf_exporter bio insert request:0x%x", rq);
	} else {
		// 记录 request 从 dispatch 队列出来放入驱动队列的事件，开始执行
		stage_p->issue = now_ns;
		__s64 delta = (__s64)(now_ns - stage_p->insert);
		if (delta < 0) {
			bpf_map_delete_elem(&xm_bio_request_start_map, &rq);
			return 0;
		}
		// bpf_printk("xm_ebpf_exporter bio issue request:0x%x", rq);
	}

	if (stage_p == &init_stage) {
		// 相等只有在 request 不存在的时候
		bpf_map_update_elem(&xm_bio_request_start_map, &rq, stage_p,
				    BPF_NOEXIST);
	}
	return 0;
}

SEC("kprobe/blk_account_io_merge_bio")
__s32 BPF_KPROBE(kprobe__blk_account_io_merge_bio, struct request *rq)
{
	struct bio_pid_data bpd;
	bpd.pid = __xm_get_pid();
	bpd.tid = __xm_get_tid();
	bpf_get_current_comm(&bpd.comm, sizeof(bpd.comm));
	bpf_map_update_elem(&xm_bio_request_pid_data_map, &rq, &bpd, 0);
	return 0;
}

SEC("kprobe/__blk_account_io_start")
__s32 BPF_KPROBE(kprobe__blk_account_io_start_new, struct request *rq)
{
	struct bio_pid_data bpd;
	bpd.pid = __xm_get_pid();
	bpd.tid = __xm_get_tid();
	bpf_get_current_comm(&bpd.comm, sizeof(bpd.comm));
	bpf_map_update_elem(&xm_bio_request_pid_data_map, &rq, &bpd, 0);
	return 0;
}

SEC("kprobe/blk_account_io_start")
__s32 BPF_KPROBE(kprobe__blk_account_io_start, struct request *rq)
{
	struct bio_pid_data bpd;
	bpd.pid = __xm_get_pid();
	bpd.tid = __xm_get_tid();
	bpf_get_current_comm(&bpd.comm, sizeof(bpd.comm));
	bpf_map_update_elem(&xm_bio_request_pid_data_map, &rq, &bpd, 0);
	return 0;
}

// SEC("kprobe/blk_mq_sched_insert_request")
// __s32 BPF_KPROBE(kprobe__blk_mq_sched_insert_request, struct request *rq,
//                  bool at_head, bool run_queue, bool async) {
//     bpf_printk("xm_ebpf_exporter bio blk_mq_sched_insert_request
//     request:0x%x, "
//                "async:%d",
//                rq, async);
//     return 0;
// }

// SEC("kprobe/dd_insert_requests")
// __s32 BPF_KPROBE(kprobe__dd_insert_requests) {
//     bpf_printk("xm_ebpf_exporter bio dd_insert_requests");
//     return 0;
// }

/*
    kworker/0:1H-9       [000] d..3 1534879.271754: bpf_trace_printk:
   xm_ebpf_exporter bio blk_mq_sched_insert_request request:0xa0fc4000, async:0
    kworker/0:1H-9       [000] d..3 1534879.271755: bpf_trace_printk:
   xm_ebpf_exporter bio blk_mq_request_bypass_insert request:0xa0fc4000
    kworker/0:1H-9       [000] d..4 1534879.271758: bpf_trace_printk:
   xm_ebpf_exporter bio issue request:0xa0fc4000
*/
// SEC("kprobe/blk_mq_request_bypass_insert")
// __s32 BPF_KPROBE(kprobe__blk_mq_request_bypass_insert, struct request *rq) {
//     bpf_printk("xm_ebpf_exporter bio blk_mq_request_bypass_insert
//     request:0x%x",
//                rq);
//     return 0;
// }

/*
    基本 insert -- issue -- complete 基本完整
    但也有 bypass, 调用 blk_mq_try_issue_list_directly, 直接 issue -- complete
   的
 */
// SEC("kprobe/blk_mq_sched_insert_requests")
// __s32 BPF_KPROBE(kprobe__blk_mq_sched_insert_requests,
//                  struct blk_mq_hw_ctx *hctx, struct blk_mq_ctx *mq_ctx,
//                  struct list_head *list, bool run_queue_async) {
//     bpf_printk("xm_ebpf_exporter bio blk_mq_sched_insert_requests "
//                "run_queue_async:%d",
//                run_queue_async);
//     return 0;
// }

// SEC("kprobe/blk_mq_try_issue_list_directly")
// __s32 BPF_KPROBE(kprobe__blk_mq_try_issue_list_directly,
//                  struct blk_mq_hw_ctx *hctx, struct list_head *list) {
//     bpf_printk("xm_ebpf_exporter bio blk_mq_try_issue_list_directly");
//     return 0;
// }

// 在插入 io-scheduler 的 dispatch 队列之前触发这个 tracepoint
// 因为在 kernel 5.11.0 之后，这个 tracepoint 的参数只有 struct request 了，
// 所以这里没法用 BPF_PROG 宏，需直接使用 ctx 作为参数，根据内核版本来获取参数
/*
    看内核函数 blk_mq_sched_insert_request，会发现如果 request 有三条路径
    1：blk_mq_sched_bypass_insert

    2: e->type->ops.insert_requests(hctx, &list, at_head);
   --->如果有调度器/sys/block/sdc/queue/scheduler，就会走这条路
   ，在有电梯算法的时候，例如 mq_deadline，会做 request
   的合并，那么就不会重新插入了 例如这个函数返回 blk_mq_sched_try_insert_merge
   true。所以要用 kretprobe:blk_mq_sched_try_insert_merge，来做 insert

    3: __blk_mq_insert_request，实际中只有走这条路才会触发该 tracepoint
   --->没有调度器/sys/block/sdc/queue/scheduler，就会走这条路

   !! 由于内核版本不同，函数参数也不同，所以不能直接使用 BPF_PROG，而是要用 ctx 来解析
*/
SEC("tp_btf/block_rq_insert") __s32 xm_tp_btf__block_rq_insert(__u64 *ctx)
{
	// ** 判断内核版本这里不行，实际中 rhel8.5 和 8.6 都是 4.18.0，后面的 extra
	// ** 版本好不同，但是 8.6 的函数已经更新了
	// if ((LINUX_KERNEL_VERSION <= KERNEL_VERSION(4, 18, 0))
	//     && (RHEL_MAJOR <= 8 && RHEL_MINOR < 6)) {
	if (__is_old_api) {
		// !! 这里这样写导致了 bpf_load 失败
		// rq = (struct request *)ctx[1];
		xm_trace_request((struct request *)ctx[1], true);
	} else {
		xm_trace_request((struct request *)ctx[0], true);
	}
	return 0;
}

/*
    在函数 blk_mq_sched_insert_requests 中，可能直接 issue request
    * try to issue requests directly if the hw queue isn't
    * busy in case of 'none' scheduler, and this way may save
    * us one extra enqueue & dequeue to sw queue.
*/

SEC("tp_btf/block_rq_issue")
__s32 xm_tp_btf__block_rq_issue(__u64 *ctx)
{
	if (__is_old_api) {
		xm_trace_request((struct request *)ctx[1], false);
		// rq = (struct request *)ctx[1];
	} else {
		xm_trace_request((struct request *)ctx[0], false);
	}
	return 0;
}

/**
 * block_rq_complete - block IO operation completed by device driver
 * @rq: block operations request
 * @error: status code
 * @nr_bytes: number of completed bytes
 */
SEC("tp_btf/block_rq_complete")
__s32 BPF_PROG(xm_tp_btf__block_rq_complete, struct request *rq, __s32 error,
	       __u32 nr_bytes)
{
	__s64 delta, in_queue_delta, complete_delta;
	struct xm_bio_key bio_key = { 0, 0, 0 };
	__u64 now_ns = bpf_ktime_get_ns();
	// 请求的扇区号
	sector_t sector;
	// 操作的扇区数量
	__u32 nr_sector;
	//
	__u64 slot;

	// TODO: 读取 rq_flags 这个字段看看，是否是被 merge 的 request
	// req_flags_t rq_flags = BPF_CORE_READ(rq, rq_flags);
	// bpf_printk("xm_ebpf_exporter bio complete request:0x%x, rq_flags:0x%x",
	// rq,
	//            rq_flags);

	struct bio_req_stage *stage_p =
		bpf_map_lookup_elem(&xm_bio_request_start_map, &rq);
	if (!stage_p) {
		// 如果在 complete 回调中找不到该 request，直接返回
		return 0;
	}

	// req 在队列中计算时间
	in_queue_delta = 0;
	if (stage_p->insert != 0) {
		delta = (__s64)(stage_p->issue - stage_p->insert);
		if (delta < 0) {
			goto cleanup;
		}
		// 转换为 us
		in_queue_delta = delta / 1000U;

		delta = (__s64)(now_ns - stage_p->insert);
		if (delta < 0) {
			goto cleanup;
		}
		complete_delta = delta / 1000U;
	} else {
		delta = (__s64)(now_ns - stage_p->issue);
		if (delta < 0) {
			goto cleanup;
		}
		complete_delta = delta / 1000U;
	}
	// bpf_printk("xm_ebpf_exporter bio request:0x%x in_queue_delta:%lld us, "
	//            "complete_delta:%lld us",
	//            rq, in_queue_delta, complete_delta);

	// 获得磁盘信息
	struct gendisk *disk = __xm_get_disk(rq);
	if (disk) {
		// 构建 key
		bio_key.major = BPF_CORE_READ(disk, major);
		bio_key.first_minor = BPF_CORE_READ(disk, first_minor);
		if (__filter_per_cmd_flag) {
			// 读取 cmd_flags
			// bio_key.cmd_flags = BPF_CORE_READ(rq, cmd_flags) | REQ_OP_MASK;
			bio_key.cmd_flags = rq->cmd_flags & REQ_OP_MASK;
		}
	} else {
		goto cleanup;
	}

	// 查询
	struct xm_bio_data *bio_info_p = __xm_bpf_map_lookup_or_try_init(
		&xm_bio_info_map, &bio_key, &__zero_bio_info);
	if (bio_info_p) {
		// 请求的起始扇区号
		sector = READ_KERN(rq->__sector);
		// 操作的连续扇区数量
		nr_sector = nr_bytes >> 9;
		if (bio_info_p->last_sector) {
			// 判断上次请求的最后扇区号
			if (bio_info_p->last_sector == sector) {
				// 循序操作
				__xm_update_u64(&bio_info_p->sequential_count,
						1);
			} else {
				// 随机操作
				__xm_update_u64(&bio_info_p->random_count, 1);
			}
			// 累计块设备的操作字节数
			__xm_update_u64(&bio_info_p->bytes, nr_bytes);
		}
		// 计算 last sector
		bio_info_p->last_sector = sector + nr_sector;

		// 统计 blk request 错误次数
		if (error < 0) {
			__xm_update_u64(&bio_info_p->req_err_count, 1);
		}

		// 计算延时槽位
		slot = __xm_log2l(in_queue_delta);
		if (slot >= XM_BIO_REQ_LATENCY_MAX_SLOTS) {
			slot = XM_BIO_REQ_LATENCY_MAX_SLOTS - 1;
		}
		__sync_fetch_and_add(
			&bio_info_p->req_in_queue_latency_slots[slot], 1);

		slot = __xm_log2l(complete_delta);
		if (slot >= XM_BIO_REQ_LATENCY_MAX_SLOTS) {
			slot = XM_BIO_REQ_LATENCY_MAX_SLOTS - 1;
		}
		__sync_fetch_and_add(&bio_info_p->req_latency_slots[slot], 1);

		// 判断是否超过阈值
		if (delta >= __request_min_latency_ns) {
			struct xm_bio_request_latency_evt_data *evt =
				bpf_ringbuf_reserve(
					&xm_bio_req_latency_event_ringbuf_map,
					sizeof(struct xm_bio_request_latency_evt_data),
					0);
			if (evt) {
				// 进程信息需要从 xm_bio_request_pid_data_map 来获取
				struct bio_pid_data *bpd = bpf_map_lookup_elem(
					&xm_bio_request_pid_data_map, &rq);
				if (!bpd) {
					evt->comm[0] = '?';
				} else {
					evt->tid = bpd->tid;
					evt->pid = bpd->pid;
					__builtin_memcpy(evt->comm, bpd->comm,
							 sizeof(evt->comm));
				}

				evt->major = bio_key.major;
				evt->first_minor = bio_key.first_minor;
				evt->cmd_flags = bio_key.cmd_flags;
				evt->len = nr_bytes;
				if (stage_p->insert == 0) {
					evt->req_in_queue_latency_us = -1;
				} else {
					evt->req_in_queue_latency_us =
						in_queue_delta;
				}
				evt->req_latency_us = complete_delta;

				// bpf_printk("xm_ebpf_exporter bio request latency comm:'%s'"
				//            "req_in_queue_latency_us:%lld us, "
				//            "req_latency_us:%lld us",
				//            evt->comm, evt->req_in_queue_latency_us,
				//            evt->req_latency_us);

				bpf_ringbuf_submit(evt, 0);
			}
		}
	}

cleanup:
	bpf_map_delete_elem(&xm_bio_request_start_map, &rq);
	bpf_map_delete_elem(&xm_bio_request_pid_data_map, &rq);

	return 0;
}

// completed error code
#if 0
static const struct {
    int errno;
    const char *name;
} blk_errors[] = {
    [BLK_STS_OK] = { 0, "" },
    [BLK_STS_NOTSUPP] = { -EOPNOTSUPP, "operation not supported" },
    [BLK_STS_TIMEOUT] = { -ETIMEDOUT, "timeout" },
    [BLK_STS_NOSPC] = { -ENOSPC, "critical space allocation" },
    [BLK_STS_TRANSPORT] = { -ENOLINK, "recoverable transport" },
    [BLK_STS_TARGET] = { -EREMOTEIO, "critical target" },
    [BLK_STS_NEXUS] = { -EBADE, "critical nexus" },
    [BLK_STS_MEDIUM] = { -ENODATA, "critical medium" },
    [BLK_STS_PROTECTION] = { -EILSEQ, "protection" },
    [BLK_STS_RESOURCE] = { -ENOMEM, "kernel resource" },
    [BLK_STS_DEV_RESOURCE] = { -EBUSY, "device resource" },
    [BLK_STS_AGAIN] = { -EAGAIN, "nonblocking retry" },

    /* device mapper special case, should not leak out: */
    [BLK_STS_DM_REQUEUE] = { -EREMCHG, "dm internal retry" },

    /* zone device specific errors */
    [BLK_STS_ZONE_OPEN_RESOURCE] = { -ETOOMANYREFS, "open zones exceeded" },
    [BLK_STS_ZONE_ACTIVE_RESOURCE] = { -EOVERFLOW, "active zones exceeded" },

    /* everything else not covered above: */
    [BLK_STS_IOERR] = { -EIO, "I/O" },
}
#endif

char LICENSE[] SEC("license") = "Dual BSD/GPL";
