/*
 * @Author: CALM.WU
 * @Date: 2021-10-15 10:20:51
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-11-03 11:39:35
 */

#include "compiler.h"
#include "daemon.h"
#include "files.h"
#include "log.h"

#include "appconfig/appconfig.h"

const int32_t process_nice_level = 19;
const int32_t process_oom_score  = 1000;

// 设置oom_socre_adj为-1000，表示禁止oom killer杀死该进程
static void oom_score_adj(void) {
    int64_t old_oom_score = 0;
    int32_t ret           = 0;

    ret = read_file_to_int64("/proc/self/oom_score_adj", &old_oom_score);
    if (unlikely(ret < 0)) {
        error("read /proc/self/oom_score_adj failed, ret: %d", ret);
        return;
    }

    if (old_oom_score == process_oom_score) {
        info("oom_score_adj is already %d", process_oom_score);
        return;
    }

    ret = write_int64_to_file("/proc/self/oom_score_adj", process_oom_score);
    if (unlikely(ret < 0)) {
        error("failed to adjust Out-Of-Memory (OOM) score to %d. run with %ld, ret: %d",
              process_oom_score, old_oom_score, ret);
        return;
    }

    info("adjust Out-Of-Memory (OOM) score from %ld to %d.", old_oom_score, process_oom_score);
    return;
}

static inline void set_process_nice_level() {
    if (nice(process_nice_level) == -1) {
        error("Cannot set CPU nice level to %d.", process_nice_level);
    } else {
        debug("Set process nice level to %d.", process_nice_level);
    }
}

static void chown_open_file(int32_t fd, uid_t uid, gid_t gid) {
    if (unlikely(fd == -1)) {
        return;
    }

    struct stat st;
    if (unlikely(fstat(fd, &st) == -1)) {
        error("fstat failed, fd: %d, errno: %d", fd, errno);
        return;
    }

    if ((st.st_uid != uid || st.st_gid != gid) && S_ISREG(st.st_mode)) {
        if (unlikely(fchown(fd, uid, gid) == -1)) {
            error("fchown failed, fd: %d, errno: %d", fd, errno);
        }
    }
    return;
}

int32_t become_user(const char *user, int32_t pid_fd, const char *pid_file) {
    // 获取ruid
    // int32_t is_root = ((getuid() == 0) ? 1 : 0);

    struct passwd *pw = getpwnam(user);
    if (unlikely(!pw)) {
        error("getpwnam failed, user: %s", user);
        return -1;
    }

    // 得到用户的ruid和rgid
    uid_t ruid = pw->pw_uid;
    gid_t rgid = pw->pw_gid;

    if (pid_file[0] != '\0') {
        // 修改pid文件的所有者
        if (chown(pid_file, ruid, rgid) < 0) {
            error("chown pid_file: %s to %u:%u failed.", pid_file, (uint32_t)ruid, (uint32_t)rgid);
        }
    }

    chown_open_file(pid_fd, ruid, rgid);

    if (setgid(rgid) < 0) {
        error("setgid failed, user: %s, ruid: %u, rgid: %u, error: %s", user, (uint32_t)ruid,
              (uint32_t)rgid, strerror(errno));
        return -1;
    }

    if (setegid(rgid) < 0) {
        error("setegid failed, user: %s, ruid: %u, rgid: %u, error: %s", user, (uint32_t)ruid,
              (uint32_t)rgid, strerror(errno));
        return -1;
    }

    if (setuid(ruid) < 0) {
        error("setuid failed, user: %s, ruid: %u, rgid: %u", user, (uint32_t)ruid, (uint32_t)rgid);
        return -1;
    }

    if (seteuid(ruid) < 0) {
        error("seteuid failed, user: %s, ruid: %u, rgid: %u", user, (uint32_t)ruid, (uint32_t)rgid);
        return -1;
    }

    return 0;
}

int32_t become_daemon(int32_t dont_fork, const char *pid_file, const char *user) {
    if (!dont_fork) {
        int32_t i = fork();
        if (i == -1) {
            perror("cannot fork");
            exit(0 - errno);
        }

        if (i != 0) {
            // parent exit
            exit(0);
        }

        // become session leader
        if (setsid() < 0) {
            perror("cannot become session leader");
            exit(0 - errno);
        }

        // fork again
        i = fork();
        if (i == -1) {
            perror("cannot fork");
            exit(0 - errno);
        }

        if (i != 0) {
            // parent exit
            exit(0);
        }
    }

    info("run as user:'%s', pid file:'%s'", user, pid_file);

    // 生成pid文件
    int32_t pid_fd = -1;

    if (pid_file != NULL && pid_file[0] != '\0') {
        pid_fd = open(pid_file, O_RDWR | O_CREAT, 0644);
        if (pid_fd >= 0) {
            char pid_str[32] = { 0 };
            sprintf(pid_str, "%d", getpid());
            if (write(pid_fd, pid_str, strlen(pid_str)) <= 0) {
                error("Cannot write pidfile '%s'.", pid_file);
            }
        } else {
            error("Cannot open pidfile '%s'.", pid_file);
        }
    }

    // file mode creation mask
    umask(0007);

    // adjust my Out-Of-Memory score
    oom_score_adj();

    // 调整进程调度优先级
    set_process_nice_level();

    if (user && *user) {
        if (become_user(user, pid_fd, pid_file) != 0) {
            error("Cannot become user '%s'.", user);
            exit(0 - errno);
        } else {
            info("Become user '%s' success.", user);
        }
    }

    if (pid_fd != -1) {
        close(pid_fd);
    }
    return 0;
}

// kill_pid kills pid with SIGTERM.
int32_t kill_pid(pid_t pid) {
    int32_t ret;
    debug("Request to kill pid %d", pid);

    errno = 0;
    ret   = kill(pid, SIGTERM);
    if (ret == -1) {
        switch (errno) {
        case ESRCH:
            // We wanted the process to exit so just let the caller handle.
            return ret;
        case EPERM:
            error("Cannot kill pid %d, but I do not have enough permissions.", pid);
            break;
        default:
            error("Cannot kill pid %d, but I received an error.", pid);
            break;
        }
    }
    return ret;
}