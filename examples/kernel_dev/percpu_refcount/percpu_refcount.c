/*
 * @Author: CALM.WU
 * @Date: 2025-02-07 14:18:14
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2025-02-07 14:22:21
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
#include <linux/utsname.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sched/task.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/percpu-refcount.h>
#include <linux/delay.h>

#include "../kutils/misc.h"

#define MODULE_TAG "Module:[cw_percpu_refcount]"

struct cw_test_dev {
    struct percpu_ref ref;
    atomic_t active_tasks;
    bool is_shutdown;
    struct completion done;
};

static struct cw_test_dev *__test_cw_dev;

// 而是在 perfcpu_ref_kill 之后，当最后一个 percpu_ref_put 使计数变为 0 时触发
static void __cw_dev_release(struct percpu_ref *ref)
{
    struct cw_test_dev *cw_test_dev =
            container_of(ref, struct cw_test_dev, ref);
    pr_info(MODULE_TAG "release callback triggered\n");
    // 释放完成事件
    complete(&cw_test_dev->done);
}

static int32_t __dev_worker_thread(void *data)
{
    struct cw_test_dev *dev = data;

    if (dev->is_shutdown) {
        atomic_dec(&dev->active_tasks);
        return 0;
    }

    // 获取引用
    percpu_ref_get(&dev->ref);
    pr_info(MODULE_TAG "worker: got reference, doing work\n");
    msleep(5000); // 模拟工作负载

    pr_info(MODULE_TAG "worker: work done, putting reference\n");
    percpu_ref_put(&dev->ref);
    atomic_dec(&dev->active_tasks);

    return 0;
}

static int32_t __create_worker_threads(struct cw_test_dev *dev)
{
    int32_t i;
    struct task_struct *task;

    for (i = 0; i < 3; i++) {
        atomic_inc(&dev->active_tasks);
        task = kthread_run(__dev_worker_thread, dev, "worker-%d", i);
        if (IS_ERR(task)) {
            pr_err("Failed to create worker thread %d\n", i);
            atomic_dec(&dev->active_tasks);
            return PTR_ERR(task);
        }
    }
    return 0;
}

static void __cw_dev_cleanup(void)
{
    if (!__test_cw_dev) {
        return;
    }

    __test_cw_dev->is_shutdown = true;

    pr_info(MODULE_TAG "killing percpu_ref.\n");
    /*
    将 percpu_ref 切换到原子模式 (atomic mode)
    阻止后续的 percpu_ref_get 操作
    对已存在的引用计数进行最终的清零处理

    调用时机：
        在对象即将被销毁时调用
        通常在模块卸载或设备移除流程中使用

    同步考虑
        kill 操作本身是异步的
        需要配合 completion 或其他同步机制等待最终释放
        在确认所有引用释放完成前，不能释放对象内存
    */
    percpu_ref_kill(&__test_cw_dev->ref);

    pr_info(MODULE_TAG "waiting for completion.\n");

    wait_for_completion(&__test_cw_dev->done);
    // 释放 percpu_ref
    percpu_ref_exit(&__test_cw_dev->ref);
    kfree(__test_cw_dev);
    __test_cw_dev = NULL;
}

static int32_t __init __cw_percpu_refcount_init(void)
{
    int32_t ret = 0;
    struct cw_test_dev *dev;

    dev = kzalloc(sizeof(struct cw_test_dev), GFP_KERNEL);
    if (!dev) {
        pr_err(MODULE_TAG "Failed to allocate memory for dev\n");
        return -ENOMEM;
    }

    atomic_set(&dev->active_tasks, 0);
    dev->is_shutdown = false;
    // 初始化完成事件
    init_completion(&dev->done);

    // 初始化 percpu_ref
    ret = percpu_ref_init(&dev->ref, __cw_dev_release, 0, GFP_KERNEL);
    if (ret) {
        pr_err(MODULE_TAG "Failed to initialize percpu_ref\n");
        kfree(dev);
        return ret;
    }

    // 创建工作线程
    ret = __create_worker_threads(dev);
    if (ret) {
        percpu_ref_exit(&dev->ref);
        kfree(dev);
        return ret;
    }
    __test_cw_dev = dev;

    pr_info(MODULE_TAG "init successfully!\n");
    return 0;
}

static void __exit __cw_percpu_refcount_exit(void)
{
    __cw_dev_cleanup();
    pr_info(MODULE_TAG "bye!\n");
}

module_init(__cw_percpu_refcount_init);
module_exit(__cw_percpu_refcount_exit);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("calmwu <wubo0067@hotmail.com>");
MODULE_DESCRIPTION("cw_percpu_refcount");
MODULE_VERSION("0.1");

/*
https://blog.csdn.net/kaka__55/article/details/120467872
*/