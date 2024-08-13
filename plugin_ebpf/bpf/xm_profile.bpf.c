/*
 * @Author: CALM.WU
 * @Date: 2023-08-16 15:15:40
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2024-08-13 17:19:19
 */

#pragma GCC diagnostic ignored "-Wint-conversion"

#include <vmlinux.h>
#include "../bpf_and_user.h"
#include "xm_bpf_helpers_common.h"
#include "xm_bpf_helpers_maps.h"
#include "xm_bpf_helpers_math.h"
#include "xm_bpf_helpers_filter.h"

// 在 FDETable 中二分查找对应 row 的最大深度，那么一个 FDETable 最多支持 2^8=256
// 有 row
#define MAX_FDE_TABLE_ROW_BIN_SEARCH_DEPTH 8
// 在 Module 中查找 FDEInfo
// 的最大深度，XM_PER_MODULE_FDE_TABLE_COUNT=8192，最大深度是 13
#define MAX_MODULE_FDE_TABLE_BIN_SEARCH_DEPTH 13

#ifndef INT_MAX
#define INT_MAX 2147483647
#endif

// prog 参数，过滤条件
BPF_ARRAY(xm_profile_arg_map, struct xm_prog_filter_args, 1);
// 堆栈计数
BPF_HASH(xm_profile_sample_count_map, struct xm_profile_sample,
	 struct xm_profile_sample_data, MAX_THREAD_COUNT);
// 堆栈
BPF_STACK_TRACE(xm_profile_stack_map, 1024);
// ehframe 用户堆栈
BPF_HASH(xm_profile_ehframe_user_stack_map, __u32, struct xm_ehframe_user_stack,
	 10240);
// 保存 pid 的内存映射信息，如果 pid 存在，说明需要执行来获取用户态堆栈
BPF_HASH(xm_profile_pid_modules_map, __u32, struct xm_profile_pid_maps, 256);
// 解决：error: Looks like the BPF stack limit of 512 bytes is
// exceeded，因为结构太大，不能作为局部变量
BPF_PERCPU_ARRAY(xm_unwind_user_stack_data_heap,
		 struct xm_unwind_user_stack_resolve_data, 1);

// 保存每个 module 的 fde table 信息，内核对 hash_map 的 value 有大小限制
// if ((u64)attr->key_size + attr->value_size >= KMALLOC_MAX_SIZE -
//    sizeof(struct htab_elem))
// 	 if key_size + value_size is bigger, the user space won't be
// 	 * able to access the elements via bpf syscall. This check
// 	 * also makes sure that the elem_size doesn't overflow and it's
// 	 * kmalloc-able later in htab_map_update_elem()
//
// 	return -E2BIG;
// key is hash(buildid)
BPF_HASH(xm_profile_module_fdetables_map, __u64,
	 struct xm_profile_module_fde_tables, 256);
// 尾调
BPF_PROG_ARRAY(xm_profile_walk_stack_progs_ary, 1);

// in compile print struct sizeof
char (*__kaboom)[sizeof(struct xm_profile_module_fde_tables)] = 1;

const enum xm_prog_filter_target_scope_type __unused_filter_scope_type
	__attribute__((unused)) = XM_PROG_FILTER_TARGET_SCOPE_TYPE_NONE;

const struct xm_profile_sample *__unused_ps __attribute__((unused));
const struct xm_profile_sample_data *__unused_psd __attribute__((unused));
const struct xm_profile_fde_table_row *__unused_pfr __attribute__((unused));
const struct xm_profile_fde_table_info *__unused_pft __attribute__((unused));
const struct xm_profile_proc_maps_module *__unused_pmm __attribute__((unused));
const struct xm_profile_pid_maps *__unused_pm __attribute__((unused));
const struct xm_profile_module_fde_tables *__unused_pmft
	__attribute__((unused));
const struct xm_ehframe_user_stack *__unused_eus __attribute__((unused));

