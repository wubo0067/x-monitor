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
#include <asm/irq_regs.h>
#include <linux/log2.h>
#include <linux/kprobes.h>

#include "../kutils/misc.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
#include <linux/sched.h>
#else
#include <linux/sched/clock.h>
#endif

#define MODULE_TAG "Module:[cw_irqoff_tracer]"

// 1K 空间能保存大约 128 个 64 位的 trace entries
#define MAX_TRACE_ENTRIES (SZ_1K / sizeof(uint64_t))
// 堆栈的平均深度，这个应该是经验值，16
#define PER_TRACE_ENTRIES_AVERAGE (8 + 8)
// 保存 8 个堆栈追踪结果
#define MAX_STACK_TRACE_ENTRIES (MAX_TRACE_ENTRIES / PER_TRACE_ENTRIES_AVERAGE)

// 2 ^ 20 = 1 秒，单位是微秒
#define MAX_LATENCY_SLOTS 20

// 分布表示字符个数
#define LATENCY_HISTOGRAM_CHARS 40

struct irqoff_stack_trace {
    uint32_t nr_entries; // 这个 stack 堆栈的深度
    unsigned long *entries;
};

struct irqoff_latency {
    uint64_t last_timestamp; // 上次采样的时间戳

    // stack_traces 数组索引
    uint32_t nr_stack_traces;
    struct irqoff_stack_trace stack_traces[MAX_STACK_TRACE_ENTRIES];
    uint32_t nr_entries; // 全部 stack 的深度，所有 stack_traces 数组的 nr_entries 加起来
    unsigned long entries[MAX_TRACE_ENTRIES];

    uint32_t latency_slots
            [MAX_LATENCY_SLOTS]; // 延迟时间分布，范围 2^(n+1)*sampline_period~2(n+2)*sampline_period ms 的数量

    /*task comms*/
    char task_comms[MAX_STACK_TRACE_ENTRIES][TASK_COMM_LEN];
    /*task pids*/
    pid_t task_pids[MAX_STACK_TRACE_ENTRIES];
    struct {
        uint64_t nsecs : 63;
        uint64_t more : 1;
    } latency_ext[MAX_STACK_TRACE_ENTRIES];
};

struct per_cpu_irq_latency {
    struct timer_list timer;
    struct hrtimer hrtime; // 高分辨率定时器
    struct irqoff_latency hardirq_off_latency;
    struct irqoff_latency softirq_off_latency;

    bool softirq_delayed; //软中断延迟标识
};

/*
* @brief 追踪开关，默认关闭
*/
static bool __trace_enabled = false;
/*
* @brief 默认采样周期，10,000,000ns，单位是纳秒
*/
static uint64_t __sampling_period = 10 * 1000 * 1000UL;
/*
* @brief 默认追踪中断关闭的延迟，50,000,000ns，单位是纳秒
*/
static uint64_t __irqoff_trace_latency = 50 * 1000 * 1000UL;

/* 指向一个 per-CPU 内存块的指针，而 DEFINE_PER_CPU 在每个 CPU 上都会分配一个对象*/
static struct per_cpu_irq_latency __percpu *__cpu_stack_trace;

// 自定义中断堆栈保存函数
typedef unsigned int (*cw_stack_trace_save_regs_t)(struct pt_regs *regs,
                                                   unsigned long *store,
                                                   unsigned int size,
                                                   unsigned int skipnr);

static cw_stack_trace_save_regs_t __cw_stack_trace_save_regs = NULL;

/*
1：stack_trace_save_regs 函数没有导出，只有通过 kallsyms_lookup_name 来获取该 symbol 地址
2: 5.14 版本之前的内核，kallsyms_lookup_name 也没有导出，所以先通过 kprobe 获取该函数的地址再获取 stack_trace_save_regs
   0bd476e6c67190b5eb7b6e105c8db8ff61103281: kallsyms: unexport kallsyms_lookup_name() and kallsyms_on_each_symbol()
*/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 14, 0)
static int32_t __dummy_stack_trace_save_regs_pre_handler(struct kprobe *p,
                                                         struct pt_regs *regs)
{
    // 这个函数是为了让 kprobe 能够匹配到 stack_trace_save_regs 函数
    // 这里不需要做任何事情，只是为了让 kprobe 能够匹配到该函数
    return 0;
}
#endif

static int32_t __get_stack_trace_save_regs(void)
{
// 5.14 是 redhat9 的内核版本
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 14, 0)
    // 使用 kallsyms_lookup_name 获取 stack_trace_save_regs
    __cw_stack_trace_save_regs =
            (cw_stack_trace_save_regs_t)kallsyms_lookup_name(
                    "stack_trace_save_regs");
