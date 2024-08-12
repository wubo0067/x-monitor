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
#include <linux/compiler.h>

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
    }
    pr_info(MODULE_TAG " seq iteration stop<---\n");
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

static const char *thrdlist_hdr =
        "-------------------------------------------------------------------------------------------------------\n"
        "    TGID     PID        STATE         current           stack-start         Thread Name     MT? # thrds\n"
        "-------------------------------------------------------------------------------------------------------\n";

struct thrds_seq_data {
    struct task_struct *p;
    struct task_struct *t;
};

static int32_t reach_end = 0;

static void *thrds_seq_start(struct seq_file *s, loff_t *pos)
{
    loff_t index = *pos;
    struct task_struct *p = NULL, *t = NULL;
    struct thrds_seq_data *data = NULL;

    if (*pos == 0) {
        // print header
        seq_printf(s, "%s", thrdlist_hdr);
        data = kmalloc(sizeof(struct thrds_seq_data), GFP_KERNEL);
        if (!data) {
            return NULL;
        }
        data->p = &init_task;
        data->t = &init_task;
        pr_info(MODULE_TAG
                " thrds seq iteration start pos:(%lld), task:%d--->\n",
                *pos, data->p->pid);
        return data;
    } else {
        if (*((int32_t *)(s->private)) == 1) {
            pr_info(MODULE_TAG
                    " thrds seq iteration next pos:(%lld) reach the task end\n",
                    *pos);
            return NULL;
        }
        // 从头跳过 index 个线程
        do_each_thread(p, t)
        {
            index--;
            if (index == 0) {
                goto OUT;
            }
        }
        while_each_thread(p, t);

    OUT:
        if (p != &init_task) {
            data = kmalloc(sizeof(struct thrds_seq_data), GFP_KERNEL);
            if (!data) {
                return NULL;
            }
            data->p = p;
            data->t = t;
            pr_info(MODULE_TAG
                    " thrds seq iteration next pos:(%lld), task:%d--->\n",
                    *pos, data->p->pid);
            return data;
        }
    }

    return NULL;
}

static void *thrds_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
    struct thrds_seq_data *data = (struct thrds_seq_data *)v;
    struct task_struct *p = data->p;
    struct task_struct *t = data->t;

    data->t = next_thread(t);
    if (data->t == p) {
        data->p = next_task(p);
        data->t = data->p;
        pr_info(MODULE_TAG " thrds seq iteration, change to next task:(%d)\n",
                data->p->pid);
    } else {
        pr_info(MODULE_TAG
                " thrds seq iteration, the next thrd:(tgid:%d,pid:%d,thrid:%d)\n",
                data->p->tgid, data->p->pid, data->t->pid);
    }

    if (*pos != 0 && data->p == &init_task) {
        // 如果已经遍历完所有线程，则返回 NULL
        kfree(data);
        *((int32_t *)(s->private)) = 1;
        pr_info(MODULE_TAG " thrds seq iteration reach the end\n");
        return NULL;
    }
    // } else {
    //     pr_info(MODULE_TAG " thrds seq iteration next pos:(%lld)\n", *pos);
    // }

    ++(*pos);
    return data;
}

static void thrds_seq_stop(struct seq_file *s, void *v)
{
    if (v) {
        kfree(v);
        pr_info(MODULE_TAG " thrds seq iteration free data\n");
    }
    pr_info(MODULE_TAG " thrds seq iteration stop<---\n");
    return;
}

static const char *task_state_to_string(long state)
{
    switch (state) {
    case TASK_RUNNING:
        return "RUNNING";
    case TASK_INTERRUPTIBLE:
        return "INTERRUPTIBLE";
    case TASK_UNINTERRUPTIBLE:
        return "UNINTERRUPTIBLE";
    case __TASK_STOPPED:
        return "STOPPED";
    case __TASK_TRACED:
        return "TRACED";
    case TASK_DEAD:
        return "DEAD";
    case TASK_WAKEKILL:
        return "WAKEKILL";
    case TASK_WAKING:
        return "WAKING";
    case TASK_PARKED:
        return "PARKED";
    case TASK_NOLOAD:
        return "NOLOAD";
    case TASK_NEW:
        return "NEW";
    default:
        return "UNKNOWN";
    }
}

static int32_t thrds_seq_show(struct seq_file *s, void *v)
{
    int32_t nr_thrds = 0;
    long state = 0;

    struct thrds_seq_data *data = (struct thrds_seq_data *)v;
    struct task_struct *p = data->p;
    struct task_struct *t = data->t;

    get_task_struct(t);
    task_lock(t);

    state = READ_ONCE((t)->__state);

    pr_info(MODULE_TAG " thrds seq iteration show pid:(%d)\n", t->pid);
    // 获取当前进程的线程数
    nr_thrds = get_nr_threads(t);
    if (!p->mm) {
        seq_printf(s, "%8d %8d [%16s]  0x%px 0x%px [%16s]\n", p->tgid, t->pid,
                   task_state_to_string(state), t, t->stack, t->comm);
    } else {
        if ((p->tgid == t->pid) && (nr_thrds > 1)) {
            seq_printf(s, "%8d %8d [%16s]  0x%px 0x%px %16s  %3d\n", p->tgid,
                       t->pid, task_state_to_string(state), t, t->stack,
                       t->comm, nr_thrds);
        } else {
            seq_printf(s, "%8d %8d [%16s]  0x%px 0x%px %16s\n", p->tgid, t->pid,
                       task_state_to_string(state), t, t->stack, t->comm);
        }
    }
    task_unlock(t);
    put_task_struct(t);

    return 0;
}

static struct seq_operations thrds_seq_ops = {
    .start = thrds_seq_start,
    .next = thrds_seq_next,
    .stop = thrds_seq_stop,
    .show = thrds_seq_show,
};

static int32_t thrds_seq_open(struct inode *inode, struct file *file)
{
    int32_t ret;
    ret = seq_open(file, &thrds_seq_ops);
    if (!ret) {
        struct seq_file *seq = file->private_data;
        seq->private = &reach_end;
        rcu_read_lock();
    }
    pr_info(MODULE_TAG " open /proc/seq_thrds\n");
    return ret;
}

static int32_t thrds_seq_release(struct inode *inode, struct file *file)
{
    pr_info(MODULE_TAG " release /proc/seq_thrds\n");
    reach_end = 0;
    rcu_read_unlock();
    return seq_release(inode, file);
}

static struct file_operations thrds_file_ops = {
    .owner = THIS_MODULE,
    .open = thrds_seq_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = thrds_seq_release,
};

static int32_t __init cw_seqfile_test_init(void)
{
    // /proc/seq_task
    if (!proc_create("seq_num", 0644, NULL, &num_file_ops)) {
        pr_err(MODULE_TAG " create /proc/seq_num failed!\n");
        return -ENOMEM;
    }

    if (!proc_create("seq_thrds", 0644, NULL, &thrds_file_ops)) {
        pr_err(MODULE_TAG "create /proc/seq_thrds failed!\n");
        remove_proc_entry("seq_num", NULL);
        return -ENOMEM;
    }

    pr_info(MODULE_TAG " init successfully!\n");
    return 0;
}

static void __exit cw_seqfile_test_exit(void)
{
    remove_proc_entry("seq_num", NULL);
    remove_proc_entry("seq_thrds", NULL);
    pr_info(MODULE_TAG " bye!\n");
}

module_init(cw_seqfile_test_init);
module_exit(cw_seqfile_test_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("calmwu <wubo0067@hotmail.com>");
MODULE_DESCRIPTION("ioctl test");
MODULE_VERSION("0.1");