static const __u64 __zero = 0;
static const struct xm_profile_sample_data __init_psd = {
	.count = 1,
	.pid_ns = 0,
	.pgid = 0,
};

// https://github.com/abrandoned/murmur2/blob/master/MurmurHash2.c
static __always_inline __u32 __murmur_hash2_user_stack_id(const void *key,
							  __s32 len)
{
	const __u32 m = 0x5bd1e995;
	const __s32 r = 24;
	const __u32 seed = 0;

	/* Initialize the hash to a 'random' value */

	__u32 h = seed ^ len;

	/* Mix 4 bytes at a time into the hash */

	const unsigned char *data = (const unsigned char *)key;
	// 最长不会超过 256 个 u32
	for (__s32 i = 0; i < 256; i++) {
		if (len < 4) {
			break;
		}
		__u32 k = *(__u32 *)data;

		k *= m;
		k ^= k >> r;
		k *= m;

		h *= m;
		h ^= k;

		data += 4;
		len -= 4;
	}

	switch (len) {
	case 3:
		h ^= data[2] << 16;
	case 2:
		h ^= data[1] << 8;
	case 1:
		h ^= data[0];
		h *= m;
	};

	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;

	return h;
}

// 获取用户 task_struct 用户空间的寄存器，为了使用 eh_frame 的信息做 stack
// unwind
static __always_inline void
	__xm_get_task_userspace_regs(struct task_struct *task,
				     struct pt_regs *user_regs,
				     struct xm_task_userspace_regs *tu_regs)
{
	// 获取指令寄存器的值，判断了是否是内核地址空间
	if (in_kernel_space(PT_REGS_IP(user_regs))) {
		// !! 由于系统调用在进程切换到内核地址空间，需要按 ptrace
		// 的方式，来获取保存在 stack 中的用户态寄存器
		__u64 __ptr;
		__ptr = (__u64)READ_KERN(task->stack);
		__ptr += THREAD_SIZE - TOP_OF_KERNEL_STACK_PADDING;
		struct pt_regs *regs = ((struct pt_regs *)__ptr) - 1;
		tu_regs->rip = PT_REGS_IP_CORE(regs);
		tu_regs->rsp = PT_REGS_SP_CORE(regs);
		tu_regs->rbp = PT_REGS_FP_CORE(regs);
	} else {
		// perf event
		// snapshot 当前 task_struct 在用户地址空间，直接从 cur_regs 中获取
		tu_regs->rip = PT_REGS_IP(user_regs);
		tu_regs->rsp = PT_REGS_SP(user_regs);
		tu_regs->rbp = PT_REGS_FP(user_regs);
	}
}

#define BINARY_SEARCH_DEFAULT_RESULT 0xABABABABABABABAB

// int element = sortedArray[index];
// !!R8 unbounded memory access, make sure to bounds check any such access
static __always_inline __s32
	__bsearch_fde_table_row(const struct xm_profile_fde_table_row *rows,
				__u64 left, __u64 right, __u64 address)
{
	__u64 result = BINARY_SEARCH_DEFAULT_RESULT;
	__u64 mid = 0;
	// 最大深度限制查找 rows 的范围
	for (__u32 i = 0; i < MAX_FDE_TABLE_ROW_BIN_SEARCH_DEPTH; i++) {
		if (left > right) {
			break;
		}
		mid = left + (right - left) / 2;

		if (mid < 0 || mid >= XM_PER_MODULE_FDE_ROWS_COUNT) {
			return -1;
		}

		if (rows[mid].loc <= address) {
			result = mid;
			left = mid + 1;
		} else {
			right = mid - 1;
		}
	}
	return result;
}

