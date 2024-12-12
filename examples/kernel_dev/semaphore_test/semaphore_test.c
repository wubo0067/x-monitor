/*
 * @Author: CALM.WU
 * @Date: 2024-12-12 10:46:27
 * @Last Modified by:   CALM.WU
 * @Last Modified time: 2024-12-12 10:46:27
 */

#define pr_fmt(fmt) "%s:%s(): " fmt, KBUILD_MODNAME, __func__

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kthread.h>
#include <linux/rwsem.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/string.h>

#include "../kutils/misc.h"

#define MODULE_TAG "Module:[cw_rw_semaphore_test]"
#define PROC_FILE "rw_semaphore_test"

static struct proc_dir_entry *proc_cw __read_mostly;
static DECLARE_RWSEM(__cw_rwsem);

enum rwsem_waiter_type { RWSEM_WAITING_FOR_WRITE, RWSEM_WAITING_FOR_READ };

struct rwsem_waiter {
    struct list_head list;
    struct task_struct *task;
    enum rwsem_waiter_type type;
    unsigned long timeout;
};

static int32_t __rw_semaphore_show(struct seq_file *m, void *data)
{
    struct rwsem_waiter *waiter;
    struct task_struct *owner;

    owner = (struct task_struct *)atomic_long_read(&__cw_rwsem.owner);
    seq_printf(m,
               "rw_semaphore:%p, count:0x%016lx, owner:%p, owner.comm:'%s'\n",
               &__cw_rwsem, atomic_long_read(&__cw_rwsem.count), owner,
               0 != owner ? owner->comm : "NULL");

    if (!list_empty(&__cw_rwsem.wait_list)) {
        waiter = list_first_entry(&__cw_rwsem.wait_list, struct rwsem_waiter,
                                  list);
        seq_printf(m, "\twait list: comm:'%s', type:'%s'\n", waiter->task->comm,
                   waiter->type == 0 ? "RWSEM_WAITING_FOR_WRITE" :
                                       "RWSEM_WAITING_FOR_READ");

        // remaining waiters
        list_for_each_entry (waiter, __cw_rwsem.wait_list.next, list) {
            seq_printf(m, "\twait list: comm:'%s', type:'%s'\n",
                       waiter->task->comm,
                       waiter->type == 0 ? "RWSEM_WAITING_FOR_WRITE" :
                                           "RWSEM_WAITING_FOR_READ");
        }
    }
    return 0;
}

static int32_t __rw_semaphore_test_open(struct inode *inode, struct file *filp)
{
    // 要提供 show 方法
    return single_open(filp, __rw_semaphore_show, NULL);
}

static int32_t __cw_sem_downread(void *data)
{
    PRINT_CTX();
    down_read(&__cw_rwsem);
    return 0;
}

static int32_t __cw_sem_upread(void *data)
{
    PRINT_CTX();
    up_read(&__cw_rwsem);
    return 0;
}

static int32_t __cw_sem_downwrite(void *data)
{
    PRINT_CTX();
    down_write(&__cw_rwsem);
    return 0;
}

static int32_t __cw_sem_upwrite(void *data)
{
    PRINT_CTX();
    up_write(&__cw_rwsem);
    return 0;
}

static ssize_t __rw_semaphore_test_write(struct file *filp,
                                         const char __user *buf, size_t count,
                                         loff_t *offs)
{
    struct task_struct *ts = NULL;
    char cmd_buf[32] = { 0 };

    if (copy_from_user(cmd_buf, buf, count)) {
        return -EFAULT;
    }

    print_hex_dump_bytes(MODULE_TAG "User buffer: ", DUMP_PREFIX_OFFSET,
                         cmd_buf, count);

    // 使用 echo 输入的带有换行符，所以这里需要去掉换行符
    cmd_buf[count - 1] = 0;

    if (0 == strncmp(cmd_buf, "down_read", 9)) {
        ts = kthread_run(__cw_sem_downread, NULL, "cw_sem_downread");
        if (IS_ERR(ts)) {
            return -EFAULT;
        }
    } else if (0 == strncmp(cmd_buf, "up_read", 7)) {
        ts = kthread_run(__cw_sem_upread, NULL, "cw_sem_upread");
        if (IS_ERR(ts)) {
            return -EFAULT;
        }
    } else if (0 == strncmp(cmd_buf, "down_write", 10)) {
        ts = kthread_run(__cw_sem_downwrite, NULL, "cw_sem_downwrite");
        if (IS_ERR(ts)) {
            return -EFAULT;
        }
    } else if (0 == strncmp(cmd_buf, "up_write", 8)) {
        ts = kthread_run(__cw_sem_upwrite, NULL, "cw_sem_upwrite");
        if (IS_ERR(ts)) {
            return -EFAULT;
        }
    }

    return count;
}

static const struct file_operations __rw_semaphore_test_fops = {
    .owner = THIS_MODULE,
    .open = __rw_semaphore_test_open,
    .read = seq_read,
    .write = __rw_semaphore_test_write,
    .llseek = seq_lseek,
    .release = single_release,
};

static int32_t __init __cw_rw_semaphore_test_init(void)
{
    proc_cw = proc_mkdir("cw_test", NULL);
    if (!proc_cw) {
        pr_err(MODULE_TAG " proc_mkdir failed\n");
        return -1;
    }

    if (!proc_create(PROC_FILE, 0644, proc_cw, &__rw_semaphore_test_fops)) {
        goto err;
    }

    pr_info(MODULE_TAG " init successfully!\n");
    return 0;
err:
    remove_proc_subtree("cw_test", NULL);
    return -1;
}

static void __exit __cw_rw_semaphore_test_exit(void)
{
    remove_proc_subtree("cw_test", NULL);
    pr_info(MODULE_TAG " bye!\n");
}

module_init(__cw_rw_semaphore_test_init);
module_exit(__cw_rw_semaphore_test_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("calmwu <wubo0067@hotmail.com>");
MODULE_DESCRIPTION("rw_semaphore test");
MODULE_VERSION("0.1");