/*
 * @Author: CALM.WU
 * @Date: 2022-03-29 14:29:03
 * @Last Modified by:   CALM.WU
 * @Last Modified time: 2022-03-29 14:29:03
 */

#include "process_collector.h"

#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/strings.h"
#include "utils/os.h"

// ls  "/proc/$pid/fd"|wc -l

int32_t collector_process_fd_usage(struct process_collector *pc) {
    pc->process_open_fds = 0;

    DIR *fds = opendir(pc->fd_full_filename);
    if (unlikely(NULL == fds)) {
        error("[PROCESS:fds] open fds dir '%s' failed.", pc->fd_full_filename);
        return -1;
    }

    struct dirent *fd_entry = NULL;
    while ((fd_entry = readdir(fds))) {
        if (likely(isdigit(fd_entry->d_name[0]))) {
            pc->process_open_fds++;
        }
    }

    closedir(fds);
    fds = NULL;

    debug("[PROCESS:fds] process '%d' open fds: %d", pc->pid, pc->process_open_fds);
    return 0;
}