static __always_inline const struct xm_profile_fde_table_info *
	__find_fde_table_info_in_module(
		const struct xm_profile_module_fde_tables *module_tables,
		__u64 address)
{
	if (module_tables->fde_table_count < XM_PER_MODULE_FDE_TABLE_COUNT) {
		__u64 left = 0;
		__u64 right = module_tables->fde_table_count - 1;
		__u64 result = BINARY_SEARCH_DEFAULT_RESULT;
		__u64 mid = 0;
		const struct xm_profile_fde_table_info *fde_table_infos =
			module_tables->fde_infos;

		for (__s32 i = 0; i < MAX_MODULE_FDE_TABLE_BIN_SEARCH_DEPTH;
		     i++) {
			if (left > right) {
				break;
			}
			mid = left + (right - left) / 2;
			// **fix:math between map_value pointer and register with unbounded
			// **min value is not allowed
			if (mid < 0 || mid >= XM_PER_MODULE_FDE_TABLE_COUNT) {
				return 0;
			}
			if (fde_table_infos[mid].start <= address) {
				result = mid;
				left = mid + 1;
			} else {
				right = mid - 1;
			}
		}

		if (result != BINARY_SEARCH_DEFAULT_RESULT) {
			return &module_tables->fde_infos[result];
		}
	}
	return 0;
}

// struct xm_find_module_ctx {
//     __u64 pc;
//     const struct xm_profile_pid_maps *maps;
// };

// static __always_inline __s32 check_module(__u32 index, void *ctx) {
//     const struct xm_find_module_ctx *find_ctx =
//         (const struct xm_find_module_ctx *)ctx;
//     const struct xm_profile_proc_maps_module *module =
//         &find_ctx->maps->modules[index];

//     if (module->start_addr <= find_ctx->pc
//         && find_ctx->pc <= module->end_addr) {
//         // 判断 pc 在/proc/pid/maps 对应的 module 地址范围内
//         return 1;
//     }

//     // continue
//     return 0;
// }

static __always_inline const struct xm_profile_proc_maps_module *
	__find_module_in_maps(const struct xm_profile_pid_maps *maps, __u64 pc)
{
	// struct xm_find_module_ctx ctx = {
	//     .pc = pc,
	//     .maps = maps,
	// };

	// __s32 pos = bpf_loop(XM_PER_PROCESS_ASSOC_MODULE_COUNT,
	//                      (void *)&check_module, (void *)&ctx, 0);
	// if (pos >= 0 && pos < XM_PER_PROCESS_ASSOC_MODULE_COUNT)
	//     return &maps->modules[pos];
	for (__u32 i = 0; i < XM_PER_PROCESS_ASSOC_MODULE_COUNT; i++) {
		const struct xm_profile_proc_maps_module *module =
			&maps->modules[i];
		if (module->start_addr <= pc && pc <= module->end_addr) {
			// 判断 pc 在/proc/pid/maps 对应的 module 地址范围内
			return module;
		}
	}

	return 0;
}

