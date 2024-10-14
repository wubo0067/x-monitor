/*
 * @Author: CALM.WU
 * @Date: 2024-10-14 10:15:20
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2024-10-14 18:24:20
 */

#define pr_fmt(fmt) "%s:%s(): " fmt, KBUILD_MODNAME, __func__

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/err.h>

#include <linux/kthread.h>
#include <linux/kdev_t.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/completion.h>
#include <linux/slab.h>    //kmalloc()
#include <linux/uaccess.h> //copy_to/from_user()

#include "../kutils/misc.h"
#include "../kutils/dev.h"

#define MODULE_TAG "Module:[cw_completion_test]"
#define CW_COMPLETION_TEST_DEV_MAJOR 0
#define CW_COMPLETION_TEST_NR_DEVS 1

// 静态初始化完成对象
static DECLARE_COMPLETION(__data_read_done);
//
static struct task_struct *__read_wait_task = NULL;
//
static int32_t __completion_flag = 0;
static int32_t __read_count = 0;

// 设备创建上下文
static struct cw_cdev_crt_ctx __dev_crt_ctx;

static int32_t __read_done_thread(void *unused)
{
    while (1) {
        pr_info(MODULE_TAG " wait for data read completion event...\n");
        wait_for_completion(&__data_read_done);
        if (__completion_flag == 2) {
            pr_info(MODULE_TAG " event from exit function\n");
            break;
        }
        pr_info(MODULE_TAG "[%d] data read completion event received\n",
                ++__read_count);
        __completion_flag = 0;
    }
    // do_exit() calls schedule() to switch to a new process
    do_exit(0);
    return 0;
}

static int32_t __comp_dev_open(struct inode *inode, struct file *filp)
{
    dev_info(__dev_crt_ctx.devs[0].device,
             " filename: \"%s\"\n"
             " wrt open file: f_flags = 0x%x\n",
             filp->f_path.dentry->d_iname, filp->f_flags);
    return 0;
}

static int32_t __comp_dev_release(struct inode *inode, struct file *filp)
{
    dev_info(__dev_crt_ctx.devs[0].device, "filename: \"%s\" released\n",
             filp->f_path.dentry->d_iname);
    return 0;
}

static ssize_t __comp_dev_read(struct file *filp, char __user *buf,
                               size_t count, loff_t *f_pos)
{
    PRINT_CTX();
    dev_info(__dev_crt_ctx.devs[0].device,
             "%s wants to read filename: \"%s\" %zu bytes\n", current->comm,
             filp->f_path.dentry->d_iname, count);
    __completion_flag = 1;
    if (!completion_done(&__data_read_done)) {
        complete(&__data_read_done);
    }
    return 0;
}

static ssize_t __comp_dev_write(struct file *filp, const char __user *buf,
                                size_t count, loff_t *f_pos)
{
    PRINT_CTX();
    dev_info(__dev_crt_ctx.devs[0].device,
             "%s wants to write filename: \"%s\" %zu bytes\n", current->comm,
             filp->f_path.dentry->d_iname, count);
    return 0;
}

static struct file_operations __completion_test_dev_fops = {
    .owner = THIS_MODULE,
    .open = __comp_dev_open,
    .read = __comp_dev_read,
    .write = __comp_dev_write,
    .release = __comp_dev_release,
};

static int32_t __init cw_completion_test_init(void)
{
    int32_t ret = 0;

    __dev_crt_ctx.major = CW_COMPLETION_TEST_DEV_MAJOR;
    __dev_crt_ctx.base_minor = 0;
    __dev_crt_ctx.count = CW_COMPLETION_TEST_NR_DEVS;
    __dev_crt_ctx.name = "cw_completion_test_dev";
    __dev_crt_ctx.name_fmt = "cw_completion_test_dev%d";
    __dev_crt_ctx.dev_priv_data = NULL;
    __dev_crt_ctx.fops = &__completion_test_dev_fops;

    ret = module_create_cdevs(&__dev_crt_ctx);
    if (unlikely(0 != ret)) {
        pr_err(MODULE_TAG " module_create_cdevs failed\n");
        return ret;
    }

    __read_wait_task =
            kthread_run(__read_done_thread, NULL, "read_done_thread");
    if (IS_ERR(__read_wait_task)) {
        ret = PTR_ERR(__read_wait_task);
        pr_err(MODULE_TAG " kthread_run failed. ret:%d\n", ret);
        goto unreg_chrdev;
    }
    // 唤醒线程
    wake_up_process(__read_wait_task);

    pr_info(MODULE_TAG " init successfully!\n");
    return 0;

unreg_chrdev:
    module_destroy_cdevs(&__dev_crt_ctx);
    return -1;
}

static void __exit cw_completion_test_exit(void)
{
    // __completion_flag = 2;
    // // 判断是否有 completion 的等待者，0 标识有，非 0 标识没有
    // if (!completion_done(&__data_read_done)) {
    //     pr_info(MODULE_TAG " complete data read\n");
    //     // This will wake up a single thread waiting on this completion
    //     complete(&__data_read_done);
    // }

    // module_destroy_cdevs(&__dev_crt_ctx);
    // pr_info(MODULE_TAG " bye!\n");
}

module_init(cw_completion_test_init);
module_exit(cw_completion_test_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("calmwu <wubo0067@hotmail.com>");
MODULE_DESCRIPTION("completion test");
MODULE_VERSION("0.1");
