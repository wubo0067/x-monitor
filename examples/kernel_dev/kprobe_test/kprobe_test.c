/*
 * @Author: CALM.WU
 * @Date: 2025-07-04 10:38:17
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2025-07-17 14:53:52
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
#define MAX_SYMBOL_LEN 64
static char __kp_symbol[MAX_SYMBOL_LEN] = "_do_fork";
module_param_string(symbol, __kp_symbol, sizeof(__kp_symbol), 0644);
MODULE_PARM_DESC(symbol, "Symbol name to attach a kprobe, default is _do_fork");

struct timing_entry {
    pid_t tid;     // thread ID
    ktime_t start; // start time in nanoseconds
    struct hlist_node node;
};

#define TIMING_HASH_BITS 8 /* 哈希表大小 2^8 = 256 */
static DEFINE_HASHTABLE(__timing_table, TIMING_HASH_BITS);
static spinlock_t __lock;
static struct kprobe __kpb = {
    .symbol_name = __kp_symbol, // The symbol to probe
};

static int32_t __kp_hander_pre(struct kprobe *p, struct pt_regs *regs)
{
    struct timing_entry *entry;
    pid_t tid = current->pid;

    PRINT_CTX();

    // Allocate memory for the timing entry
    // GFP_ATOMIC 标志的作用是在中断上下文或其他不可睡眠的上下文中分配内存。
    entry = kmalloc(sizeof(struct timing_entry), GFP_ATOMIC);
    if (!entry) {
        pr_err(MODULE_TAG "Failed to allocate memory for timing entry\n");
        return 0;
    }

    // Initialize the timing entry
    entry->tid = tid;
    entry->start = ktime_get();

    spin_lock(&__lock);
    // Insert the entry into the hash table
    hash_add(__timing_table, &entry->node, tid);
    spin_unlock(&__lock);

    //pr_info(MODULE_TAG, "Pre-handler: TID=%d, Start Time=%lld ns\n", tid, ktime_to_ns(entry->start));
    return 0;
}

static void __kp_handler_post(struct kprobe *p, struct pt_regs *regs,
                              unsigned long flags)
{
    struct timing_entry *entry;
    pid_t tid = current->pid;
    ktime_t end_time = ktime_get();
    int64_t elapsed_ns;
    bool found = false;

    PRINT_CTX();

    spin_lock(&__lock);
    // Search for the entry in the hash table
    hash_for_each_possible (__timing_table, entry, node, tid) {
        if (entry->tid == tid) {
            found = true;
            pr_info(MODULE_TAG "Post-handler: TID=%d, ", tid);
            SHOW_DELTA(end_time, entry->start);
            // Remove the entry from the hash table
            hash_del(&entry->node);
            kfree(entry);
            break;
        }
    }

    if (!found) {
        pr_warn(MODULE_TAG "Post-handler: TID=%d not found in timing table\n",
                tid);
    }

    spin_unlock(&__lock);
}

static int32_t __kp_handler_fault(struct kprobe *p, struct pt_regs *regs,
                                  int32_t trapnr)
{
    pr_err(MODULE_TAG "Fault handler: p->addr = 0x%p, Trap #%dn\n", p->addr,
           trapnr);
    return 0; // Return 0 to continue execution
}

static int __init __cw_kprobe_test_init(void)
{
    // Initialize the hash table
    hash_init(__timing_table);
    spin_lock_init(&__lock);

    // Set up the kprobe
    __kpb.pre_handler = __kp_hander_pre;
    __kpb.post_handler = __kp_handler_post;
    __kpb.fault_handler = __kp_handler_fault;

    if (register_kprobe(&__kpb)) {
        pr_err(MODULE_TAG "Failed to register kprobe for %s\n",
               __kpb.symbol_name);
        return -EINVAL;
    }
    /*
	 ⚡ root@localhost  ~  grep do_fork /proc/kallsyms
	ffffffffaa6f0b90 T _do_fork
	Module:[cw_kprobe_test]Registered kprobe for _do_fork successfully. Original address: 0xffffffffaa6f0b90
	*/
    pr_info(MODULE_TAG
            "Registered kprobe for %s successfully. Original address: 0x%lx\n",
            __kpb.symbol_name, (unsigned long)__kpb.addr);
    return 0;
}

static void __exit __cw_kprobe_test_exit(void)
{
    struct timing_entry *entry;
    unsigned int bkt;
    struct hlist_node *tmp;

    // Unregister the kprobe
    unregister_kprobe(&__kpb);

    // Clean up the hash table
    spin_lock(&__lock);
    hash_for_each_safe (__timing_table, bkt, tmp, entry, node) {
        pr_info(MODULE_TAG "Cleaning up entry: TID=%d\n", entry->tid);
        hash_del(&entry->node);
        kfree(entry);
    }
    spin_unlock(&__lock);

    pr_info(MODULE_TAG " exited.\n");
}

module_init(__cw_kprobe_test_init);
module_exit(__cw_kprobe_test_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("calmwu <wubo0067@hotmail.com>");
MODULE_DESCRIPTION("cw_kprobe_test - A test module for kprobes");
MODULE_VERSION("0.0.1");