static __always_inline const struct xm_profile_fde_table_row *
	__find_fde_table_row_by_pc(const struct xm_profile_pid_maps *pid_maps,
				   __u64 pc)
{
	const struct xm_profile_proc_maps_module *module = 0;
	const struct xm_profile_module_fde_tables *module_fde_tables = 0;
	const struct xm_profile_fde_table_info *module_fde_table_info = 0;
	__u64 addr = pc;
	__s32 find_row_pos = 0;

	// 根据 rip 查找对应的 module
	module = __find_module_in_maps(pid_maps, pc);
	if (module) {
		bpf_printk("\tfind module-{start:0x%lx...end:0x%lx}, type:%d",
			   module->start_addr, module->end_addr, module->type);

		if (module->type == so) {
			addr -= module->start_addr;
			bpf_printk("\tmodule type is 'so', adjust(addr -= "
				   "module->start_addr) ==> 0x%lx",
				   addr);
		}
		// 根据 module 的 build_id_hash 查找对应的 module
		module_fde_tables =
			bpf_map_lookup_elem(&xm_profile_module_fdetables_map,
					    &(module->build_id_hash));

		if (module_fde_tables) {
			// bpf_printk("\tmodule build_id_hash:'%llu' in "
			//            "xm_profile_module_fde_tables",
			//            module->build_id_hash);

			// 根据 addr 查找对应的 fde table
			module_fde_table_info = __find_fde_table_info_in_module(
				module_fde_tables, addr);
			if (module_fde_table_info) {
				bpf_printk(
					"\taddr:0x%lx in "
					"fde_table-{start:0x%lx...end:0x%lx}",
					addr, module_fde_table_info->start,
					module_fde_table_info->end);

				// 根据 addr 查找对应的 fde table row
				if ((module_fde_table_info->row_pos +
				     module_fde_table_info->row_count) <=
				    XM_PER_MODULE_FDE_ROWS_COUNT) {
					find_row_pos = __bsearch_fde_table_row(
						module_fde_tables->fde_rows,
						module_fde_table_info->row_pos,
						module_fde_table_info->row_pos +
							module_fde_table_info
								->row_count -
							1,
						addr);
					if (find_row_pos !=
						    BINARY_SEARCH_DEFAULT_RESULT &&
					    find_row_pos >= 0 &&
					    find_row_pos <
						    XM_PER_MODULE_FDE_ROWS_COUNT) {
						bpf_printk(
							"\taddr:0x%lx at fde_table %d row.",
							addr,
							find_row_pos -
								module_fde_table_info
									->row_pos);
						return &module_fde_tables->fde_rows
								[find_row_pos];
					} else {
						bpf_printk(
							"\t[err] addr:0x%lx does not have a "
							"corresponding row in the fde_table. table "
							"row count:%d ",
							addr,
							module_fde_table_info
								->row_count);
					}
				}
			} else {
				bpf_printk(
					"\t[err] addr:0x%lx not found in the corresponding "
					"fde_table",
					addr);
			}
		} else {
			bpf_printk(
				"\t[err] module-{start:0x%lx...end:0x%lx}, "
				"build_id_hash:%d not in xm_profile_module_fde_tables",
				module->start_addr, module->end_addr,
				module->build_id_hash);
		}
	} else {
		bpf_printk(
			"\t[err] addr:0x%lx not found in the corresponding module.",
			pc);
	}
	return 0;
}

static __always_inline __s32 __get_user_stack(
	struct bpf_perf_event_data *ctx,
	const struct xm_profile_pid_maps *pid_maps, struct task_struct *task,
	struct xm_unwind_user_stack_resolve_data *uwind_usi)
{
	//
	const struct xm_profile_fde_table_row *row = 0;
	__u64 rip = uwind_usi->regs.rip, prev_rip_addr = 0,
	      rsp = uwind_usi->regs.rsp, rbp = uwind_usi->regs.rbp,
	      prev_rbp_addr = 0, cfa = 0;
	__s32 ret = 0;
	bool reached_bottom = false;

