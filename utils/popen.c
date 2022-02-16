/*
 * @Author: CALM.WU
 * @Date: 2021-10-12 12:01:21
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-11-25 10:35:02
 */

#include "compiler.h"
#include "log.h"
#include "popen.h"

#define PIPE_READ 0
#define PIPE_WRITE 1

#define FLAG_CREATE_PIPE \
    1  // Create a pipe like popen() when set, otherwise set stdout to /dev/null
#define FLAG_CLOSE_FD \
    2  // Close all file descriptors other than STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO

/*
 * Returns -1 on failure, 0 on success. When FLAG_CREATE_PIPE is set, on success set the FILE *fp
 * pointer.
 */
static inline int custom_popene(const char *command, volatile pid_t *pidptr, char **env,
                                uint8_t flags, FILE **fpp) {
    FILE *                     fp           = NULL;
    int32_t                    ret          = 0;  // success return value
    int32_t                    pipefd[2]    = { -1, -1 };
    int32_t                    error        = 0;
    pid_t                      pid          = 0;
    char *const                spawn_argv[] = { "sh", "-c", (char *)command, NULL };
    posix_spawnattr_t          attr;
    posix_spawn_file_actions_t actions;

    if (command == NULL || pidptr == NULL) {
        error("Invalid parameter");
        return -EINVAL;
    }

    if (flags & FLAG_CREATE_PIPE) {
        if (pipe(pipefd) != 0) {
            error("pipe() failed: %s", strerror(errno));
            return -errno;
        }
        if ((fp = fdopen(pipefd[PIPE_READ], "r")) == NULL) {
            error("fdopen() failed: %s", strerror(errno));
            goto error_after_pipe;
        }
    }

    if (flags & FLAG_CLOSE_FD) {
        // Mark all files to be closed by the exec() stage of posix_spawn()
        // close-on-exec 在执行exec之前关闭指定的文件描述符
        int32_t i = 0;
        // 除了标准输入，错误输出句柄都在exec后关闭，新程序不继承
        for (i = (int)(sysconf(_SC_OPEN_MAX) - 1); i >= 0; i--) {
            if (i != STDIN_FILENO && i != STDERR_FILENO) {
                fcntl(i, F_SETFD, FD_CLOEXEC);
            }
        }
    }

    if (!posix_spawn_file_actions_init(&actions)) {
        if (flags & FLAG_CREATE_PIPE) {
            // move the pipe to stdout in the child
            if (posix_spawn_file_actions_adddup2(&actions, pipefd[PIPE_WRITE], STDOUT_FILENO)) {
                error("posix_spawn_file_actions_adddup2() failed: %s", strerror(errno));
                goto error_after_posix_spawn_file_actions_init;
            }
        } else {
            if (posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY,
                                                 0)) {
                error("posix_spawn_file_actions_addopen() failed: %s", strerror(errno));
            }
        }
    } else {
        error("posix_spawn_file_actions_init() failed: %s", strerror(errno));
        goto error_after_pipe;
    }

    // 初始化属性
    if (!(error = posix_spawnattr_init(&attr))) {
        sigset_t mask;

        if (posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF)) {
            error("posix_spawnattr_setflags() failed: %s", strerror(errno));
        }
        sigemptyset(&mask);
        if (posix_spawnattr_setsigmask(&attr, &mask)) {
            error("posix_spawnattr_setsigmask() failed: %s", strerror(errno));
        }
    } else {
        error("posix_spawnattr_init() failed: %s", strerror(errno));
    }

    // TODO: set lock
    // fork and exec 新程序
    if (!posix_spawn(&pid, "/bin/sh", &actions, &attr, spawn_argv, env)) {
        *pidptr = pid;
        debug("Spawned command: '%s' on pid %d from parent pid %d", command, pid, getpid());
    } else {
        error("Failed to spawn command '%s' from parent pid %d, posix_spawn() failed reason: %s",
              command, getpid(), strerror(errno));
        // 子进程创建失败，关闭pipe的读
        if (flags & FLAG_CREATE_PIPE) {
            fclose(fp);
            fp = NULL;
        }
        ret = -1;
    }

    // 父进程关闭pipe write fd，这个作为子进程的标准输出
    if (flags & FLAG_CREATE_PIPE) {
        close(pipefd[PIPE_WRITE]);
        if (0 == ret) {
            // 将pipe read赋值给fpp
            *fpp = fp;
        }
    }

    // 释放attr
    if (!error) {
        if (posix_spawnattr_destroy(&attr)) {
            error("posix_spawnattr_destroy() failed: %s", strerror(errno));
        }
    }

    // 释放actions
    if (posix_spawn_file_actions_destroy(&actions)) {
        error("posix_spawn_file_actions_destroy() failed: %s", strerror(errno));
    }

    return ret;

