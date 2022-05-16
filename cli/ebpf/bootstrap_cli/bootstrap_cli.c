/*
 * @Author: CALM.WU
 * @Date: 2022-05-16 14:53:52
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-05-16 15:33:42
 */

#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/log.h"
#include "utils/os.h"
#include "utils/consts.h"
#include "utils/x_ebpf.h"

#include <bpf/libbpf.h>
#include "xm_bootstrap.skel.h"

static struct args {
    bool verbose;
} __env = {
    .verbose = true,
};

int32_t main(int32_t argc, char **argv) {
    int32_t ret = 0;

    if (log_init("../cli/log.cfg", "bootstrap_cli") != 0) {
        fprintf(stderr, "log init failed\n");
        return -1;
    }

    debug("bootstrap say hi!");

    libbpf_set_strict_mode(LIBBPF_STRICT_ALL);
    if (__env.verbose) {
        libbpf_set_print(xm_bpf_printf);
    }

    ret = bump_memlock_rlimit();
    if (ret) {
        fatal("failed to increase memlock rlimit: %s\n", strerror(errno));
        return -1;
    }

    debug("bootstrap say byebye!");

    return 0;
}
