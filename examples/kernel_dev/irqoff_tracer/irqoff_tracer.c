/*
 * @Author: CALM.WU
 * @Date: 2025-05-26 14:36:58
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2025-05-26 18:06:50
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
#include <linux/stacktrace.h>
#include <linux/hrtimer.h>
#include <linux/timer.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
#include <linux/sched.h>
#else
#include <linux/sched/clock.h>
#endif

#include "../kutils/misc.h"

#define MODULE_TAG "Module:[cw_irqoff_tracer]"

#define MAX_TRACE_ENTRIES (SZ_1K / sizeof(uint64_t))
#define PER_TRACE_ENTRIES_AVERAGE (8 + 8)

#define MAX_STACK_TRACE_ENTRIES (MAX_TRACE_ENTRIES / PER_TRACE_ENTRIES_AVERAGE)

#define MAX_LATENCY_RECORDS 10

struct irqoff_trace {
    uint32_t nr_entries;
    uint64_t *entries;
}

struct stack_dump_metadata {
    uint64_t last_timestamp; // 上次采样的时间戳
    uint64_t nr_irqoff_trace;
    struct irqoff_trace trace[MAX_STACK_TRACE_ENTRIES];
    uint64_t nr_entries;
    uint64_t latency_count[MAX_LATENCY_RECORDS];

    /*task comms*/
    char task_comms[MAX_STACK_TRACE_ENTRIES][TASK_COMM_LEN];
    /*task pids*/
    pid_t task_pids[MAX_STACK_TRACE_ENTRIES];

    struct {
        uint64_t nsecs : 63;
        uint64_t more : 1
    } latency[MAX_STACK_TRACE_ENTRIES];
};

struct per_cpu_stack_trace {
    struct timer_list timer;
    struct hrtimer *hrtime; // 高分辨率定时器
    struct stack_dump_metadata hardirq_trace;
    struct stack_dump_metadata softirq_trace;

    bool softirq_delayed; // 是否延迟软中断采样
};

/*
* @brief 追踪开关，默认关闭
*/
static bool __trace_enabled = false;
/*
* @brief 默认采样周期，10ms，单位是纳秒
*/
static uint64_t __sampling_period = 10 * 1000 * 1000UL;
/*
* @brief 默认追踪中断关闭的延迟，50ms，单位是纳秒
*/
static u64 __trace_irqoff_latency = 50 * 1000 * 1000UL;

/* 指向一个 per-CPU 内存块的指针，而 DEFINE_PER_CPU 在每个 CPU 上都会分配一个对象*/
static struct per_cpu_stack_trace __percpu *__cpu_stack_trace;

// ***************irqoff trace 开关********** */
// 高精度定时器回调函数
static enum hrtimer_restart
        __irqoff_tracer_hrtimer_callback(struct hrtimer *hrtimer)
{
    // 重启定时器，间隔为采样周期
    hrtimer_forward_now(hrtimer, ns_to_ktime(__sampling_period));
    return HRTIMER_RESTART;
}

static void __irqoff_tracer_timer_callback(struct timer_list *timer)
{
    // 获取当前时间戳，纳秒
    uint64_t now = local_clock();
    uint64_t delta;

    delta = now -
            __this_cpu_read(__cpu_stack_trace->hardirq_trace.last_timestamp);
    __this_cpu_write(__cpu_stack_trace->softirq_trace.last_timestamp, now);
    __this_cpu_write(__cpu_stack_trace->softirq_delayed, false);
}

static void __irqoff_tracer_smp_start_timer(void *info)
{
    uint64_t now = local_clock();
    struct per_cpu_stack_trace __percpu *trace =
            (struct per_cpu_stack_trace *)info;

    // 设置定时器启动时间
    trace->hardirq_trace.last_timestamp = now;
    trace->softirq_trace.last_timestamp = now;

    // 允许你指定一个到期时间范围，而不是一个精确的到期时间点。这在某些情况下非常有用，
    // 例如当对定时器的实际触发时间有一个可接受的微小波动范围时，可以允许内核在内部进行一些优化
    hrtimer_start_range_ns(&(trace->hrtime), ns_to_ktime(__sampling_period), 0,
                           HRTIMER_MODE_PINNED);
    trace->timer.expires =
            jiffies + msecs_to_jiffies(__sampling_period / 1000000UL);
    add_timer_on(&trace->timer, smp_processor_id());
}