	for (__s32 i = 0; i < XM_MAX_STACK_DEPTH_PER_PROGRAM; i++) {
		bpf_printk("\twalk to frame:%d, current rip:0x%lx",
			   uwind_usi->e_st.len, rip);

		row = __find_fde_table_row_by_pc(pid_maps, rip);
		if (row) {
			// 找到 pc 在 FDETables 中对应的 row
			// bpf_printk("\t   current rip correspond with fdeTable row "
			//            "loc:0x%lx cfa{reg_num:%d, offset:%d}",
			//            row->loc, row->cfa.reg, row->cfa.offset);
			// bpf_printk("\t   rbp_cfa_offset:%d, ra_cfa_offset:%d***",
			//            row->rbp_cfa_offset, row->ra_cfa_offset);

			// 开始计算 cfa，也是上一帧的 rsp
			// AMD64_Rbp     = 6
			// AMD64_Rsp     = 7
			if (row->cfa.reg == 7) {
				cfa = rsp + row->cfa.offset;
			} else if (row->cfa.reg == 6) {
				cfa = rbp + row->cfa.offset;
			} else {
				ret = -1;
				bpf_printk(
					"\t[err] loc:0x%lx, cfa reg:%d not support.",
					row->loc, row->cfa.reg);
				break;
			}
			bpf_printk("\tframe:%d cfa:0x%lx", uwind_usi->e_st.len,
				   cfa);

			if (row->rbp_cfa_offset != INT_MAX) {
				// 通过 cfa + rbp_cfa_offset 来计算 上一帧 rbp
				// 在栈空间的地址
				prev_rbp_addr = cfa + row->rbp_cfa_offset;
				// bpf_printk("\t   frame:%d rbp addr:0x%lx",
				//            uwind_usi->e_st.len, prev_rbp_addr);
				// 读取上一帧 rbp 的值
				ret = bpf_probe_read_user(
					&rbp, 8, (void *)prev_rbp_addr);
				if (ret < 0) {
					bpf_printk(
						"\t[err] read frame:%d rbp at "
						"prev_rbp_addr:0x%lx failed. err:%d",
						uwind_usi->e_st.len,
						prev_rbp_addr, ret);
					break;
				} else {
					bpf_printk(
						"\tframe:%d prev_rbp_addr:0x%lx, rbp:0x%lx",
						uwind_usi->e_st.len,
						prev_rbp_addr, rbp);
					// TODO: 如果 rbp == 0，说明到了最低的 call frame
					if (rbp == 0) {
						bpf_printk(
							"\t[warn] rbp value is 0, reached bottom");
						reached_bottom = true;
					}
				}
			} else {
				// !! 这里不应该标识为达到 bottom
				// reached_bottom = true;
				bpf_printk(
					"\t[warn] rbp_cfa_offset is INT_MAX");
			}

			if (row->ra_cfa_offset != INT_MAX) {
				// 通过 cfa + ra_cfa_offset 来计算上一帧 rip
				prev_rip_addr = cfa + row->ra_cfa_offset;
				// bpf_printk("\tframe:%d rip addr:0x%lx", uwind_usi->e_st.len,
				//            prev_rip_addr);
				// 读取上一帧 rip 的值
				ret = bpf_probe_read_user(
					&rip, 8, (void *)prev_rip_addr);
				if (ret < 0) {
					bpf_printk(
						"\t[err] read frame:%d rip at "
						"prev_rip_addr:0x%lx failed. err:%d",
						uwind_usi->e_st.len,
						prev_rip_addr, ret);
					break;
				} else {
					bpf_printk(
						"\tframe:%d prev_rip_addr:0x%lx, rip:0x%lx",
						uwind_usi->e_st.len,
						prev_rip_addr, rip);

					if (rip == 0) {
						bpf_printk(
							"\t[warn] rip should not be zero");
						ret = -1;
						break;
					}
				}
			} else {
				reached_bottom = true;
			}

			// ** math between map_value pointer and register with unbounded min
			// ** value is not allowed
			if (uwind_usi->e_st.len < PERF_MAX_STACK_DEPTH) {
				uwind_usi->e_st.pcLst[uwind_usi->e_st.len] =
					rip;
				__xm_update_u64(&uwind_usi->e_st.len, 1);
				// uwind_usi->e_st.len++;
				//  保存寄存器，为下一个尾调做准备
				uwind_usi->regs.rbp = rbp;
				uwind_usi->regs.rip = rip;
				uwind_usi->regs.rsp = cfa;
			}

			if (!reached_bottom) {
				rsp = cfa;
			} else {
				// 到了最低的 call frame
				bpf_printk("\treached bottom, stack depth:%d",
					   uwind_usi->e_st.len);
				break;
			}
		} else {
			bpf_printk(
				"\t[err] current rip:0x%lx is invalid, stack depth:%d",
				rip, uwind_usi->e_st.len);
			ret = -1;
			break;
		}
	}