#else
    // 使用 kprobe 获取 stack_trace_save_regs
    int32_t ret;
    struct kprobe kp = {
        .symbol_name = "stack_trace_save_regs",
        .pre_handler = __dummy_stack_trace_save_regs_pre_handler,
    };
    ret = register_kprobe(&kp);
    if (ret < 0) {
        pr_err(MODULE_TAG
               " failed to register kprobe for stack_trace_save_regs\n");
        return ret;
    }
    // kprobe 注册成功后，kprobe 结构体的 addr 字段会被填充为匹配到的函数地址
    __cw_stack_trace_save_regs = (cw_stack_trace_save_regs_t)kp.addr;
    // 注销
    unregister_kprobe(&kp);

#endif
    if (!__cw_stack_trace_save_regs) {
        pr_err(MODULE_TAG " failed to get stack_trace_save_regs function\n");
        return -ENOENT;
    }
    return 0;
}

static void __save_stack_trace(struct pt_regs *regs,
                               struct irqoff_stack_trace *st,
                               unsigned long *entries, uint32_t max_entries,
                               bool skip)
{
    st->entries = entries;
    if (regs) {
        // Return: Number of trace entries stored.
        // stack_trace_save_regs 这个函数没有 EXPORT_SYMBOL_GPL，在模块中没法调用
        // *中断处理程序需要记录被中断代码的调用路径
        st->nr_entries =
                __cw_stack_trace_save_regs(regs, entries, max_entries, skip);
    } else {
        st->nr_entries = stack_trace_save(entries, max_entries, skip);
    }
}

