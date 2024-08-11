/*
 * @Author: CALM.WU
 * @Date: 2024-08-08 14:10:09
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2024-08-08 18:17:45
 */

#define pr_fmt(fmt) "%s:%s():%d: " fmt, KBUILD_MODNAME, __func__, __LINE__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/sched.h>

// 如果编译没有代入版本信息
#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#else
#define KERNEL_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + (c))
#endif

#define MODULE_TAG "Module:[seqfile_test]"

static loff_t __max_seq_num = 3;

static void *num_seq_start(struct seq_file *s, loff_t *pos)
{
    loff_t *seq_num = NULL;

    if (*pos == 0) {
        pr_info(MODULE_TAG " seq iteration started--->\n");

        seq_num = kmalloc(sizeof(loff_t), GFP_KERNEL);
        if (!seq_num)
            return NULL;

        *seq_num = *pos;
        seq_printf(s, "Start\n");
        return seq_num;
    } else {
        if (*pos > __max_seq_num) {
            pr_info(MODULE_TAG " seq iteration reached the end\n");
        }
    }

    return NULL;
}

static int32_t num_seq_show(struct seq_file *s, void *v)
{
    loff_t *seq_num = NULL;
    seq_num = (loff_t *)v;
    if (*seq_num <= __max_seq_num) {
        seq_printf(s, "%lld\n", (long long)*seq_num);
        pr_info(MODULE_TAG " seq iteration show:(%lld)\n", *seq_num);
        return 0;
    }
    // 返回非零，结束迭代，会调用 stop 函数
    return -1;
}

// 返回指向下一个数据项的指针，如果没有更多数据项可读取则返回 NULL。
// v: start 或前一次 next 函数返回的指向当前数据项的指针
static void *num_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
    loff_t *seq_num = NULL;

    seq_num = (loff_t *)v;
    *pos = ++(*seq_num);
    pr_info(MODULE_TAG " seq iteration next:(%lld)\n", *seq_num);
    // 返回的指针在下一次迭代中作为 show 的 v 参数传入
    return seq_num;
}

static void num_seq_stop(struct seq_file *s, void *v)
{
    if (v) {
        kfree(v);
        seq_printf(s, "End\n");
        pr_info(MODULE_TAG " seq iteration stop<---\n");
    }
}

static struct seq_operations num_seq_ops = {
    .start = num_seq_start,
    .next = num_seq_next,
    .stop = num_seq_stop,
    .show = num_seq_show,
};

//
static int32_t num_seq_open(struct inode *inode, struct file *file)
{
    // 在文件 open 时设置 sequence ops
    return seq_open(file, &num_seq_ops);
    /*
    private data 的绑定
    struct seq_file *seq = file->private_data;
    seq->private = PDE_DATA(inode);
    */
}

static struct file_operations num_file_ops = {
    .owner = THIS_MODULE,
    .open = num_seq_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = seq_release,
};

static void *tasks_seq_start(struct seq_file *s, loff_t *pos)
{
    struct task_struct *p = NULL;

    return NULL;
}

static void *tasks_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
    return NULL;
}

static void tasks_seq_stop(struct seq_file *s, void *v)
{
    return;
}

static int32_t tasks_seq_show(struct seq_file *s, void *v)
{
    return 0;
}

static struct seq_operations tasks_seq_ops = {
    .start = tasks_seq_start,
    .next = tasks_seq_next,
    .stop = tasks_seq_stop,
    .show = tasks_seq_show,
};

static int32_t tasks_seq_open(struct inode *inode, struct file *file)
{
    return seq_open(file, &tasks_seq_ops);
}

static struct file_operations tasks_file_ops = {
    .owner = THIS_MODULE,
    .open = tasks_seq_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = seq_release,
};

static int32_t __init cw_seqfile_test_init(void)
{
    // /proc/seq_task
    if (!proc_create("seq_num", 0644, NULL, &num_file_ops)) {
        pr_err(MODULE_TAG " create proc file failed!\n");
        return -ENOMEM;
    }
    pr_info(MODULE_TAG " init successfully!\n");
    return 0;
}

static void __exit cw_seqfile_test_exit(void)
{
    remove_proc_entry("seq_num", NULL);
    pr_info(MODULE_TAG " bye!\n");
}

module_init(cw_seqfile_test_init);
module_exit(cw_seqfile_test_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("calmwu <wubo0067@hotmail.com>");
MODULE_DESCRIPTION("ioctl test");
MODULE_VERSION("0.1");