	if (ret == 0 && !reached_bottom &&
	    uwind_usi->e_st.len < PERF_MAX_STACK_DEPTH &&
	    uwind_usi->tail_call_count < MX_MAX_TAIL_CALL_COUNT - 1) {
		bpf_printk(
			"\t   Continue tail "
			"call:'xm_walk_user_stacktrace':times(%d) for pid:%d--->",
			uwind_usi->tail_call_count, uwind_usi->pid);
		// !!misaligned value access off (0x0; 0x0)+0+1052 size 8
		//__xm_update_u64(&uwind_usi->tail_call_count, 1);
		uwind_usi->tail_call_count++;
		bpf_tail_call(ctx, &xm_profile_walk_stack_progs_ary, 0);
	}

	return ret;
}

SEC("perf_event")
__s32 xm_walk_user_stacktrace(struct bpf_perf_event_data *ctx)
{
	struct xm_unwind_user_stack_resolve_data *uwind_usi =
		bpf_map_lookup_elem(&xm_unwind_user_stack_data_heap, &__zero);
	if (uwind_usi) {
		bpf_printk(
			"--->Enter tail call:'xm_walk_user_stacktrace':times(%d) "
			"for pid:%d",
			uwind_usi->tail_call_count, uwind_usi->pid);
		struct xm_profile_pid_maps *pid_maps = bpf_map_lookup_elem(
			&xm_profile_pid_modules_map, &(uwind_usi->pid));
		if (pid_maps) {
			struct task_struct *ts =
				(struct task_struct *)bpf_get_current_task();

			__s32 ret =
				__get_user_stack(ctx, pid_maps, ts, uwind_usi);
			if (ret == 0) {
				struct xm_profile_sample ps = {
					.pid = uwind_usi->pid,
					.kernel_stack_id = -1,
					.user_stack_id = -1,
					.ehframe_user_stack_id = 0,
					.comm[0] = '\0',
				};

				// 计算 user 堆栈的 hash 值
				ps.ehframe_user_stack_id =
					__murmur_hash2_user_stack_id(
						(__u32 *)uwind_usi->e_st.pcLst,
						PERF_MAX_STACK_DEPTH *
							2); // sizeof(__u64) /
				// sizeof(__u32));

				// 将 xm_ehframe_user_stack 放入 hashmap 中
				__s32 ret = bpf_map_update_elem(
					&xm_profile_ehframe_user_stack_map,
					(const void *)&ps.ehframe_user_stack_id,
					(const void *)&uwind_usi->e_st,
					BPF_ANY);
				if (ret != 0) {
					bpf_printk(
						"update xm_profile_ehframe_user_stack_map "
						"failed, err:%d",
						ret);
				} else {
					ps.kernel_stack_id = bpf_get_stackid(
						ctx, &xm_profile_stack_map,
						KERN_STACKID_FLAGS);
					BPF_CORE_READ_STR_INTO(&ps.comm, ts,
							       group_leader,
							       comm);
					// 查询
					struct xm_profile_sample_data *val =
						__xm_bpf_map_lookup_or_try_init(
							(void *)&xm_profile_sample_count_map,
							&ps, &__init_psd);
					if (val) {
						// 进程堆栈计数递增
						__sync_fetch_and_add(
							&(val->count), 1);
						val->pid_ns =
							__xm_get_task_pidns(ts);
						val->pgid =
							__xm_get_task_pgid(ts);
						// bpf_printk("xm_walk_user_stacktrace completed,
						// pid:%d, "
						//            "ehframe_user_stack_id:%u",
						//            uwind_usi->pid, ps.ehframe_user_stack_id);
					}
				}
			}
			bpf_printk(
				"Complete tail call:'xm_walk_user_stacktrace':times(%d) "
				"for pid:%d, stack depth:%d<---\n",
				uwind_usi->tail_call_count, uwind_usi->pid,
				uwind_usi->e_st.len);
		} else {
			bpf_printk("pid:%d not in xm_profile_pid_modules_map",
				   uwind_usi->pid);
		}
	}

	return 0;
}

