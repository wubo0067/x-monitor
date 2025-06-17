/*
 * @Author: CALM.WU
 * @Date: 2024-04-28 15:27:04
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2024-04-28 17:04:14
 */

#define pr_fmt(fmt) "%s:%s(): " fmt, KBUILD_MODNAME, __func__

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/jiffies.h>

static int32_t __init __cw_delay_test_init(void)
{
    uint64_t t1, t2, count = 0;
    struct timespec64 ts64;

    // 忙等待，10 毫秒
    pr_info("use time_before implement busy waiting\n");
    t1 = ktime_get_real_ns();
    unsigned long timeout = jiffies + 10;
    while (time_before(jiffies, timeout)) {
        need_resched();
        pr_info("busy waiting, the %d awakeing\n", ++count);
    }
    t2 = ktime_get_real_ns();
    pr_info("-> actual: %11llu ns = %7llu us = %4llu ms\n", (t2 - t1),
            (t2 - t1) / 1000, (t2 - t1) / 1000000);

    // 睡眠等待
    pr_info("use schedule_timeout implement sleep waiting\n");
    set_current_state(TASK_INTERRUPTIBLE);
    t1 = ktime_get_real_ns();
    schedule_timeout(msecs_to_jiffies(3000));
    t2 = ktime_get_real_ns();
    pr_info("-> actual: %11llu ns = %7llu us = %4llu ms\n", (t2 - t1),
            (t2 - t1) / 1000, (t2 - t1) / 1000000);

    // 短时延迟，忙等，很精确
    pr_info("use udelay implement short delay\n");
    t1 = ktime_get_real_ns();
    udelay(1000);
    t2 = ktime_get_real_ns();
    pr_info("-> actual: %11llu ns = %7llu us = %4llu ms\n", (t2 - t1),
            (t2 - t1) / 1000, (t2 - t1) / 1000000);

    // 计算开机到现在的时间
    pr_info("The time since the system was started.\n");
    jiffies_to_timespec64(jiffies - INITIAL_JIFFIES, &ts64);
    pr_info("sec:%lld+ns:%ld\n", ts64.tv_sec, ts64.tv_nsec);

    pr_info("Hello, delay test module\n");
    return 0;
}

static void __exit __cw_delay_test_exit(void)
{
    pr_info("Goodbye, delay test module\n");
}

module_init(__cw_delay_test_init);
module_exit(__cw_delay_test_exit);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("calmwu <wubo0067@hotmail.com>");
MODULE_DESCRIPTION("delay function Example");
MODULE_VERSION("0.1");