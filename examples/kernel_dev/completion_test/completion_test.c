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
#include <linux/errno.h>
#include <linux/kthread.h>
#include <linux/kdev_t.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/completion.h>
#include <linux/slab.h>    //kmalloc()
#include <linux/uaccess.h> //copy_to/from_user()

// #ifndef __KERNEL__
// #define __KERNEL__
// #endif

#include "../kutils/misc.h"
#include "../kutils/dev.h"

#define MODULE_TAG "Module:[cw_completion_test]"
#define CW_COMPLETION_TEST_DEV_MAJOR 0
#define CW_COMPLETION_TEST_NR_DEVS 1

// 静态初始化完成对象
static DECLARE_COMPLETION(__data_read_done);
//
static struct task_struct *__read_wait_tasks[2];
//
static int32_t __completion_flag = 0;
static int32_t __read_count = 0;

// 设备创建上下文
static struct cw_cdev_crt_ctx __dev_crt_ctx;

static int32_t __read_done_thread(void *unused)
{
    int32_t ret = 0;

    allow_signal(SIGQUIT);
    allow_signal(SIGINT);

    while (1) {
        SHOW_CPU_CTX();

        pr_info(MODULE_TAG " wait for data read completion event...\n");
        // task 会被设置为 D 状态，也就是 TASK_UNINTERRUPTIBLE
        // 如果内核设置了/proc/sys/kernel/hung_task_timeout_secs，超时后会在 dmesg 中打印警告信息
        // 内核线程状态为 Sleep，无法使用 kill -9 释放
        ret = wait_for_completion_interruptible(&__data_read_done);
        // ret = wait_for_completion_interruptible_timeout(&__data_read_done,
        //                                                 usecs_to_jiffies(500));
        pr_info(MODULE_TAG " wait for completion ret:%d\n", ret);

        if (ret == -ERESTARTSYS) {
            pr_info(MODULE_TAG " received interrupted event\n");
            break;
        }
        if (__completion_flag == 2) {
            pr_info(MODULE_TAG " received exit event\n");
            break;
        }
        pr_info(MODULE_TAG " received read [%d] event\n", ++__read_count);
        __completion_flag = 0;
        // **收到 complete_all 通知后进行将 done 清零
        // reinit_completion(&__data_read_done);
    }
    // do_exit() calls schedule() to switch to a new process
    /*
        设置进程状态为 TASK_DEAD。
        向父进程发送 SIGCHLD 信号，通知其子进程已退出。
        关闭所有打开的文件描述符、释放内存、资源等。
        如果是内核线程，进行内核线程特定的清理。
        唤醒可能等待该进程退出的其他进程。
        调用 schedule() 让出 CPU，进行调度切换。
        释放 task_struct 结构，清理进程的最后资源。
        完成进程的退出，不返回。
        不需要外部再调用 kthread_stop
    */
    do_exit(0);
    return 0;
}

static int32_t __comp_dev_open(struct inode *inode, struct file *filp)
{
    pr_info(" filename: '%s' wrt open file: f_flags = 0x%x\n",
            filp->f_path.dentry->d_iname, filp->f_flags);
    return 0;
}

static int32_t __comp_dev_release(struct inode *inode, struct file *filp)
{
    pr_info("filename: '%s' released\n", filp->f_path.dentry->d_iname);
    return 0;
}

static ssize_t __comp_dev_read(struct file *filp, char __user *buf,
                               size_t count, loff_t *f_pos)
{
    PRINT_CTX();
    pr_info("comm:'%s' read filename:'%s' %zu bytes\n", current->comm,
            filp->f_path.dentry->d_iname, count);
    __completion_flag = 1;
    if (!completion_done(&__data_read_done)) {
        // 唤醒一个
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

    __read_wait_tasks[0] =
            kthread_run(__read_done_thread, NULL, "read_done_thread/0");
    // if (IS_ERR(__read_wait_task)) {
    //     ret = PTR_ERR(__read_wait_task);
    //     pr_err(MODULE_TAG " kthread_run failed. ret:%d\n", ret);
    //     goto unreg_chrdev;
    // }
    // 唤醒线程
    wake_up_process(__read_wait_tasks[0]);

    __read_wait_tasks[1] =
            kthread_run(__read_done_thread, NULL, "read_done_thread/1");
    wake_up_process(__read_wait_tasks[1]);

    pr_info(MODULE_TAG " init successfully!\n");
    return 0;

    // unreg_chrdev:
    //     module_destroy_cdevs(&__dev_crt_ctx);
    //     return -1;
}

static void __exit cw_completion_test_exit(void)
{
    __completion_flag = 2;
    // 判断是否有 completion 的等待者，0 标识有，非 0 标识没有
    if (!completion_done(&__data_read_done)) {
        pr_info(MODULE_TAG " complete data read\n");
        // This will wake up a single thread waiting on this completion
        // done 会设置为 UNIT_MAX, 而且 wait 不会减少这个值，如果需要再次使用，需要重新初始化
        complete_all(&__data_read_done);
    }

    module_destroy_cdevs(&__dev_crt_ctx);
    pr_info(MODULE_TAG " bye!\n");
}

module_init(cw_completion_test_init);
module_exit(cw_completion_test_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("calmwu <wubo0067@hotmail.com>");
MODULE_DESCRIPTION("completion test");
MODULE_VERSION("0.1");

// cw_completion_test: exports duplicate symbol module_create_cdevs (owned by cw_dev_ioctl_test), symbol 已经存在，冲突了
// echo 'module cw_completion_test +p' > /sys/kernel/debug/dynamic_debug/control
