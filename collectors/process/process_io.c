/*
 * @Author: CALM.WU
 * @Date: 2022-03-29 14:29:00
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-04-13 16:43:40
 */

// Display the IO accounting fields
// https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/Documentation/filesystems/proc.rst?id=HEAD#l1305

#include "process_status.h"

#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/strings.h"
#include "utils/os.h"

int32_t collector_process_io_usage(struct process_status *ps) {
    ps->pf_proc_pid_io = procfile_reopen(ps->pf_proc_pid_io, ps->io_full_filename, NULL,
                                         PROCFILE_FLAG_NO_ERROR_ON_FILE_IO);
    if (unlikely(NULL == ps->pf_proc_pid_io)) {
        error("[PROCESS] procfile_reopen '%s' failed.", ps->io_full_filename);
        return -1;
    }

    ps->pf_proc_pid_io = procfile_readall(ps->pf_proc_pid_io);
    if (!unlikely(ps->pf_proc_pid_io)) {
        error("[PROCESS:io] read process stat '%s' failed.", ps->pf_proc_pid_io->filename);
        return -1;
    }

    ps->io_logical_bytes_read = str2uint64_t(procfile_lineword(ps->pf_proc_pid_io, 0, 1));
    ps->io_logical_bytes_written = str2uint64_t(procfile_lineword(ps->pf_proc_pid_io, 1, 1));
    ps->io_read_calls = str2uint64_t(procfile_lineword(ps->pf_proc_pid_io, 2, 1));
    ps->io_write_calls = str2uint64_t(procfile_lineword(ps->pf_proc_pid_io, 3, 1));
    ps->io_storage_bytes_read = str2uint64_t(procfile_lineword(ps->pf_proc_pid_io, 4, 1));
    ps->io_storage_bytes_written = str2uint64_t(procfile_lineword(ps->pf_proc_pid_io, 5, 1));
    ps->io_cancelled_write_bytes = str2int32_t(procfile_lineword(ps->pf_proc_pid_io, 6, 1));

    debug("[PROCESS:io] process '%d' io_logical_bytes_read: %lu, io_logical_bytes_written: %lu, "
          "io_read_calls: %lu, io_write_calls: %lu, io_storage_bytes_read: %lu, "
          "io_storage_bytes_written: %lu, io_cancelled_write_bytes: %d",
          ps->pid, ps->io_logical_bytes_read, ps->io_logical_bytes_written, ps->io_read_calls,
          ps->io_write_calls, ps->io_storage_bytes_read, ps->io_storage_bytes_written,
          ps->io_cancelled_write_bytes);

    return 0;
}