static int32_t __irqoff_tracer_save_stack(struct pt_regs *regs, bool is_hardirq,
                                          uint64_t delay_nsecs)
{
    uint32_t nr_entries, nr_stack_traces;
    struct per_cpu_irq_latency *pcpu;
    struct irqoff_stack_trace *stack_trace;
    struct irqoff_latency *latency;

    pcpu = this_cpu_ptr(__cpu_stack_trace);
    latency = is_hardirq ? &(pcpu->hardirq_off_latency) :
                           &(pcpu->softirq_off_latency);

    nr_stack_traces = latency->nr_stack_traces;
    // 不能超过记录堆栈的数量
    if (unlikely(nr_stack_traces >= MAX_STACK_TRACE_ENTRIES)) {
        pr_warn(MODULE_TAG
                " irqoff stack traces overflow, nr_stack_traces:%u\n",
                nr_stack_traces);
        return -1;
    }

    nr_entries = latency->nr_entries;
    // 不能超过所有堆栈深度的总和
    if (unlikely(nr_entries >= MAX_TRACE_ENTRIES)) {
        pr_warn(MODULE_TAG
                " irqoff stack trace entries overflow, nr_entries:%u\n",
                nr_entries);
        return -1;
    }

    // 在 hrtimer 回调函数中（硬中断上下文）current->comm 获得的是被中断的任务的名称，
    strlcpy(latency->task_comms[nr_stack_traces], current->comm, TASK_COMM_LEN);
    latency->task_pids[nr_stack_traces] = current->pid;
    // 延迟时间，单位是纳秒
    latency->latency_ext[nr_stack_traces].nsecs = delay_nsecs;
    // *不是硬中断且 regs 不为 NULL！但是，通常情况下只有在硬中断下才能获取被中断 task 的寄存器值
    latency->latency_ext[nr_stack_traces].more = !is_hardirq && regs;

    stack_trace = &latency->stack_traces[nr_stack_traces];

    // 调用内核函数保存堆栈追踪信息
    __save_stack_trace(regs, stack_trace, &latency->entries[nr_entries],
                       MAX_TRACE_ENTRIES - nr_entries, 0);
    // 堆栈深度累计
    latency->nr_entries += stack_trace->nr_entries;

    /*
    !1：这是同步原语，在 smp 环境下，这种写操作会在内存层面插入屏障，确保在写入 nr_stack_traces 之前，对 latency
    !   结构体的其他字段的写入操作已经完成。
    *2：并且其它 CPU 在读取 nr_stack_traces 时，会看到最新的值。
    *3：它并不能保证这个 per-cpu 变量的值会被同步到其他 CPU 上，因为每个 CPU 看到的都是自己的副本，其他 CPU 访问的是自己的 per-cpu 区域。
    *4：smp_call_function()、get_cpu_var()、for_each_possible_cpu() 等方式跨 CPU 访问其他 CPU 的 per-cpu 变量时，才需要考虑同步和可见性问题。
    *
    *5：为什么不能直接使用__this_cpu_inc 来增加 stack_trace->nr_irqoff_trace，而需要使用 smp_store_release 呢？
    *   a：如果直接用 __this_cpu_inc 增加计数器，编译器和 CPU 可能会对写入操作进行重排序，
    *      导致其他 CPU 看到 nr_irqoff_trace 增加了，但相关的数据内容还没写完，这样会引发数据不一致或读取到未初始化的数据。
    *   b：smp_store_release 在写入 nr_irqoff_trace 时，会插入一个“release”内存屏障，确保在它之前对 stack_trace 其它成员的写操作都已经完成并对其他 CPU 可见
    *     这样，其他 CPU 通过“acquire”语义读取 nr_irqoff_trace 时，可以确保看到的数据是完整且一致的。
    !!       b1：用 smp_store_release 的场景，往往是因为有“跨 CPU 访问”
    !!           其他 CPU 可能会通过某种方式（如调试、统计、遍历所有 CPU 的数据）访问本 CPU 的 per-cpu 变量。
    !!           有些内核代码会在非本地 CPU 上读取或分析其他 CPU 的 per-cpu 数据（比如通过 /proc、trace、dump 等接口）

    !!       nr_irqoff_trace 就是一个“发布点”：它的增加意味着“有新数据可读”。
    *   c：__this_cpu_inc 只是对本 CPU 的 per-cpu 变量做原子自增，不涉及任何内存屏障，也不保证其它成员的写入顺序。
    *     因此它适合用在不需要同步数据内容的场景。
    *
    * 6：什么 release/acquire 语意
    ??  release（释放）语义：写操作时，保证在此之前的所有写操作都已经完成，并且对其他 CPU 可见，然后再执行带有 release 语义的写入。
    ??  acquire（获取）语义：读操作时，保证在此之后的所有读操作都不会被重排序到带有 acquire 语义的读取之前。
    !!  一个线程/CPU 先写好数据，再“发布”一个信号；另一个线程/CPU 先“获取”信号，再读取数据时，看到的数据一定是完整的。
    ??  线程 A：data = 42; // 1. 写入数据
    *          smp_store_release(&flag, 1); // 2. 发布信号（release 语义）
    ??  线程 B：while (!smp_load_acquire(&flag)); // 3. 等待信号（acquire 语义）
    *          printf("%d\n", data); // 4. 读取数据
    !!  其实在一个写多个读的场景下，release/acquire 语义可以保证读到的数据是最新的。不需要加锁，确实可以做到 lockless 并发访问
    !!  前提
    ??  1:只有一个写者（不会有多个写者同时写同一份数据）。
    ??  2:所有读者都通过 acquire 语义读取“信号”或“标志位”。
    ??  3:写者通过 release 语义写入“信号”或“标志位”。
    ??  4:读者只在看到“信号”已发布后才去读取数据。
    !!  典型应用：Linux 内核中的 RCU、trace、环形缓冲区、单生产者多消费者队列等

    访问 nr_stack_traces 时，需要使用 smp_load_acquire 来确保读取到的值是最新的，
    保证 cat /proc/xxx时那边读取到最新的数据
    */
    smp_store_release(&latency->nr_stack_traces, nr_stack_traces + 1);

    if (unlikely(latency->nr_entries >= MAX_TRACE_ENTRIES - 1)) {
        // 告警，累计堆栈深度超过了最大值
        pr_warn(MODULE_TAG " irqoff trace entries overflow, "
                           "nr_entries:%u, max_entries:%ld\n",
                latency->nr_entries, MAX_TRACE_ENTRIES);
        return -1;
    }

    return 0;
}

static int32_t __irqoff_tracer_record(uint64_t delta, bool is_hardirq,
                                      bool skip)
{
    int32_t index = 0;
    uint64_t throttle = __sampling_period << 1; // 阈值是采样周期的两倍
    struct per_cpu_irq_latency *pcpu;

    // 如果 hrtimer 的中断延迟小于采样周期的两倍，则不记录
    if (delta < throttle) {
        return -1;
    }

    /* 计算 slot，单位 ms，千分之一秒
        index=0: 2*sampline_period ~ 4*sampling_period - 1
        index=1: 4*sampline_period ~ 8*sampling_period - 1
        index=2: 8*sampline_period ~ 16*sampling_period - 1
        index=3: 16*sampline_period ~ 32*sampling_period - 1
        index=4: 32*sampline_period ~ 64*sampling_period - 1
        index=5: 64*sampline_period ~ 128*sampling_period - 1
        ...

        eg: delta = 30000000ns, __sampling_period = 10000000ns, ilog2(3) = 1, index = 0
            delta = 500000000ns, __sampling_period = 10000000ns, ilog2(50) = 5, index = 4
    */
    index = ilog2(delta / __sampling_period) - 1;
    if (unlikely(index >= MAX_LATENCY_SLOTS)) {
        index = MAX_LATENCY_SLOTS - 1; // 超过最大槽位，放到最后一个槽位
    }

    pcpu = this_cpu_ptr(__cpu_stack_trace);

    if (is_hardirq) {
        // 是 hrtimer 超时，记录该 slot 的数量
        pcpu->hardirq_off_latency.latency_slots[index]++;
    } else if (!skip) {
        // 软中断延迟采样
        pcpu->softirq_off_latency.latency_slots[index]++;
    }

    // 如果延迟超过了阈值，记录堆栈追踪信息
    if (unlikely(delta > __irqoff_trace_latency)) {
        // 记录中断延迟超时堆栈
        __irqoff_tracer_save_stack(skip ? get_irq_regs() : NULL, is_hardirq,
                                   delta);
    }

    return 0;
}

