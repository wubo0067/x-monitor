/*
 * @Author: CALM.WU
 * @Date: 2022-05-16 14:53:52
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-06-28 14:22:43
 */

#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/log.h"
#include "utils/os.h"
#include "utils/consts.h"
#include "utils/x_ebpf.h"
#include "utils/mempool.h"

#include <bpf/libbpf.h>
#include "collectc/cc_hashtable.h"
#include "xm_bootstrap.skel.h"

#define TASK_COMM_LEN 16
#define MAX_FILENAME_LEN 128

struct bootstrap_ev {
    pid_t pid;
    pid_t ppid;
    uid_t uid;
    uint16_t exit_code;
    uint64_t start_ns;
    uint64_t duration_ns;
    char comm[TASK_COMM_LEN];
    char filename[MAX_FILENAME_LEN];
    bool exit_event;
};

static struct args {
    bool verbose;
} __env = {
    .verbose = true,
};

static sig_atomic_t __sig_exit = 0;

static CC_HashTable *__bs_ev_table = NULL;
struct xm_mempool_s *__bs_ev_xmp = NULL;

static int32_t __cmp_pid(const void *p1, const void *p2) {
    pid_t *pid1 = (pid_t *)p1;
    pid_t *pid2 = (pid_t *)p2;
    return !(*pid1 == *pid2);
}

static void __sig_handler(int UNUSED(sig)) {
    __sig_exit = 1;
    warn("SIGINT/SIGTERM received, exiting...\n");
}

static int32_t __handle_event(void *UNUSED(ctx), void *data,
                              size_t UNUSED(data_sz)) {
    struct bootstrap_ev *bs_ev = data, *ev = NULL;
    struct tm *tm;
    char ts[32];
    time_t t;

    time(&t);
    tm = localtime(&t);
    strftime(ts, sizeof(ts), "%H:%M:%S", tm);

    if (!bs_ev->exit_event) {
        // 新进程
        ev = (struct bootstrap_ev *)xm_mempool_malloc(__bs_ev_xmp);
        memcpy(ev, bs_ev, sizeof(struct bootstrap_ev));
        cc_hashtable_add(__bs_ev_table, &ev->pid, ev);
        debug("%-8s %-8s %-5s %-16s %-7d %-7d %-10s", ts, get_username(ev->uid),
              "EXEC", ev->comm, ev->pid, ev->ppid, ev->filename);
    } else {
        if (cc_hashtable_get(__bs_ev_table, &bs_ev->pid, (void *)&ev)
            == CC_OK) {
            debug("%-8s %-8s %-5s %-16s %-7d %-7d %-15s %-10u %lums", ts,
                  get_username(ev->uid), "EXIT", ev->comm, ev->pid, ev->ppid,
                  ev->filename, bs_ev->exit_code, bs_ev->duration_ns / 1000000);
            cc_hashtable_remove(__bs_ev_table, (void *)&bs_ev->pid, NULL);
            xm_mempool_free(__bs_ev_xmp, ev);
        }
    }

    return 0;
}

int32_t main(int32_t argc, char **argv) {
    int32_t ret = 0;
    struct xm_bootstrap_bpf *skel = NULL;
    struct ring_buffer *rb = NULL;

    if (log_init("../examples/log.cfg", "bootstrap_cli") != 0) {
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

    // 设置 bpf ring buffer polling
    rb = ring_buffer__new(bpf_map__fd(skel->maps.__bs_ev_rbmap), __handle_event,
                          NULL, NULL);
    if (unlikely(!rb)) {
        error("failed to create ring buffer\n");
        goto cleanup;
    }

    signal(SIGINT, __sig_handler);
    signal(SIGTERM, __sig_handler);

    __bs_ev_xmp = xm_mempool_init(sizeof(struct bootstrap_ev), 1024, 1024);

    // 初始化事件表
    CC_HashTableConf config;
    cc_hashtable_conf_init(&config);
    config.key_length = sizeof(pid_t);
    config.hash = GENERAL_HASH;
    config.key_compare = __cmp_pid;
    cc_hashtable_new_conf(&config, &__bs_ev_table);

    debug("%-8s %-8s %-5s %-16s %-7s %-7s %-15s %-10s %s", "TIME", "USER",
          "EVENT", "COMM", "PID", "PPID", "FILENAME", "EXIT_CODE",
          "DURATION_MS");

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

    cc_hashtable_destroy(__bs_ev_table);
    xm_mempool_fini(__bs_ev_xmp);

    debug("bootstrap say byebye!");

    log_fini();

    return 0;
}