// 上面必须return，就算没有goto执行，下面的代码也会执行
error_after_posix_spawn_file_actions_init:
    if (posix_spawn_file_actions_destroy(&actions)) {
        error("posix_spawn_file_actions_destroy() failed: %s", strerror(errno));
    }

error_after_pipe:
    if (flags & FLAG_CREATE_PIPE) {
        if (fp) {
            fclose(fp);
            fp = NULL;
        } else if (pipefd[PIPE_READ] != -1) {
            close(pipefd[PIPE_READ]);
        }

        if (pipefd[PIPE_WRITE] != -1) {
            close(pipefd[PIPE_WRITE]);
        }
    }

    return -errno;
}

// See man environ
extern char **environ;

FILE *mypopen(const char *command, volatile pid_t *pidptr) {
    FILE *fp = NULL;
    custom_popene(command, pidptr, environ, FLAG_CREATE_PIPE | FLAG_CLOSE_FD, &fp);
    return fp;
}

FILE *mypopene(const char *command, volatile pid_t *pidptr, char **env) {
    FILE *fp = NULL;
    custom_popene(command, pidptr, env, FLAG_CREATE_PIPE | FLAG_CLOSE_FD, &fp);
    return fp;
}

static int32_t custom_pclose(FILE *fp, pid_t pid) {
    int32_t   ret = 0;
    siginfo_t info;

    debug("Request to pclose on pid: %d", pid);

    if (fp) {
        // 关闭父进程pipe读
        fclose(fp);
    }

    errno = 0;

    ret = waitid(P_PID, (id_t)pid, &info, WEXITED);

    if (ret != -1) {
        debug("child pid: %d exited with status: %d", pid, info.si_status);
        switch (info.si_code) {
        case CLD_EXITED:
            if (info.si_status != 0) {
                debug("child pid: %d exited with status: %d", pid, info.si_status);
            }
            return info.si_status;
        case CLD_KILLED:
            if (info.si_status == SIGTERM) {
                debug("child pid: %d killed by SIGTERM", pid);
                return 0;
            }
            debug("child pid: %d killed by signal: %d", pid, info.si_status);
            return -1;
        case CLD_DUMPED:
            debug("child pid: %d core dumped by signal: %d", pid, info.si_status);
            return -2;
        case CLD_STOPPED:
            debug("child pid: %d stopped by signal: %d", pid, info.si_status);
            return 0;
        case CLD_TRAPPED:
            debug("child pid: %d trapped by signal: %d", pid, info.si_status);
            return -4;
        case CLD_CONTINUED:
            debug("child pid: %d continued by signal: %d", pid, info.si_status);
            return 0;
        default:
            error("child pid %d gave us a SIGCHLD with code %d and status %d", info.si_pid,
                  info.si_code, info.si_status);
            return -5;
        }
    } else {
        error("pclose() on pid: %d failed: %s", pid, strerror(errno));
    }

    return 0;
}

int32_t mypclose(FILE *fp, pid_t pid) {
    return custom_pclose(fp, pid);
}