SEC("perf_event") __s32 xm_do_perf_event(struct bpf_perf_event_data *ctx)
{
	pid_t tid, pid;
	struct xm_profile_pid_maps *pid_maps = 0;

	struct xm_profile_sample ps = {
		.pid = 0,
		.kernel_stack_id = -1,
		.user_stack_id = -1,
		.ehframe_user_stack_id = 0,
		.comm[0] = '\0',
	};
	struct xm_profile_sample_data *val = 0;

	// 得到线程 id
	tid = __xm_get_tid();
	// 得到进程 id
	pid = __xm_get_pid();

	// perf 的快照没有 catch 到执行线程
	if (tid == 0) {
		return 0;
	}
	// 只关注用户进程
	if (is_kthread()) {
		return 0;
	}

	// if (pid == 0) {
	//     bpf_printk("xm_do_perf_event this kernel thread:%d", tid);
	// }

	// 判断过滤条件
	struct task_struct *ts = (struct task_struct *)bpf_get_current_task();
	if (!filter_ts(&xm_profile_arg_map, ts, pid)) {
		return 0;
	}

	// **如果执行程序支持 frame point 才能用这种方式，否则要用 eh_frame +
	// **ip、bp、sp 来做 stack unwind
	// 判断是否需要通过解析 .eh_frame 的数据来做 unwind stack
	pid_maps = bpf_map_lookup_elem(&xm_profile_pid_modules_map, &pid);
	// 获取
	struct xm_unwind_user_stack_resolve_data *uwind_usi =
		bpf_map_lookup_elem(&xm_unwind_user_stack_data_heap, &__zero);

	if (pid_maps && uwind_usi) {
		// 初始化 uwind user stack 数据对象
		uwind_usi->pid = pid;
		// 获取当前的寄存器，这是 top frame 当前执行的指令地址
		__xm_get_task_userspace_regs(ts, &ctx->regs, &uwind_usi->regs);
		bpf_printk("start unwind stack rip:0x%lx, rsp:0x%lx, rbp:0x%x",
			   uwind_usi->regs.rip, uwind_usi->regs.rsp,
			   uwind_usi->regs.rbp);
		// 初始化所有程序计数器
		for (__s32 i = 0; i < PERF_MAX_STACK_DEPTH; i++) {
			uwind_usi->e_st.pcLst[i] = 0;
		}
		uwind_usi->e_st.len = 1;
		uwind_usi->e_st.pcLst[0] = uwind_usi->regs.rip;
		uwind_usi->tail_call_count = 0;
		// 尾调，跳转到自定义 unwind stack 函数中
		bpf_tail_call(ctx, &xm_profile_walk_stack_progs_ary, 0);
		return 0;
	}

	ps.pid = pid;
	BPF_CORE_READ_STR_INTO(&ps.comm, ts, group_leader, comm);

	ps.user_stack_id =
		bpf_get_stackid(ctx, &xm_profile_stack_map, BPF_F_USER_STACK);
	//  获取内核堆栈
	ps.kernel_stack_id =
		bpf_get_stackid(ctx, &xm_profile_stack_map, KERN_STACKID_FLAGS);

	val = __xm_bpf_map_lookup_or_try_init(
		(void *)&xm_profile_sample_count_map, &ps, &__init_psd);
	if (val) {
		// 进程堆栈计数递增
		__sync_fetch_and_add(&(val->count), 1);
		val->pid_ns = __xm_get_task_pidns(ts);
		val->pgid = __xm_get_task_pgid(ts);
	}

	return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";

// !! processed 57970 insns (limit 1000000) max_states_per_insn 4 total_states
// !! 1110 peak_states 1109 mark_read 36

// !! The sequence of 8193 jumps is too complex.

// !! processed 1000001 insns (limit 1000000) max_states_per_insn 19
// !! total_states
// !! 20748 peak_states 1257 mark_read 33
