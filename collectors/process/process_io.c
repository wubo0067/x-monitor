/*
 * @Author: CALM.WU
 * @Date: 2022-03-29 14:29:00
 * @Last Modified by:   CALM.WU
 * @Last Modified time: 2022-03-29 14:29:00
 */

// Display the IO accounting fields
// https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/Documentation/filesystems/proc.rst?id=HEAD#l1305

#include "process_collector.h"

#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/strings.h"
#include "utils/os.h"

int32_t collector_process_io_usage(struct process_collector *pc) {
    pc->pf_proc_pid_io = procfile_reopen(pc->pf_proc_pid_io, pc->io_full_filename, NULL,
                                         PROCFILE_FLAG_NO_ERROR_ON_FILE_IO);
    if (unlikely(NULL == pc->pf_proc_pid_io)) {
        error("[PROCESS] procfile_reopen '%s' failed.", pc->io_full_filename);
        return -1;
    }

    pc->pf_proc_pid_io = procfile_readall(pc->pf_proc_pid_io);
    if (!unlikely(pc->pf_proc_pid_io)) {
        error("[PROCESS:io] read process stat '%s' failed.", pc->pf_proc_pid_io->filename);
        return -1;
    }

    pc->io_logical_bytes_read = str2uint64_t(procfile_lineword(pc->pf_proc_pid_io, 0, 1));
    pc->io_logical_bytes_written = str2uint64_t(procfile_lineword(pc->pf_proc_pid_io, 1, 1));
    pc->io_read_calls = str2uint64_t(procfile_lineword(pc->pf_proc_pid_io, 2, 1));
    pc->io_write_calls = str2uint64_t(procfile_lineword(pc->pf_proc_pid_io, 3, 1));
    pc->io_storage_bytes_read = str2uint64_t(procfile_lineword(pc->pf_proc_pid_io, 4, 1));
    pc->io_storage_bytes_written = str2uint64_t(procfile_lineword(pc->pf_proc_pid_io, 5, 1));
    pc->io_cancelled_write_bytes = str2int32_t(procfile_lineword(pc->pf_proc_pid_io, 6, 1));

    debug("[PROCESS:io] process '%d' io_logical_bytes_read: %lu, io_logical_bytes_written: %lu, "
          "io_read_calls: %lu, io_write_calls: %lu, io_storage_bytes_read: %lu, "
          "io_storage_bytes_written: %lu, io_cancelled_write_bytes: %d",
          pc->pid, pc->io_logical_bytes_read, pc->io_logical_bytes_written, pc->io_read_calls,
          pc->io_write_calls, pc->io_storage_bytes_read, pc->io_storage_bytes_written,
          pc->io_cancelled_write_bytes);

    return 0;
}