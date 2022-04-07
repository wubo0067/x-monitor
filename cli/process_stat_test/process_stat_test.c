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
#include "utils/strings.h"
#include "collectors/process/process_collector.h"

static int32_t       __sig_exit = 0;
static const int32_t __def_loop_count = 500000000;

static void __sig_handler(int sig) {
    __sig_exit = 1;
    debug("SIGINT/SIGTERM received, exiting...");
}

int32_t main(int32_t argc, char **argv) {
    if (log_init("../cli/process_stat_test/log.cfg", "process_stat_test") != 0) {
        fprintf(stderr, "log init failed\n");
        return -1;
    }

    if (unlikely(argc != 2)) {
        fatal("./process_stat_test <pid>\n");
        return -1;
    }

    pid_t pid = str2int32_t(argv[1]);
    if (unlikely(0 == pid)) {
        fatal("./process_stat_test <pid>\n");
        return -1;
    }

    get_system_cpus();
    get_system_hz();

    // pid_t self = getpid();

    debug("process_stat_test pid: %d", pid);

    struct process_collector *pc = new_process_collector(pid);
    if (pc == NULL) {
        error("new_process_collector() failed");
        return -1;
    }

    signal(SIGINT, __sig_handler);
    signal(SIGTERM, __sig_handler);

    int32_t ret = 0;

    while (!__sig_exit) {
        int32_t loop_count = __def_loop_count;

        COLLECTOR_PROCESS_USAGE(pc, ret);

        if (unlikely(0 != ret)) {
            debug("process '%d' exit", pid);
            break;
        }
        sleep(3);
        debug(" ");
    }

    debug("ret: %d", ret);

    free_process_collector(pc);

    log_fini();

    return 0;
}