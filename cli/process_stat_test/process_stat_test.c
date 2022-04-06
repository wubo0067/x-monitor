/*
 * @Author: CALM.WU
 * @Date: 2022-03-31 10:24:17
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-03-31 10:32:43
 */

#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/log.h"
#include "utils/os.h"
#include "collectors/process/process_stat.h"

static int32_t       __sig_exit = 0;
static const int32_t __def_loop_count = 500000000;

static void __sig_handler(int sig) {
    __sig_exit = 1;
    debug("SIGINT/SIGTERM received, exiting...");
}

int32_t main(int32_t argc, char **argv) {
    (void)argc;
    (void)argv;

    if (log_init("../cli/process_stat_test/log.cfg", "process_stat_test") != 0) {
        fprintf(stderr, "log init failed\n");
        return -1;
    }

    get_system_cpus();
    get_system_hz();

    pid_t self = getpid();

    debug("process_stat_test self pid: %d", self);

    struct process_stat *ps = process_stat_new(self);
    if (ps == NULL) {
        error("process_stat_new() failed");
        return -1;
    }

    signal(SIGINT, __sig_handler);
    signal(SIGTERM, __sig_handler);

    while (!__sig_exit) {
        int32_t loop_count = __def_loop_count;

        while (--loop_count)
            ;

        collector_process_cpu_usage(ps);
        collector_process_mem_usage(ps);
        collector_process_io_usage(ps);
        collector_process_fds_usage(ps);
        sleep(3);
        debug(" ");
    }

    process_stat_free(ps);

    log_fini();

    return 0;
}