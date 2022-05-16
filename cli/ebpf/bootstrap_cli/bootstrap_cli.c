/*
 * @Author: CALM.WU
 * @Date: 2022-05-16 14:53:52
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-05-16 15:54:08
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

static sig_atomic_t __sig_exit = 0;

static void __sig_handler(int sig) {
    __sig_exit = 1;
    warn(stderr, "SIGINT/SIGTERM received, exiting...\n");
}

static int32_t __handle_event(void *ctx, void *data, size_t data_sz) {
    return 0;
}

int32_t main(int32_t argc, char **argv) {
    int32_t                  ret = 0;
    struct xm_bootstrap_bpf *skel = NULL;
    struct ring_buffer      *rb = NULL;

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

    // open skel
    skel = xm_bootstrap_bpf__open();
    if (unlikely(!skel)) {
        fatal("failed to open xm_bootstrap_bpf\n");
        return -1;
    }

    // 设置 Read-only BPF configuration variables

    // Load & verify BPF programs
    ret = xm_bootstrap_bpf__load(skel);
    if (unlikely(0 != ret)) {
        error("failed to load xm_bootstrap_bpf: %s\n", strerror(errno));
        goto cleanup;
    }

    // attach tracepoints
    ret = xm_bootstrap_bpf__attach(skel);
    if (unlikely(0 != ret)) {
        error("failed to attach xm_bootstrap_bpf: %s\n", strerror(errno));
        goto cleanup;
    }

    // 设置bpf ring buffer polling
    rb = ring_buffer__new(bpf_map__fd(skel->maps.rb), __handle_event, NULL, NULL);
    if (unlikely(!rb)) {
        error("failed to create ring buffer\n");
        goto cleanup;
    }

    signal(SIGINT, __sig_handler);
    signal(SIGTERM, __sig_handler);

    debug("%-8s %-5s %-16s %-7s %-7s %s", "TIME", "EVENT", "COMM", "PID", "PPID",
          "FILENAME/EXIT CODE");

    while (!__sig_exit) {
        ret = ring_buffer__poll(rb, 100 /*timeout, ms*/);
        if (ret == -EINTR) {
            break;
        }
        if (ret < 0) {
            error("failed to poll ring buffer: %s\n", strerror(errno));
            break;
        }
    }

cleanup:
    if (rb) {
        ring_buffer__free(rb);
    }

    if (skel) {
        xm_bootstrap_bpf__destroy(skel);
    }

    debug("bootstrap say byebye!");

    return 0;
}