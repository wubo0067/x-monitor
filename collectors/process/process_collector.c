/*
 * @Author: CALM.WU
 * @Date: 2022-03-29 16:09:03
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-03-31 10:31:25
 */

#include "process_collector.h"

#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/os.h"
#include "utils/strings.h"
#include "utils/procfile.h"

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

struct process_collector *new_process_collector(pid_t pid) {
    if (unlikely(pid <= 0)) {
        error("[PROCESS] invalid pid: %d", pid);
        return NULL;
    }

    struct process_collector *pc = calloc(1, sizeof(struct process_collector));
    if (unlikely(NULL == pc)) {
        error("[PROCESS] calloc process_collector failed.");
        return NULL;
    }

    pc->pid = pid;
    // 读取cmd_line
    get_process_name(pid, pc->cmd_line, sizeof(pc->cmd_line));
    // 计算hash
    char hash_buffer[HASH_BUFFER_SIZE] = { 0 };
    snprintf(hash_buffer, HASH_BUFFER_SIZE - 1, "%d %s", pid, pc->comm);
    pc->hash = simple_hash(hash_buffer);

    MAKE_PROCESS_FULL_FILENAME(pc->stat_full_filename, __proc_pid_stat_path_fmt, pid);
    MAKE_PROCESS_FULL_FILENAME(pc->status_full_filename, __proc_pid_status_path_fmt, pid);
    MAKE_PROCESS_FULL_FILENAME(pc->io_full_filename, __proc_pid_io_path_fmt, pid);
    MAKE_PROCESS_FULL_FILENAME(pc->fd_full_filename, __proc_pid_fd_path_fmt, pid);

    debug("[PROCESS] new_process_collector: pid: %d, cmd_line: '%s', hash: %u, stat_file: '%s', "
          "status_file: '%s', io_file: '%s', fd_file: '%s'",
          pid, pc->cmd_line, pc->hash, pc->stat_full_filename, pc->status_full_filename,
          pc->io_full_filename, pc->fd_full_filename);

    return pc;
}

void free_process_collector(struct process_collector *pc) {
    if (likely(pc)) {
        if (likely(pc->pf_proc_pid_io)) {
            procfile_close(pc->pf_proc_pid_io);
        }

        if (likely(pc->pf_proc_pid_stat)) {
            procfile_close(pc->pf_proc_pid_stat);
        }

        if (likely(pc->stat_full_filename)) {
            free(pc->stat_full_filename);
        }

        if (likely(pc->status_full_filename)) {
            free(pc->status_full_filename);
        }

        if (likely(pc->io_full_filename)) {
            free(pc->io_full_filename);
        }

        if (likely(pc->fd_full_filename)) {
            free(pc->fd_full_filename);
        }
        debug("[PROCESS] free_process_collector: pid: %d, comm: %s", pc->pid, pc->comm);

        free(pc);
        pc = NULL;
    }
}