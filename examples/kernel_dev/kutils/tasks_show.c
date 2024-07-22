/*
 * @Author: CALM.WU
 * @Date: 2024-07-18 10:19:31
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2024-07-18 18:11:06
 */

#define pr_fmt(fmt) "%s: " fmt, KBUILD_MODNAME

#include <linux/sched.h>

// 如果编译没有代入版本信息
#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#else
#define KERNEL_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + (c))
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 10, 0)
#include <linux/sched/signal.h> /* for_each_xxx(), ... */
#endif

static const char *tasklist_hdr =
        "     Name       |  TGID  |   PID  |  RUID |  EUID";

static const char *thrdlist_hdr =
        "------------------------------------------------------------------------------------------\n"
        "    TGID     PID         current           stack-start         Thread Name     MT? # thrds\n"
        "------------------------------------------------------------------------------------------\n";

int32_t show_tasklist(char *buf, size_t buf_size)
{
    int32_t remaining = buf_size - 1;
    int32_t cur_pos = 0;
    int32_t n = 0;
    struct task_struct *p;

    //pr_info("show_tasklist buf_size:%ld\n", buf_size);

    cur_pos = snprintf(buf, remaining, "%s\n", tasklist_hdr);
    remaining -= cur_pos;

    // 因为 for_each_process 使用了 list_entry_rcu，所以该函数才有效
    rcu_read_lock();

    for_each_process (p) {
        // 增加 task_struct 的引用计数
        get_task_struct(p);
        // n 返回值可能比 remaining 大，所以需要检查 remaining
        n = snprintf(buf + cur_pos, (size_t)remaining,
                     "%-16s|%8d|%8d|%7u|%7u\n", p->comm, p->tgid, p->pid,
                     __kuid_val(p->cred->uid), __kuid_val(p->cred->euid));

        // 减少 task_struct 的引用计数
        put_task_struct(p);

        cur_pos += n;
        remaining -= n;
        // pr_info("show_tasklist n:%d, cur_pos:%d, remaining:%d\n", n, cur_pos,
        //         remaining);

        if (remaining <= 0) {
            pr_info("show_tasklist n:%d, cur_pos:%d, remaining:%d, cur_pos+remaining:%d\n",
                    n, cur_pos, remaining, (cur_pos + remaining));
            cur_pos += remaining;
            //pr_warn("Buffer is too small\n");
            break;
        }
    }

    rcu_read_unlock();

    buf[cur_pos] = 0;
    return cur_pos;
}

int32_t show_thrdlist(char *buf, size_t buf_size)
{
    int32_t remaining = buf_size - 1;
    int32_t cur_pos = 0, n, nr_thrds = 1;
    struct task_struct *p, *t;

    cur_pos = snprintf(buf, (size_t)remaining, "%s\n", thrdlist_hdr);
    remaining -= cur_pos;

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)
    do_each_thread(p, t)
    {
#else
    for_each_process_thread (p, t) {
#endif
        get_task_struct(t);
        // task_lock 是一种用于进程控制块（task_struct）的锁
        task_lock(t);

        nr_thrds = get_nr_threads(p);

        if (!p->mm) {
            // kernel thread
            n = snprintf(buf + cur_pos, (size_t)remaining,
                         "%8d %8d  0x%px 0x%px [%16s]\n", p->tgid, t->pid, t,
                         t->stack, t->comm);
        } else {
            if ((p->tgid == t->pid) && (nr_thrds > 1)) {
                // multi-threaded process
                n = snprintf(buf + cur_pos, remaining,
                             "%8d %8d  0x%px 0x%px %16s  %3d\n", p->tgid,
                             t->pid, t, t->stack, t->comm, nr_thrds);
            } else {
                // single-threaded process
                n = snprintf(buf + cur_pos, remaining,
                             "%8d %8d  0x%px 0x%px %16s\n", p->tgid, t->pid, t,
                             t->stack, t->comm);
            }
        }

        task_unlock(t);
        put_task_struct(t);

        cur_pos += n;
        remaining -= n;
        if (remaining <= 0) {
            pr_info("show_tasklist n:%d, cur_pos:%d, remaining:%d, cur_pos+remaining:%d\n",
                    n, cur_pos, remaining, (cur_pos + remaining));
            cur_pos += remaining;
            // 是个双重循环，break 无法跳出，所以用 goto
            goto out;
        }
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)
    }
    while_each_thread(p, t);
#else
    }
#endif
out:
    buf[cur_pos] = 0;
    return cur_pos;
}
