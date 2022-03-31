/*
 * @Author: CALM.WU
 * @Date: 2022-03-29 16:09:03
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-03-31 10:31:25
 */

#include "process_stat.h"

#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/os.h"
#include "utils/strings.h"
#include "utils/procfile.h"

#define HASH_BUFFER_SIZE (XM_CMD_LINE_MAX + 10)

static const char *__proc_pid_stat_path = "/proc/%d/stat",
                  *__proc_pid_status_path = "/proc/%d/status", *__proc_pid_io_path = "/proc/%d/io";

struct process_stat *process_stat_new(pid_t pid) {
    if (unlikely(pid <= 0)) {
        error("invalid pid: %d", pid);
        return NULL;
    }

    struct process_stat *pstat = calloc(1, sizeof(struct process_stat));
    if (unlikely(NULL == pstat)) {
        error("calloc process_stat failed.");
        return NULL;
    }

    pstat->pid = pid;
    // 读取comm
    get_process_name(pid, pstat->comm, sizeof(pstat->comm));
    // 计算hash
    char hash_buffer[HASH_BUFFER_SIZE] = { 0 };
    snprintf(hash_buffer, HASH_BUFFER_SIZE - 1, "%d %s", pid, pstat->comm);
    pstat->hash = simple_hash(hash_buffer);

    // 打开/proc/pid/stat, status, io, 文件
    char file_path[PATH_MAX] = { 0 };

    snprintf(file_path, PATH_MAX - 1, __proc_pid_status_path, pid);
    pstat->fd_status = open(file_path, O_RDONLY);
    if (unlikely(pstat->fd_status < 0)) {
        error("open '%s' failed, errno: %d", file_path, errno);
        process_stat_free(pstat);
        return NULL;
    } else {
        debug("open '%s' success", file_path);
    }

    snprintf(file_path, PATH_MAX - 1, __proc_pid_stat_path, pid);
    pstat->pf_proc_pid_stat = procfile_open(file_path, NULL, PROCFILE_FLAG_NO_ERROR_ON_FILE_IO);

    snprintf(file_path, PATH_MAX - 1, __proc_pid_io_path, pid);
    pstat->pf_proc_pid_io = procfile_open(file_path, NULL, PROCFILE_FLAG_NO_ERROR_ON_FILE_IO);

    if (unlikely(NULL == pstat->pf_proc_pid_stat || NULL == pstat->pf_proc_pid_io)) {
        error("open /proc/%d/'stat,io' failed.", pid);
        process_stat_free(pstat);
        return NULL;
    }

    debug("process_stat_new: pid: %d, comm: '%s', hash: %u", pid, pstat->comm, pstat->hash);

    return pstat;
}

void process_stat_free(struct process_stat *pstat) {
    if (likely(pstat)) {
        debug("process_stat_free: pid: %d, comm: %s", pstat->pid, pstat->comm);

        if (likely(pstat->pf_proc_pid_io)) {
            procfile_close(pstat->pf_proc_pid_io);
        }

        if (likely(pstat->fd_status > 0)) {
            close(pstat->fd_status);
        }

        if (likely(pstat->pf_proc_pid_stat)) {
            procfile_close(pstat->pf_proc_pid_stat);
        }

        free(pstat);
        pstat = NULL;
    }
}