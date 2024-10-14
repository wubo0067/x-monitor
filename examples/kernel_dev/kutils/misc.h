/*
 * @Author: CALM.WU
 * @Date: 2024-04-26 14:05:57
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2024-10-14 18:05:58
 */

#ifndef __CW_MISC_H
#define __CW_MISC_H

// 如果编译没有代入版本信息
#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#else
#define KERNEL_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + (c))
#endif

#ifdef __KERNEL__

#define PRINT_CTX()                                                                                              \
    do {                                                                                                         \
        int PRINTCTX_SHOWHDR = 1;                                                                                \
        char intr = '.';                                                                                         \
        if (!in_task()) {                                                                                        \
            if (in_irq() && in_softirq())                                                                        \
                intr = 'H'; /* hardirq occurred inside a softirq */                                              \
            else if (in_irq())                                                                                   \
                intr = 'h'; /* hardirq is running */                                                             \
            else if (in_softirq())                                                                               \
                intr = 's';                                                                                      \
        } else                                                                                                   \
            intr = '.';                                                                                          \
                                                                                                                 \
        if (PRINTCTX_SHOWHDR == 1)                                                                               \
            pr_debug(                                                                                            \
                    "CPU)  task_name:PID  | irqs,need-resched,hard/softirq,preempt-depth  /* func_name() */\n"); \
        pr_debug("%03d) %c%s%c:%d   |  "                                                                         \
                 "%c%c%c%u   "                                                                                   \
                 "/* %s() */\n",                                                                                 \
                 raw_smp_processor_id(), (!current->mm ? '[' : ' '),                                             \
                 current->comm, (!current->mm ? ']' : ' '), current->pid,                                        \
                 (irqs_disabled() ? 'd' : '.'), (need_resched() ? 'N' : '.'),                                    \
                 intr, (preempt_count() && 0xff), __func__);                                                     \
    } while (0)

/*
 * SHOW_DELTA() macro
 * Show the difference between the timestamps passed
 * Parameters:
 *  @later, @earlier : nanosecond-accurate timestamps
 * Expect that @later > @earlier
 *
 * Grab a timestamp using the ktime_get_real_ns() API..
 */
#define SHOW_DELTA(later, earlier)                                                         \
    do {                                                                                   \
        if (time_after((unsigned long)later, (unsigned long)earlier)) {                    \
            s64 delta_ns = ktime_to_ns(ktime_sub(later, earlier));                         \
            pr_info("delta: %lld ns", delta_ns);                                           \
            if (delta_ns / 1000 >= 1)                                                      \
                pr_cont(" (~ %lld us", delta_ns / 1000);                                   \
            if (delta_ns / 1000000 >= 1)                                                   \
                pr_cont(" ~ %lld ms", delta_ns / 1000000);                                 \
            if (delta_ns / 1000 >= 1)                                                      \
                pr_cont(")\n");                                                            \
        } else                                                                             \
            pr_warn("SHOW_DELTA(): *invalid* earlier > later? (check order of params)\n"); \
    } while (0)

#endif // __KERNEL__

#endif // __CW_MISC_H
