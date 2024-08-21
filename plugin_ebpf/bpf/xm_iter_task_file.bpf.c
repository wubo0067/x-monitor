/*
 * @Author: CALM.WU
 * @Date: 2024-08-13 15:10:57
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2024-08-13 18:23:23
 */

// 使用 bpf iter 特性，创建 seq_file 来输出所有进程打开的文件

#include <vmlinux.h>
#include "../bpf_and_user.h"
#include "xm_bpf_helpers_common.h"
#include "xm_bpf_helpers_maps.h"

#define PATH_MAX 4096

BPF_PERCPU_ARRAY(tmp_storage, char[PATH_MAX], 1);
static const __u64 __zero = 0;

// 怎么确认内核支持哪些 bpf iter 特性，可以在 vmlinux.h 文件中查找 struct bpf_iter__xxx，xxx 就代表支持的特性
SEC("iter/task_file")
int32_t xm_iter_task_file(struct bpf_iter__task_file *ctx)
{
	char *path = NULL;
	uint64_t nr_pages = 0;
	struct seq_file *seq = ctx->meta->seq;
	const struct task_struct *ts = ctx->task;
	uint32_t fd = ctx->fd;
	struct file *file = ctx->file;

	if (ts == NULL || file == NULL)
		return 0;

	// 如果 seq_num 是 0，表示是第一行，输出 header
	if (ctx->meta->seq_num == 0) {
		BPF_SEQ_PRINTF(
			seq,
			"            comm     tgid      pid       fd nr_pages             file\n");
	}

	// 通过 struct path 获取文件路径
	path = bpf_map_lookup_elem(&tmp_storage, &__zero);
	if (path) {
		// 获取文件占用 page 数
		if (file->f_mapping) {
			nr_pages = file->f_mapping->nrpages;
		}

		// 获取文件路径
		bpf_d_path(&(file->f_path), path, PATH_MAX);
		// file->f_path.dentry->d_name.name,

		BPF_SEQ_PRINTF(seq, "%16s %8d %8d %8d %8d %16s\n", ts->comm,
			       ts->tgid, ts->pid, fd, nr_pages, path);
	}

	bpf_printk("seq_num[%d], size:%zu, count:%zu", ctx->meta->seq_num,
		   ctx->meta->seq->size, ctx->meta->seq->count);

	return 0;
}

char _license[] SEC("license") = "GPL";

/*
26: (85) call bpf_d_path#147
invalid indirect read from stack R2 off -128+0 size 128
如果直接分配 char path[128], 则会出现上面的报错，应该 path_max 的大小应该是 4096，而且分配在堆栈上不安全，校验器不允许
改为从 BPF_PERCPU_ARRAY 分配一个 buf 就可以了

bpftool iter pin ./xm_iter_task_file.bpf.o  /sys/fs/bpf/task_file
*/