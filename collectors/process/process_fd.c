/*
 * @Author: CALM.WU
 * @Date: 2022-03-29 14:29:03
 * @Last Modified by:   CALM.WU
 * @Last Modified time: 2022-03-29 14:29:03
 */

#include "process_stat.h"

#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/strings.h"
#include "utils/os.h"

int32_t collector_process_fds_usage(struct process_stat *stat) {
    int32_t fd;

    stat->process_open_fds = 0;

    DIR *fds = opendir(stat->fds_dirname);
    if (unlikely(NULL == fds)) {
        error("[PROCESS:fds] open fds dir '%s' failed.", stat->fds_dirname);
        return -1;
    }

    struct dirent *fd_entry = NULL;
    while ((fd_entry = readdir(fds))) {
        if (!strcmp(fd_entry->d_name, "..") || !strcmp(fd_entry->d_name, "."))
            continue;

        if (unlikely(fd_entry->d_name[0] < '0' || fd_entry->d_name[0] > '9')) {
            continue;
        }

        fd = str2int32_t(fd_entry->d_name);
        if (unlikely(fd < 0)) {
            continue;
        }

        stat->process_open_fds++;
        debug("[PROCESS:fds] open fd: %d, d_name: '%s'", fd, fd_entry->d_name);
    }

    closedir(fds);
    fds = NULL;

    debug("[PROCESS:fds] read '%s' open fds: %d", stat->fds_dirname, stat->process_open_fds);
    return 0;
}