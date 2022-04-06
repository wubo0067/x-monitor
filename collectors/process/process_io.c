/*
 * @Author: CALM.WU
 * @Date: 2022-03-29 14:29:00
 * @Last Modified by:   CALM.WU
 * @Last Modified time: 2022-03-29 14:29:00
 */

// Display the IO accounting fields
// https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/Documentation/filesystems/proc.rst?id=HEAD#l1305

#include "process_stat.h"

#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/strings.h"
#include "utils/os.h"

int32_t collector_process_io_usage(struct process_stat *stat) {
    stat->pf_proc_pid_io = procfile_readall(stat->pf_proc_pid_io);
    if (!unlikely(stat->pf_proc_pid_io)) {
        error("[PROCESS:io] read process stat '%s' failed.", stat->pf_proc_pid_stat->filename);
        return -1;
    }

    stat->io_logical_bytes_read = str2uint64_t(procfile_lineword(stat->pf_proc_pid_io, 0, 1));
    stat->io_logical_bytes_written = str2uint64_t(procfile_lineword(stat->pf_proc_pid_io, 1, 1));
    stat->io_read_calls = str2uint64_t(procfile_lineword(stat->pf_proc_pid_io, 2, 1));
    stat->io_write_calls = str2uint64_t(procfile_lineword(stat->pf_proc_pid_io, 3, 1));
    stat->io_storage_bytes_read = str2uint64_t(procfile_lineword(stat->pf_proc_pid_io, 4, 1));
    stat->io_storage_bytes_written = str2uint64_t(procfile_lineword(stat->pf_proc_pid_io, 5, 1));
    stat->io_cancelled_write_bytes = str2int32_t(procfile_lineword(stat->pf_proc_pid_io, 6, 1));

    debug("[PROCESS:io] read '%s' io_logical_bytes_read: %lu, io_logical_bytes_written: %lu, "
          "io_read_calls: %lu, io_write_calls: %lu, io_storage_bytes_read: %lu, "
          "io_storage_bytes_written: %lu, io_cancelled_write_bytes: %d",
          stat->pf_proc_pid_io->filename, stat->io_logical_bytes_read,
          stat->io_logical_bytes_written, stat->io_read_calls, stat->io_write_calls,
          stat->io_storage_bytes_read, stat->io_storage_bytes_written,
          stat->io_cancelled_write_bytes);

    return 0;
}