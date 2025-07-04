/*
 * @Author: CALM.WU
 * @Date: 2025-07-04 10:38:17
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2025-07-04 11:36:38
 */

#define pr_fmt(fmt) "%s:%s(): " fmt, KBUILD_MODNAME, __func__

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/utsname.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sched/task.h>
#include <linux/list.h>
#include <linux/hashtable.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/stacktrace.h>
#include <linux/hrtimer.h>
#include <linux/timer.h>
#include <asm/irq_regs.h>
#include <linux/log2.h>
#include <linux/kprobes.h>
#include <linux/sizes.h>

#include "../kutils/misc.h"

#define MODULE_TAG "Module:[cw_kprobe_test]"

struct timing_entry {
    pid_t tid;     // thread ID
    ktime_t start; // start time in nanoseconds
    struct hlist_node node;
};

#define TIMING_HASH_BITS 8 /* 哈希表大小 2^8 = 256 */
static DEFINE_HASHTABLE(timing_table, TIMING_HASH_BITS);
static spinlock_t lock;
static struct kprobe kpb;

static int __init __cw_kprobe_test_init(void)
{
    pr_info(MODULE_TAG, " init successfully");
    return 0;
}

static void __exit __cw_kprobe_test_exit(void)
{
    pr_info(MODULE_TAG, " exited.\n");
}

module_init(__cw_kprobe_test_init);
module_exit(__cw_kprobe_test_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("calmwu <wubo0067@hotmail.com>");
MODULE_DESCRIPTION("cw_kprobe_test - A test module for kprobes");
MODULE_VERSION("0.0.1");