static void __irqoff_tracer_start_timers(void)
{
    int32_t cpu;

    for_each_online_cpu (cpu) {
        struct hrtimer *hrtimer;
        struct timer_list *timer;
        // 初始化高精度计时器
        hrtimer = per_cpu_ptr(__cpu_stack_trace->hrtime, cpu);
        // 定时器绑定到 cpu 上，回调函数只会在该 cpu 上执行，而不会被迁移
        hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_PINNED);
        hrtimer->function = __irqoff_tracer_hrtimer_callback;

        // 初始化传统定时器
        timer = per_cpu_ptr(__cpu_stack_trace->timer, cpu);
        // TIMER_IRQSAFE 表示回调函数保证中断安全
        timer_setup(timer, __irqoff_tracer_timer_callback,
                    TIMER_PINNED | TIMER_IRQSAFE);

        // 在指定 cpu 上启动定时器，等待函数执行完毕
        smp_call_function_single(cpu, __irqoff_tracer_smp_start_timer,
                                 per_cpu_ptr(__cpu_stack_trace, cpu), true);
    }
}

static void __irqoff_tracer_stop_timers(void)
{
    int32_t cpu;

    for_each_online_cpu (cpu) {
        struct hrtimer *hrtimer;
        struct timer_list *timer;

        // 获取当前 CPU 的高精度计时器和传统定时器
        hrtimer = per_cpu_ptr(__cpu_stack_trace->hrtime, cpu);
        timer = per_cpu_ptr(__cpu_stack_trace->timer, cpu);

        // 停止高精度计时器
        hrtimer_cancel(hrtimer);
        // 停止传统定时器
        del_timer_sync(timer);
    }
}

static int32_t __irqoff_tracer_enabled_show(struct seq_file *seq, void *data)
{
    seq_printf(seq, "%s\n", __trace_enabled ? "enabled" : "disabled");
    return 0;
}

static int32_t __irqoff_tracer_enabled_open(struct inode *inode,
                                            struct file *file)
{
    return single_open(file, __irqoff_tracer_enabled_show, inode->i_private);
}

