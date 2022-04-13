/*
 * @Author: CALM.WU
 * @Date: 2022-03-29 16:09:03
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-04-13 16:43:12
 */

#include "process_stat.h"

#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/os.h"
#include "utils/strings.h"
#include "utils/procfile.h"
#include "utils/mempool.h"

#define HASH_BUFFER_SIZE (XM_CMD_LINE_MAX + 10)

static const char *__proc_pid_stat_path_fmt = "/proc/%d/stat",
                  *__proc_pid_status_path_fmt = "/proc/%d/status",
                  *__proc_pid_io_path_fmt = "/proc/%d/io", *__proc_pid_fd_path_fmt = "/proc/%d/fd";

#define MAKE_PROCESS_FULL_FILENAME(full_filename, path_fmt, pid)                  \
    do {                                                                          \
        char    file_path[XM_PROC_FILENAME_MAX] = { 0 };                          \
        int32_t n = snprintf(file_path, XM_PROC_FILENAME_MAX - 1, path_fmt, pid); \
        full_filename = strndup(file_path, n);                                    \
    } while (0)

struct process_stat *new_process_stat(pid_t pid, struct xm_mempool_s *xmp) {
    if (unlikely(pid <= 0)) {
        error("[PROCESS] invalid pid: %d", pid);
        return NULL;
    }

    struct process_stat *ps = NULL;
    if (likely(xmp)) {
        ps = (struct process_stat *)xm_mempool_malloc(xmp);
        memset(ps, 0, sizeof(struct process_stat));
    } else {
        ps = (struct process_stat *)calloc(1, sizeof(struct process_stat));
    }

    if (unlikely(NULL == ps)) {
        error("[PROCESS] calloc process_stat failed.");
        return NULL;
    }

    ps->pid = pid;
    // 读取cmd_line
    get_process_name(pid, ps->cmd_line, sizeof(ps->cmd_line));
    // 计算hash
    char hash_buffer[HASH_BUFFER_SIZE] = { 0 };
    snprintf(hash_buffer, HASH_BUFFER_SIZE - 1, "%d %s", pid, ps->comm);
    ps->hash = simple_hash(hash_buffer);

    MAKE_PROCESS_FULL_FILENAME(ps->stat_full_filename, __proc_pid_stat_path_fmt, pid);
    MAKE_PROCESS_FULL_FILENAME(ps->status_full_filename, __proc_pid_status_path_fmt, pid);
    MAKE_PROCESS_FULL_FILENAME(ps->io_full_filename, __proc_pid_io_path_fmt, pid);
    MAKE_PROCESS_FULL_FILENAME(ps->fd_full_filename, __proc_pid_fd_path_fmt, pid);

    debug("[PROCESS] new_process_stat: pid: %d, cmd_line: '%s', hash: %u, stat_file: '%s', "
          "status_file: '%s', io_file: '%s', fd_file: '%s'",
          pid, ps->cmd_line, ps->hash, ps->stat_full_filename, ps->status_full_filename,
          ps->io_full_filename, ps->fd_full_filename);

    return ps;
}

void free_process_stat(struct process_stat *ps, struct xm_mempool_s *xmp) {
    if (likely(ps)) {
        if (likely(ps->pf_proc_pid_io)) {
            procfile_close(ps->pf_proc_pid_io);
        }

        if (likely(ps->pf_proc_pid_stat)) {
            procfile_close(ps->pf_proc_pid_stat);
        }

        if (likely(ps->stat_full_filename)) {
            free(ps->stat_full_filename);
        }

        if (likely(ps->status_full_filename)) {
            free(ps->status_full_filename);
        }

        if (likely(ps->io_full_filename)) {
            free(ps->io_full_filename);
        }

        if (likely(ps->fd_full_filename)) {
            free(ps->fd_full_filename);
        }
        debug("[PROCESS] free_process_stat: pid: %d, comm: %s", ps->pid, ps->comm);

        if (likely(xmp)) {
            xm_mempool_free(xmp, ps);
        } else {
            free(ps);
        }
        ps = NULL;
    }
}