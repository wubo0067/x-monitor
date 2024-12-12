/*
 * @Author: CALM.WU
 * @Date: 2022-09-25 11:43:24
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2024-12-12 10:46:38
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/kthread.h>
#include <linux/delay.h>

#define AUTHOR "calm.wu <wubo0067@hotmail.com>"
#define DESC "RCU test"

/*
1：rcu 的 read thread 会读取新、老两个版本的数据。所以模拟多 read
2：rcu 的 write 发布新数据后，read 会读取到新数据。
3：我觉得只有 rcu_read_unlock 返回后，synchronize_rcu\call_rcu 才会被调用，返回返回。
4：对于 write，这个必须加锁
5：保证了并发读的时候无锁的损耗

https://mp.weixin.qq.com/s/PU27yZvWHWVTNDcY6bGVrw
*/

/*
Sep 29 16:15:33 localhost kernel: thread: myrcu_rd_thd-12965 __myrcu_header_thread: read a = 755
Sep 29 16:15:33 localhost kernel: thread: myrcu_rd_thd-12967 __myrcu_header_thread: read a = 755
Sep 29 16:15:34 localhost kernel: thread: myrcu_rd_thd-12966 __myrcu_header_thread: read a = 755
Sep 29 16:15:34 localhost kernel: thread: myrcu_rd_thd-12965 __myrcu_header_thread: read a = 755
Sep 29 16:15:36 localhost kernel: thread: myrcu_wr_thd-12968 __myrcu_writer_thread: write to new 756
Sep 29 16:15:36 localhost kernel: thread: myrcu_rd_thd-12965 __myrcu_header_thread: read a = 756
Sep 29 16:15:36 localhost kernel: __myrcu_del: a=755
*/

struct foo {
    s32 a;
    struct rcu_head rcu;
};

static struct foo *g_ptr = NULL;
static struct task_struct *read_threads[3] = { NULL, NULL, NULL };
static struct task_struct *write_thread = NULL;

DEFINE_SPINLOCK(foo_mutex);

// read thread
static s32 __myrcu_header_thread(void *data)
{
    struct foo *p = NULL;
    // s32         read_delay = *(s32 *)data;
    s32 delay = (unsigned long)data;

    printk("thread: %s-%d read_delay: %d\n", current->comm, current->pid,
           delay);

    while (!kthread_should_stop()) {
        ssleep(delay);
        rcu_read_lock();
        // 从全局变量中读取
        p = rcu_dereference(g_ptr);
        if (p) {
            printk("thread: %s-%d %s: read a = %d\n", current->comm,
                   current->pid, __func__, p->a);
        }
        // mdelay(delay * 100);
        // 读结束，但这不会立即触发释放
        rcu_read_unlock();
    }
    return 0;
}

static void __myrcu_del(struct rcu_head *rh)
{
    struct foo *p = container_of(rh, struct foo, rcu);
    // 这一行，一定在 rcu_read_unlock 之后输出
    printk("%s: a=%d\n", __func__, p->a);
    kfree(p);
}

static s32 __myrcu_writer_thread(void *p)
{
    struct foo *old_fp;
    struct foo *new_fp;

    s32 value = (unsigned long)p;

    printk("thread: %s-%d value: %d\n", current->comm, current->pid, value);

    while (!kthread_should_stop()) {
        // write 每秒更新一次数据
        ssleep(2);
        // 更新都需要在一个拷贝上进行
        new_fp = (struct foo *)kmalloc(sizeof(struct foo), GFP_KERNEL);
        spin_lock(&foo_mutex);

        printk("thread: %s-%d %s: write to new %d\n", current->comm,
               current->pid, __func__, value);
        // 保留 old_fp，这个指针的释放需要由 synchronize_rcu 和 call_rcu 来决定
        old_fp = g_ptr;
        *new_fp = *old_fp;
        new_fp->a = value;

        // 全局数据发布更新，发布之后 read 读取的都是最新的数据，但是旧的数据还是会保留一段时间，直到 synchronize_rcu 结束
        // 也就是说，rcu_read_lock/unlock 读取老数据的 thread 都退出了，才会释放老数据
        rcu_assign_pointer(g_ptr, new_fp);

        spin_unlock(&foo_mutex);

        if (old_fp) {
            // 保证读的数据在读完之前不能被释放，这个仅仅是注册而已，不是同步方法
            call_rcu(&old_fp->rcu, __myrcu_del);
            printk("thread: %s-%d after call_rcu\n", current->comm,
                   current->pid);
        }
        value++;
    }
    return 0;
}

static s32 __init myrcu_test_init(void)
{
    s32 value_1 = 700;
    s32 read_delay_secs[3] = { 1, 2, 3 };

    printk("calm-go: myrcu_test module init\n");
    g_ptr = kzalloc(sizeof(struct foo), GFP_KERNEL);

    // 启动内核线程
    for (s32 i = 0; i < ARRAY_SIZE(read_threads); i++) {
        read_threads[i] = kthread_run(__myrcu_header_thread,
                                      (void *)(unsigned long)read_delay_secs[i],
                                      "myrcu_rd_thd");
    }

    write_thread = kthread_run(__myrcu_writer_thread,
                               (void *)(unsigned long)value_1, "myrcu_wr_thd");

    return 0;
}

static void __exit myrcu_test_exit(void)
{
    printk("calm-go: myrcu_test module exit\n");
    kthread_stop(write_thread);

    for (s32 i = 0; i < ARRAY_SIZE(read_threads); i++) {
        kthread_stop(read_threads[i]);
    }

    if (g_ptr) {
        kfree(g_ptr);
    }
}

module_init(myrcu_test_init);
module_exit(myrcu_test_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION(DESC);