// *****************latency trace********** */
static void __irqoff_tracer_latency_show_stacks(struct seq_file *m, void *v,
                                                bool is_hardirq)
{
    int32_t cpu;
    struct per_cpu_irq_latency *pcpu;
    struct irqoff_latency *latency;
    uint32_t nr_stack_traces, i_stack, i_entries;
    struct irqoff_stack_trace *stack_trace;

    for_each_online_cpu (cpu) {
        pcpu = per_cpu_ptr(__cpu_stack_trace, cpu);
        latency = is_hardirq ? &(pcpu->hardirq_off_latency) :
                               &(pcpu->softirq_off_latency);

        // Paired to smp_store_release in __irqoff_tracer_save_stack
        // 为了读取 nr_stack_traces 的最新值，保证前面的堆栈数据已经写入
        nr_stack_traces = smp_load_acquire(&latency->nr_stack_traces);

        if (nr_stack_traces == 0) {
            continue; // 没有堆栈数据
        }

        seq_printf(m, "CPU %d\n", cpu);

        for (i_stack = 0; i_stack < nr_stack_traces; i_stack++) {
            stack_trace = &latency->stack_traces[i_stack];

            // 输出延迟的 task 信息，包括延迟的时间
            seq_printf(m, "%*cCOMMAND: %s PID: %d LATENCY: %lu%s\n", 5, ' ',
                       latency->task_comms[i_stack],
                       latency->task_pids[i_stack],
                       latency->latency_ext[i_stack].nsecs / (1000 * 1000UL),
                       latency->latency_ext[i_stack].more ? "+ms" : "ms");

            if (stack_trace->nr_entries == 0) {
                seq_printf(m, "%*cNo stack trace available.\n", 5, ' ');
                continue; // 没有堆栈追踪信息
            }

            for (i_entries = 0; i_entries < stack_trace->nr_entries;
                 i_entries++) {
                unsigned long entry = stack_trace->entries[i_entries];
                // %*c：表示输出一个字符 ' '（空格），并且宽度为 5，即在每行前面填充 5 个空格，起到缩进作用
                // %pS：是内核专用的格式，用于输出一个指针所指向的符号（即函数名），而不是普通的地址。
                // 这里 (void *)trace->entries[i] 是堆栈追踪中的某个地址。
                seq_printf(m, "%*c%pS\n", 5, ' ', (void *)entry);
            }
            seq_putc(m, '\n');

            // 对于可能长时间循环行为
            // 判断是否需要主动让出 cpu 给优先级更高的 task
            // 如果当前内核允许抢占，并且有更高优先级的任务需要运行，cond_resched() 就会调用 schedule()，主动让出 CPU，进行一次调度。
            cond_resched();
        }
    }
}

static int32_t __irqoff_tracer_latency_show(struct seq_file *m, void *v)
{
    seq_printf(m, "irqoff_tracer_latency: %llums, sampling_period: %llums\n\n",
               __irqoff_trace_latency / (1000 * 1000UL),
               __sampling_period / (1000 * 1000UL));

    seq_puts(m, " hardirq:\n");
    __irqoff_tracer_latency_show_stacks(m, v, true);

    seq_putc(m, '\n');

    seq_puts(m, " softirq:\n");
    __irqoff_tracer_latency_show_stacks(m, v, false);

    return 0;
}

static int32_t __irqoff_tracer_latency_open(struct inode *inode,
                                            struct file *file)
{
    return single_open(file, __irqoff_tracer_latency_show, inode->i_private);
}

static void __irqoff_tracer_smp_clear_latency_stack(void *info)
{
    int32_t i;
    struct per_cpu_irq_latency *pcpu = (struct per_cpu_irq_latency *)info;

    // 清理延迟堆栈
    pcpu->hardirq_off_latency.nr_stack_traces = 0;
    pcpu->hardirq_off_latency.nr_entries = 0;
    pcpu->softirq_off_latency.nr_stack_traces = 0;
    pcpu->softirq_off_latency.nr_entries = 0;

    // 清空直方图数据
    for (i = 0; i < MAX_LATENCY_SLOTS; i++) {
        pcpu->hardirq_off_latency.latency_slots[i] = 0;
        pcpu->softirq_off_latency.latency_slots[i] = 0;
    }
}

