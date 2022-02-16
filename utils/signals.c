/*
 * @Author: CALM.WU
 * @Date: 2021-10-15 10:26:53
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-10-28 14:58:10
 */

#include "signals.h"
#include "common.h"
#include "compiler.h"
#include "log.h"

struct signals_waiting {
    int32_t                 signo;         // 信号
    const char             *signame;       // 信号名称
    uint32_t                receive_count; //
    enum signal_action_mode action_mode;   //
};

static struct signals_waiting __signal_waiting_list[] = {
    { SIGPIPE, "SIGPIPE", 0, E_SIGNAL_IGNORE },
    { SIGINT, "SIGINT", 0, E_SIGNAL_EXIT_CLEANLY },
    { SIGTERM, "SIGTERM", 0, E_SIGNAL_EXIT_CLEANLY },
    { SIGQUIT, "SIGQUIT", 0, E_SIGNAL_EXIT_CLEANLY },
    { SIGBUS, "SIGBUS", 0, E_SIGNAL_FATAL },
    { SIGCHLD, "SIGCHLD", 0, E_SIGNAL_CHILD },
    { SIGHUP, "SIGHUP", 0, E_SIGNAL_RELOADCONFIG }
};

static void signal_handler(int32_t signo)
{
    // find the entry in the list
    int32_t i;
    for (i = 0; i < (int32_t)ARRAY_SIZE(__signal_waiting_list); i++) {
        if (unlikely(__signal_waiting_list[i].signo == signo)) {
            // 信号接收计数
            __signal_waiting_list[i].receive_count++;

            if (__signal_waiting_list[i].action_mode == E_SIGNAL_FATAL) {
                char buffer[200 + 1];
                snprintf(buffer, 200,
                         "\nSIGNAL HANDLER: received: %s. Oops! This is bad!\n",
                         __signal_waiting_list[i].signame);
                write(STDERR_FILENO, buffer, strlen(buffer));
            }
            return;
        }
    }
}

void signals_init(void)
{
    struct sigaction sa;
    // sa_flags 字段指定对信号进行处理的各个选项。例如SA_NOCLDWAIT
    sa.sa_flags = 0;
    sigfillset(&sa.sa_mask); // 调用信号处理函数时，要屏蔽所有的信号。

    int32_t i = 0;
    for (i = 0; i < (int32_t)ARRAY_SIZE(__signal_waiting_list); i++) {
        switch (__signal_waiting_list[i].action_mode) {
        case E_SIGNAL_IGNORE:
            sa.sa_handler = SIG_IGN;
            break;
        default:
            sa.sa_handler = signal_handler;
            break;
        }
        // 注册信号处理函数
        if (sigaction(__signal_waiting_list[i].signo, &sa, NULL) == -1) {
            error("Cannot set signal handler for %s (%d)",
                  __signal_waiting_list[i].signame,
                  __signal_waiting_list[i].signo);
        }
    }
}

void signals_handle(on_signal_t fn)
{
    while (1) {
        if (pause() == -1 && errno == EINTR) {
            int32_t found = 1;

            while (found) {
                found     = 0;
                int32_t i = 0;

                for (i = 0; i < (int32_t)ARRAY_SIZE(__signal_waiting_list);
                     i++) {
                    if (__signal_waiting_list[i].receive_count > 0) {
                        found                                  = 1;
                        __signal_waiting_list[i].receive_count = 0;
                        const char *signal_name =
                            __signal_waiting_list[i].signame;

                        switch (__signal_waiting_list[i].action_mode) {
                        case E_SIGNAL_EXIT_CLEANLY:
                            info("Received signal %s. Exiting cleanly.",
                                 signal_name);
                            fn(__signal_waiting_list[i].signo,
                               E_SIGNAL_EXIT_CLEANLY);
                            exit(0);
                            break;
                        case E_SIGNAL_SAVE_DATABASE:
                            info("Received signal %s. Saving the database.",
                                 signal_name);
                            break;
                        case E_SIGNAL_FATAL:
                            info("Received signal %s. Exiting with error.",
                                 signal_name);
                            exit(1);
                            break;
                        case E_SIGNAL_RELOADCONFIG:
                            info(
                                "Received signal %s. Reloading the configuration.",
                                signal_name);
                            fn(__signal_waiting_list[i].signo,
                               E_SIGNAL_RELOADCONFIG);
                            break;
                        default:
                            info(
                                "Received signal %s. No signal handler configured, Ignoring it.",
                                signal_name);
                            break;
                        }
                    }
                }
            }
        } else {
            error(
                "pause() returned but it was not interupted by a signal. errno: %d",
                errno);
            break;
        }
    }
}

int32_t signals_block()
{
    sigset_t set;
    sigfillset(&set);

    // sigprocmask vs pthread_sigmask
    // For one, the POSIX.1 standard does not explicitly specify the usage of sigprocmask in multi-threaded programs, so
    // not all implementations may behave predictably in multi-threaded applications. Safer to use pthread_sigmask in
    // this case.
    if (0 != pthread_sigmask(SIG_BLOCK, &set, NULL)) {
        error("Cannot block signals");
        return -1;
    }

    return 0;
}

int32_t signals_unblock()
{
    sigset_t set;
    sigfillset(&set);

    if (0 != pthread_sigmask(SIG_UNBLOCK, &set, NULL)) {
        error("Cannot unblock signals");
        return -1;
    }

    return 0;
}