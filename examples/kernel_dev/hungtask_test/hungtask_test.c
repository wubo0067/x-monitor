/*
 * @Author: CALM.WU
 * @Date: 2024-10-18 17:18:42
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2024-10-18 17:58:09
 */

#define pr_fmt(fmt) "%s:%s(): " fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/signal.h>

#include "../kutils/misc.h"

#define MODULE_TAG "Module:[cw_hungtask_test]"

#define NUM_THREADS 5 // 定义线程数量

static struct task_struct *__hung_tasks[NUM_THREADS];

struct thread_data {
    int32_t thread_num;
    atomic_t running; // 线程是否运行，如果为 0 表示线程已经退出，退出后不需要调用 kthread_stop，否则会 crash
};
static struct thread_data __thread_datas[NUM_THREADS];

/* 线程函数 */
static int32_t hung_task(void *data)
{
    struct thread_data *d = (struct thread_data *)data; // 获取线程编号

    PRINT_CTX();

    pr_info(MODULE_TAG " hung task %d started\n", d->thread_num);

    /* 将当前任务设置为不可中断状态 (TASK_UNINTERRUPTIBLE) */
    set_current_state(TASK_UNINTERRUPTIBLE);

    /* 模拟任务被挂起，延迟 150 秒 */
    schedule_timeout(HZ * 150);

    pr_info(MODULE_TAG " hung task %d exiting\n", d->thread_num);
    atomic_set(&d->running, 0);

    return 0;
}

static int32_t __init cw_hungtask_test_init(void)
{
    int i;

    pr_info(MODULE_TAG " Loading...\n");

    /* 创建多个内核线程 */
    for (i = 0; i < NUM_THREADS; i++) {
        __thread_datas[i].thread_num = i; // 传递线程编号给线程函数
        atomic_set(&__thread_datas[i].running, 1);

        __hung_tasks[i] = kthread_run(hung_task, &__thread_datas[i],
                                      "hung_task_thread%d", i);

        if (IS_ERR(__hung_tasks[i])) {
            pr_err(MODULE_TAG " Failed to create kernel thread %d\n", i);
            return PTR_ERR(__hung_tasks[i]);
        }

        pr_info(MODULE_TAG " hung task thread %d created successfully\n", i);
    }
    return 0;
}

static void __exit cw_hungtask_test_exit(void)
{
    int32_t i, ret;

    /* 停止所有线程 */
    for (i = 0; i < NUM_THREADS; i++) {
        if (__hung_tasks[i]) { // && atomic_read(&__thread_datas[i].running)) {
            /* 获取 kthread_stop 返回值并检查 */
            /*
            kthread_stop() 的作用是停止内核线程。它通过唤醒目标线程来让它意识到需要退出。
            即使线程处于 TASK_UNINTERRUPTIBLE 状态，kthread_stop() 内部会通过设置线程的 kthread_should_stop() 标志，
            并调用 wake_up_process() 函数，强制唤醒该线程。
            */
            ret = kthread_stop(__hung_tasks[i]);
            if (ret == -ESRCH) {
                pr_err(MODULE_TAG
                       " Error stopping thread %d: Thread already exited\n",
                       i);
            } else {
                pr_info(MODULE_TAG
                        " hung task thread %d stopped with return value: %d\n",
                        i, ret);
            }
        }
    }
    pr_info(MODULE_TAG " Unloading \n");
}

module_init(cw_hungtask_test_init);
module_exit(cw_hungtask_test_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("calmwu <wubo0067@hotmail.com>");
MODULE_DESCRIPTION("Kernel Thread to Simulate hung Task");
MODULE_VERSION("0.1");