// 设置__irqoff_trace_latency，必须大于等于采样周期的两倍
static ssize_t __irqoff_tracer_latency_write(struct file *file,
                                             const char __user *ubuf,
                                             size_t cnt, loff_t *ppos)
{
    unsigned long new_latency = 0;
    int32_t cpu;
    struct per_cpu_irq_latency *pcpu;

    if (kstrtoul_from_user(ubuf, cnt, 10, &new_latency))
        return -EINVAL;

    if (new_latency == 0) {
        pr_info(MODULE_TAG " clearing all latency stacks.\n");
        // 清理所有 CPU 上的延迟堆栈
        for_each_online_cpu (cpu) {
            pcpu = per_cpu_ptr(__cpu_stack_trace, cpu);
            smp_call_function_single(
                    cpu, __irqoff_tracer_smp_clear_latency_stack, pcpu, true);
        }
        return cnt;
    }

    if (new_latency < (__sampling_period << 1) / (1000 * 1000UL)) {
        // 延迟阈值必须大于等于采样周期的两倍
        pr_err(MODULE_TAG
               " new latency is too small, must be greater and equal twice the sampling period(%llu).\n",
               (__sampling_period << 1) / (1000 * 1000UL));
        return -EINVAL;
    }

    // 设置新的延迟阈值
    __irqoff_trace_latency = new_latency * 1000 * 1000UL; // 转换为纳秒
    pr_info(MODULE_TAG " set new latency threshold: %llums\n",
            __irqoff_trace_latency / (1000 * 1000UL));

    return cnt;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct file_operations __irqoff_tracer_latency_fops = {
    .open = __irqoff_tracer_latency_open,
    .read = seq_read,
    .write = __irqoff_tracer_latency_write,
    .llseek = seq_lseek,
    .release = single_release,
};
#else
static const struct proc_fops __irqoff_tracer_latency_fops = {
    .proc_open = __irqoff_tracer_latency_open,
    .proc_read = seq_read,
    .proc_write = __irqoff_tracer_latency_write,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};
#endif

// ****************latency histogram********** */
#define STR_BUFFER_SIZE (LATENCY_HISTOGRAM_CHARS + 1)

static void __irqoff_tracer_histogram_show(struct seq_file *m,
                                           const char *header,
                                           const uint32_t *latency_slots,
                                           int32_t slot_count, uint32_t factor)
{
    int32_t index, max_non_zero_slot = 0;
    uint32_t max_count = 0;
    uint32_t histogram_char_num = 0;
    char str[STR_BUFFER_SIZE] = { 0 };

    for (index = 0; index < slot_count; index++) {
        if (latency_slots[index] > max_count) {
            max_count = latency_slots[index];
        }
        if (latency_slots[index] > 0) {
            max_non_zero_slot = index + 1;
        }
    }

    if (max_count == 0) {
        seq_printf(m, "%s No data available.\n", header);
        return;
    }

    // print header
    if (likely(header)) {
        seq_printf(m, "%s\n", header);
    }
    seq_printf(m, "%*c%s%*c : %-9s %s\n", 9, ' ', "msecs", 10, ' ', "count",
               "distribution");
    for (index = 0; index < max_non_zero_slot; index++) {
        uint32_t count = latency_slots[index];
        uint32_t slot_start = (1UL << (index + 1)) * factor;
        uint32_t slot_end = (1UL << (index + 2)) * factor;

        pr_info(MODULE_TAG
                " latency slot %d: %u, start: %u, end: %u, factor: %u\n",
                index, count, slot_start, slot_end, factor);

        histogram_char_num = (count * LATENCY_HISTOGRAM_CHARS) / max_count;
        memset(str, ' ', LATENCY_HISTOGRAM_CHARS);
        memset(str, '=', histogram_char_num);
        str[LATENCY_HISTOGRAM_CHARS] = '\0';

        seq_printf(m, "%10d -> %-10d : %-8u |%s|\n", slot_start, slot_end - 1,
                   count, str);
    }
}

static void __irqoff_tracer_histogram_summary(struct seq_file *m, void *v,
                                              bool is_hardirq)
{
    int32_t cpu, i = 0;
    uint32_t latency_slots[MAX_LATENCY_SLOTS] = { 0 };
    uint32_t *slots = NULL;
    struct per_cpu_irq_latency *pcpu = NULL;

    // 分别汇总每个 cpu 上的延迟槽位
    for_each_online_cpu (cpu) {
        // per_cpu_ptr 的第一个参数应为 per-cpu 变量的地址，而不是结构体成员的地址
        pcpu = per_cpu_ptr(__cpu_stack_trace, cpu);
        slots = is_hardirq ? pcpu->hardirq_off_latency.latency_slots :
                             pcpu->softirq_off_latency.latency_slots;

        for (i = 0; i < MAX_LATENCY_SLOTS; i++) {
            latency_slots[i] += slots[i];
        }
    }
    __irqoff_tracer_histogram_show(m,
                                   is_hardirq ?
                                           "Hardirq-off Latency Histogram:\n" :
                                           "Softirq-off Latency Histogram:\n",
                                   latency_slots, MAX_LATENCY_SLOTS,
                                   __sampling_period / (1000 * 1000UL));
}

static int32_t __irqoff_tracer_histogram(struct seq_file *m, void *v)
{
    __irqoff_tracer_histogram_summary(m, v, true);
    __irqoff_tracer_histogram_summary(m, v, false);

    return 0;
}

static int32_t __irqoff_tracer_histogram_open(struct inode *inode,
                                              struct file *file)
{
    return single_open(file, __irqoff_tracer_histogram, inode->i_private);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct file_operations __irqoff_tracer_histogram_fops = {
    .open = __irqoff_tracer_histogram_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};
#else
static const struct proc_fops __irqoff_tracer_histogram_fops = {
    .proc_open = __irqoff_tracer_histogram_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};
#endif

// ***************irqoff trace 开关********** */
// 高精度定时器回调函数，硬中断上下文
static enum hrtimer_restart
        __irqoff_tracer_hrtimer_callback(struct hrtimer *hrtimer)
{
    uint64_t now = local_clock();
    uint64_t delta, soft_delta;
    struct per_cpu_irq_latency *pcpu = NULL;

    // 计算高精度计时器的时间间隔
    pcpu = this_cpu_ptr(__cpu_stack_trace);
    delta = now - pcpu->hardirq_off_latency.last_timestamp;
    pcpu->hardirq_off_latency.last_timestamp = now;

    if (!__irqoff_tracer_record(delta, true, false)) {
        // 记录了 hrtimer 延迟
        __this_cpu_write(__cpu_stack_trace->hardirq_off_latency.last_timestamp,
                         now);
    } else if (!__this_cpu_read(__cpu_stack_trace->softirq_delayed)) {
        soft_delta = now - pcpu->softirq_off_latency.last_timestamp;
        // 软中断延迟采样
        if (unlikely(soft_delta >=
                     __irqoff_trace_latency + __sampling_period)) {
            // 软中断延迟采样，记录堆栈
            pcpu->softirq_delayed = true;
            __irqoff_tracer_record(soft_delta, false, true);
        }
    }

    // 重启定时器，间隔为采样周期
    hrtimer_forward_now(hrtimer, ns_to_ktime(__sampling_period));

    if (!in_irq()) {
        pr_warn(MODULE_TAG " hrtimer callback not in irq context\n");
    }
    return HRTIMER_RESTART;
}

// 传统定时器回调函数
static void __irqoff_tracer_timer_callback(struct timer_list *timer)
{
    // 获取当前时间戳，纳秒
    uint64_t now = local_clock();
    uint64_t delta;

    delta = now -
            __this_cpu_read(
                    __cpu_stack_trace->hardirq_off_latency.last_timestamp);
    __this_cpu_write(__cpu_stack_trace->softirq_off_latency.last_timestamp,
                     now);
    __this_cpu_write(__cpu_stack_trace->softirq_delayed, false);

    // 记录堆栈
    __irqoff_tracer_record(delta, false, false);

    //继续定时器
    mod_timer(timer, jiffies + msecs_to_jiffies(__sampling_period / 1000000UL));

    if (!in_softirq()) {
        pr_warn(MODULE_TAG
                " timer callback in softirq context, this is not expected\n");
    }
}

static void __irqoff_tracer_smp_start_timer(void *info)
{
    uint64_t now = local_clock();
    struct per_cpu_irq_latency __percpu *latency =
            (struct per_cpu_irq_latency *)info;

    // 设置定时器启动时间
    latency->hardirq_off_latency.last_timestamp = now;
    latency->softirq_off_latency.last_timestamp = now;

    // *两个定时器的间隔周期基本是相同的
    // 允许你指定一个到期时间范围，而不是一个精确的到期时间点。这在某些情况下非常有用，
    // 例如当对定时器的实际触发时间有一个可接受的微小波动范围时，可以允许内核在内部进行一些优化
    hrtimer_start_range_ns(&(latency->hrtime), ns_to_ktime(__sampling_period),
                           0, HRTIMER_MODE_PINNED);
    latency->timer.expires =
            jiffies + msecs_to_jiffies(__sampling_period / 1000000UL);
    add_timer_on(&latency->timer, smp_processor_id());
}

static void __irqoff_tracer_start_timers(void)
{
    int32_t cpu;
    struct per_cpu_irq_latency *pcpu;

    for_each_online_cpu (cpu) {
        struct hrtimer *hrtimer;
        struct timer_list *timer;
        pcpu = per_cpu_ptr(__cpu_stack_trace, cpu);
        // 初始化高精度计时器
        hrtimer = &(pcpu->hrtime);
        // 定时器绑定到 cpu 上，回调函数只会在该 cpu 上执行，而不会被迁移
        hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_PINNED);
        hrtimer->function = __irqoff_tracer_hrtimer_callback;

        // 初始化传统定时器
        timer = &(pcpu->timer);
        // TIMER_IRQSAFE 表示回调函数保证中断安全
        timer_setup(timer, __irqoff_tracer_timer_callback,
                    TIMER_PINNED | TIMER_IRQSAFE);

        // 在指定 cpu 上启动定时器，等待函数执行完毕
        smp_call_function_single(cpu, __irqoff_tracer_smp_start_timer, pcpu,
                                 true);
    }
}

static void __irqoff_tracer_stop_timers(void)
{
    int32_t cpu;

    for_each_online_cpu (cpu) {
        struct hrtimer *hrtimer;
        struct timer_list *timer;
        struct per_cpu_irq_latency *pcpu;

        pcpu = per_cpu_ptr(__cpu_stack_trace, cpu);
        // 获取当前 CPU 的高精度计时器和传统定时器
        hrtimer = &(pcpu->hrtime);
        timer = &(pcpu->timer);

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

#define MAX_INPUT_SIZE 8
static ssize_t __irqoff_tracer_enabled_write(struct file *file,
                                             const char __user *buf, size_t cnt,
                                             loff_t *ppos)
{
    char kbuf[MAX_INPUT_SIZE]; // 足够存储 "true", "false", "0", "1", "on", "off", "y", "n" 等
    ssize_t ret;
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
        pr_err(MODULE_TAG " copy_from_user failed: %zd\n", ret);
        return -EFAULT;
    }
    kbuf[cnt] = '\0'; // 确保字符串以空终止

    ret = kstrtobool(kbuf, &new_enabled);
    if (ret) {
        pr_err(MODULE_TAG
               " invalid input '%s'. kstrtobool failed: %ld. Please use '0', '1', 'true', 'false', 'on', 'off', 'y', or 'n'.\n",
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

static int32_t __irqoff_tracer_sampling_period_open(struct inode *inode,
                                                    struct file *file)
{
    return single_open(file, __sampling_period_show, inode->i_private);
}

static ssize_t __irqoff_tracer_sampling_period_write(struct file *file,
                                                     const char __user *ubuf,
                                                     size_t cnt, loff_t *ppos)
{
    unsigned long new_period;
    ssize_t ret;

    if (!__trace_enabled) {
        pr_err(MODULE_TAG
               " tracing is not enabled, cannot set sampling period\n");
        return -EPERM; // 操作不允许
    }

    ret = kstrtoul_from_user(ubuf, cnt, 10, &new_period);
    if (ret)
        return ret;

    __sampling_period = new_period * 1000 * 1000UL; // 转换为纳秒
    /*
    eg:
        __sampling_period = 10000000ns, __irqoff_trace_latency = 15000000ns
        ==> __irqoff_trace_latency = 20000000ns

        __sampling_period = 10000000ns, __irqoff_trace_latency = 30000000ns
        ==> __irqoff_trace_latency = 30000000ns
    */
    // 如果采样周期大于两倍的延迟周期，将延迟周期设置为采样周期的两倍
    if (__sampling_period > (__irqoff_trace_latency >> 1)) {
        // 采样定理，大于等于 2 倍采样周期时间才能反映真实情况，这样可以更全面地捕捉跨周期的事件，提升统计结果的准确性和鲁棒性
        __irqoff_trace_latency = __sampling_period << 1;
    }

    pr_info(MODULE_TAG
            " updated sampling period:%llums, irqoff trace latency:%llums\n",
            __sampling_period / (1000 * 1000UL),
            __irqoff_trace_latency / (1000 * 1000UL));

    return cnt;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct file_operations __irqoff_tracer_sampling_period_fops = {
    .open = __irqoff_tracer_sampling_period_open,
    .read = seq_read,
    .write = __irqoff_tracer_sampling_period_write,
    .llseek = seq_lseek,
    .release = single_release,
};
#else
static const struct proc_fops __irqoff_tracer_sampling_period_fops = {
    .proc_open = __irqoff_tracer_sampling_period_open,
    .proc_read = seq_read,
    .proc_write = __irqoff_tracer_sampling_period_write,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};
#endif

#if defined(FOR_TEST)
static ssize_t __irqoff_tracer_test_write(struct file *file,
                                          const char __user *ubuf, size_t cnt,
                                          loff_t *ppos)
{
    char kbuf[MAX_INPUT_SIZE];

    if (cnt < 1) {
        pr_err(MODULE_TAG " no input provided.\n");
        return -EINVAL; // 没有输入
    }
    if (cnt > MAX_INPUT_SIZE - 1) {
        cnt = MAX_INPUT_SIZE - 1; // 确保不会溢出
    }

    // 从用户空间复制数据到内核空间
    if (copy_from_user(kbuf, ubuf, cnt)) {
        pr_err(MODULE_TAG " failed to copy data from user space.\n");
        return -EFAULT; // 发生错误
    }
    kbuf[cnt] = '\0'; // 确保字符串以 null 结尾

    // Implementation of the write function for the test
    // 写入 h 字符，屏蔽本地硬中断
    if (kbuf[0] == 'h') {
        pr_info(MODULE_TAG " disabling local hardirq.\n");
        local_irq_disable();
        // 延迟 100ms
        mdelay(100);
        local_irq_enable();
        pr_info(MODULE_TAG " local hardirq enabled.\n");
    } else if (kbuf[0] == 's') {
        // 写入 s 字符，屏蔽本地软中断
        pr_info(MODULE_TAG " disabling local softirq.\n");
        local_bh_disable();
        // 延迟 100ms
        mdelay(100);
        local_bh_enable();
        pr_info(MODULE_TAG " local softirq enabled.\n");
    } else {
        pr_err(MODULE_TAG " invalid command '%c', use 'h' or 's'.\n", kbuf[0]);
        return -EINVAL;
    }
    return cnt;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct file_operations __irqoff_tracer_test_fops = {
    .write = __irqoff_tracer_test_write,
};
#else
static const struct proc_fops __irqoff_tracer_test_fops = {
    .proc_write = __irqoff_tracer_test_write,
};
#endif // LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)

#endif // FOR_TEST

static int32_t __init __cw_irqoff_tracer_init(void)
{
    struct proc_dir_entry *parent_dir;

    __cpu_stack_trace = alloc_percpu(struct per_cpu_irq_latency);
    if (!__cpu_stack_trace) {
        pr_err(MODULE_TAG " failed to allocate per-CPU stack trace memory.\n");
        return -ENOMEM;
    }

    if (__get_stack_trace_save_regs() < 0) {
        goto err_free_percpu;
    }

    // 创建 /proc/irqoff_tracer 目录
    parent_dir = proc_mkdir("irqoff_tracer", NULL);
    if (!parent_dir) {
        pr_err(MODULE_TAG " failed to create /proc/irqoff_tracer directory.\n");
        goto err_free_percpu;
    }

    if (!proc_create("histogram", 0644, parent_dir,
                     &__irqoff_tracer_histogram_fops)) {
        pr_err(MODULE_TAG " failed to create /proc/irqoff_tracer/histogram.\n");
        goto err_remove_proc;
    }

    if (!proc_create("latency", 0644, parent_dir,
                     &__irqoff_tracer_latency_fops)) {
        pr_err(MODULE_TAG " failed to create /proc/irqoff_tracer/latency.\n");
        goto err_remove_proc;
    }

    if (!proc_create("enabled", 0644, parent_dir,
                     &__irqoff_tracer_enabled_fops)) {
        pr_err(MODULE_TAG " failed to create /proc/irqoff_tracer/enabled.\n");
        goto err_remove_proc;
    }

    if (!proc_create("sampling_period", 0644, parent_dir,
                     &__irqoff_tracer_sampling_period_fops)) {
        pr_err(MODULE_TAG
               " failed to create /proc/irqoff_tracer/sampling_period.\n");
        goto err_remove_proc;
    }

#if defined(FOR_TEST)
    //#pragma message("FOR_TEST is defined: test proc entry will be built")
    pr_info(MODULE_TAG " test proc entry created, use 'h' to disable hardirq "
                       "and 's' to disable softirq.\n");

    if (!proc_create("for_test", 0644, parent_dir,
                     &__irqoff_tracer_test_fops)) {
        pr_err(MODULE_TAG " failed to create /proc/irqoff_tracer/test.\n");
        goto err_remove_proc;
    }
#endif // FOR_TEST

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
MODULE_LICENSE("GPL");
MODULE_AUTHOR("calmwu <wubo0067@hotmail.com>");
MODULE_DESCRIPTION("cw_irqoff_tracer");
MODULE_VERSION("0.0.1");