static int32_t __irqoff_tracer_enabled_write(struct file *file,
                                             const char __user *buf, size_t cnt,
                                             loff_t *ppos)
{
    char kbuf[8]; // 足够存储 "true", "false", "0", "1", "on", "off", "y", "n" 等
    int ret;
    bool new_enabled;

    if (cnt == 0) {
        return -EINVAL; // 没有输入
    }

    // 确保不会溢出 kbuf
    if (cnt >= sizeof(kbuf)) {
        pr_err(MODULE_TAG " input too long\n");
        return -EINVAL;
    }

    ret = copy_from_user(kbuf, buf, cnt);
    if (ret) {
        pr_err(MODULE_TAG " copy_from_user failed: %d\n", ret);
        return -EFAULT;
    }
    kbuf[cnt] = '\0'; // 确保字符串以空终止

    ret = kstrtobool(kbuf, &new_enabled);
    if (ret) {
        pr_err(MODULE_TAG
               " invalid input '%s'. kstrtobool failed: %d. Please use '0', '1', 'true', 'false', 'on', 'off', 'y', or 'n'.\n",
               kbuf, ret);
        return ret; // kstrtobool 返回负的 errno
    }

    // 如果设置相同，直接返回
    if (!!__trace_enabled == !!new_enabled) {
        pr_info(MODULE_TAG " tracing already %s.\n",
                (!!__trace_enabled) ? "enabled" : "disabled");
        return cnt; // 没有变化，直接返回
    }

    if (new_enabled) {
        pr_info(MODULE_TAG " enabling tracing.\n");
        __irqoff_tracer_start_timers();
    } else {
        pr_info(MODULE_TAG " disabling tracing.\n");
        __irqoff_tracer_stop_timers();
    }
    __trace_enabled = new_enabled;

    *ppos += cnt; // 更新文件位置指针
    return cnt;   // 返回消耗的字节数
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct file_operations __irqoff_tracer_enabled_fops = {
    .open = __irqoff_tracer_enabled_open,
    .read = seq_read,
    .write = __irqoff_tracer_enabled_write,
    .llseek = seq_lseek,
    .release = single_release,
};
#else
static const struct proc_fops __irqoff_tracer_enabled_fops = {
    .proc_open = __irqoff_tracer_enabled_open,
    .proc_read = seq_read,
    .proc_write = __irqoff_tracer_enabled_write,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};
#endif

// ***************sampling period********** */
static int32_t __sampling_period_show(struct seq_file *seq, void *data)
{
    seq_printf(seq, "%llums\n", __sampling_period / (1000 * 1000UL));
    return 0;
}

static int32_t __sampling_period_open(struct inode *inode, struct file *file)
{
    return single_open(file, __sampling_period_show, inode->i_private);
}

static int32_t __sampling_period_write(struct file *file,
                                       const char __user *ubuf, size_t cnt,
                                       loff_t *ppos)
{
    uint64_t new_period;
    int32_t ret;

    if (!__trace_enabled) {
        pr_err(MODULE_TAG
               " tracing is not enabled, cannot set sampling period\n");
        return -EPERM; // 操作不允许
    }

    ret = kstrtoul_from_user(ubuf, cnt, 10, &new_period);
    if (ret)
        return ret;

    __sampling_period = new_period * 1000 * 1000UL; // 转换为纳秒
    // 如果采样周期大于两倍的延迟周期，将延迟周期设置为采样周期的两倍
    if (__sampling_period > (__trace_irqoff_latency >> 1)) {
        __trace_irqoff_latency = __sampling_period << 1;
    }

    pr_info(MODULE_TAG " updated sampling period to %llums\n",
            __sampling_period / (1000 * 1000UL));

    return cnt;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct file_operations __sampling_period_fops = {
    .open = __sampling_period_open,
    .read = seq_read,
    .write = __sampling_period_write,
    .llseek = seq_lseek,
    .release = single_release,
};
#else
static const struct proc_fops __sampling_period_fops = {
    .proc_open = __sampling_period_open,
    .proc_read = seq_read,
    .proc_write = __sampling_period_write,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};
#endif

static int32_t __init __cw_irqoff_tracer_init(void)
{
    struct proc_dir_entry *parent_dir;

    __cpu_stack_trace = alloc_percpu(struct per_cpu_stack_trace);
    if (!__cpu_stack_trace) {
        pr_err(MODULE_TAG " failed to allocate per-CPU stack trace memory.\n");
        return -ENOMEM;
    }
    pr_info(MODULE_TAG " initialized.\n");
    // TODO  stack_trace_skip_hardirq_init

    // 创建 /proc/irqoff_tracer 目录
    parent_dir = proc_mkdir("irqoff_tracer", NULL);
    if (!parent_dir) {
        pr_err(MODULE_TAG " failed to create /proc/irqoff_tracer directory.\n");
        goto free_percpu;
    }

    // TODO:distribute

    // TODO:trace_latency

    if (!proc_create("enabled", 0644, parent_dir,
                     &__irqoff_tracer_enabled_fops)) {
        pr_err(MODULE_TAG " failed to create /proc/irqoff_tracer/enabled.\n");
        goto err_remove_proc;
    }
    pr_info(MODULE_TAG " successfully created /proc/irqoff_tracer/enabled.\n");

    if (!proc_create("sampling_period", 0644, parent_dir,
                     &__sampling_period_fops)) {
        pr_err(MODULE_TAG
               " failed to create /proc/irqoff_tracer/sampling_period.\n");
        goto err_remove_proc;
    }
    pr_info(MODULE_TAG
            " successfully created /proc/irqoff_tracer/sampling_period.\n");

    pr_info(MODULE_TAG " successfully created /proc/irqoff_tracer.\n");
    return 0;

err_remove_proc:
    remove_proc_subtree("irqoff_tracer", NULL);
err_free_percpu:
    free_percpu(__cpu_stack_trace);

    return -ENOMEM;
}

static void __exit __cw_irqoff_tracer_exit(void)
{
    if (__trace_enabled) {
        __irqoff_tracer_stop_timers();
        __trace_enabled = false;
    }
    remove_proc_subtree("irqoff_tracer", NULL);
    free_percpu(__cpu_stack_trace);
    pr_info(MODULE_TAG " exited.\n");
}

module_init(__cw_irqoff_tracer_init);
module_exit(__cw_irqoff_tracer_exit);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("calmwu <wubo0067@hotmail.com>");
MODULE_DESCRIPTION("cw_irqoff_tracer");
MODULE_VERSION("0.1");