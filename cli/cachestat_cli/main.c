/*
 * @Author: CALM.WU
 * @Date: 2021-11-03 11:23:12
 * @Last Modified by: calmwu
 * @Last Modified time: 2022-02-19 23:02:11
 */

#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/log.h"
#include "utils/os.h"
#include "utils/consts.h"
#include "utils/x_ebpf.h"

#include <bpf/libbpf.h>
#include "xm_cachestat.skel.h"

#define CLEAR() printf("\e[1;1H\e[2J")

struct cachestat_value {
    uint64_t add_to_page_cache_lru;
    uint64_t ip_add_to_page_cache;   // IP寄存器的值
    uint64_t mark_page_accessed;
    uint64_t ip_mark_page_accessed;   // IP寄存器的值
    uint64_t account_page_dirtied;
    uint64_t ip_account_page_dirtied;   // IP寄存器的值
    uint64_t mark_buffer_dirty;
    uint64_t ip_mark_buffer_dirty;   // IP寄存器的值
    uint32_t uid;                    // 用户ID
    char     comm[16];               // 进程名
};

static sig_atomic_t __sig_exit = 0;
static const float  __epsilon = 1e-6;
static const char  *__optstr = "v";

static struct args {
    bool verbose;
} __env = {
    .verbose = true,
};

static const struct option __opts[] = {
    { "verbose", no_argument, NULL, 'v' },
    { 0, 0, 0, 0 },
};

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [OPTS]\n\n"
            "OPTS:\n"
            "    -v    verbose\n",
            prog);
}

static void sig_handler(int sig) {
    __sig_exit = 1;
    fprintf(stderr, "SIGINT/SIGTERM received, exiting...\n");
}

int32_t main(int32_t argc, char **argv) {
    int32_t map_fd, ret, result = 0;
    int32_t opt;
    // const char *section;
    //  char        symbol[256];
    time_t     t;
    struct tm *tm;
    char       ts[32];

    while ((opt = getopt_long(argc, argv, __optstr, __opts, NULL)) != -1) {
        switch (opt) {
        case 'v':
            __env.verbose = true;
            break;
        default:
            usage(basename(argv[0]));
            return 1;
        }
    }

    if (log_init("../cli/cachestat_cli/log.cfg", "cachestat_cli") != 0) {
        fprintf(stderr, "log init failed\n");
        return -1;
    }

    if (xm_load_kallsyms()) {
        fprintf(stderr, "failed to process /proc/kallsyms\n");
        return -1;
    }

    libbpf_set_strict_mode(LIBBPF_STRICT_ALL);
    if (__env.verbose) {
        libbpf_set_print(xm_bpf_printf);
    }

    ret = bump_memlock_rlimit();
    if (ret) {
        fprintf(stderr, "failed to increase memlock rlimit: %s\n", strerror(errno));
        return -1;
    }

    // 打开
    struct xm_cachestat_bpf *obj = xm_cachestat_bpf__open();
    if (unlikely(!obj)) {
        fprintf(stderr, "failed to open xm_cachestat_bpf\n");
        return -1;
    } else {
        debug("xm_cachestat_bpf open success\n");
    }

    // 加载
    ret = xm_cachestat_bpf__load(obj);
    if (unlikely(0 != ret)) {
        fprintf(stderr, "failed to load xm_cachestat_bpf: %s\n", strerror(errno));
        return -1;
    } else {
        debug("xm_cachestat_bpf load success\n");
    }

    // find map
    // map_fd = bpf_object__find_map_fd_by_name(obj, "cachestat_map");
    map_fd = bpf_map__fd(obj->maps.cachestat_map);
    if (map_fd < 0) {
        fprintf(stderr, "ERROR: finding a map 'cachestat_map' in obj file failed\n");
        goto cleanup;
    }

    // 负载
    // ret = cachestat_bpf__attach(obj);
    // if (unlikely(0 != ret)) {
    //     fprintf(stderr, "failed to attach cachestat_bpf: %s\n", strerror(errno));
    //     goto cleanup;
    // } else {
    //     debug("cachestat_bpf attach success\n");
    // }

    obj->links.xmonitor_bpf_add_to_page_cache_lru =
        bpf_program__attach(obj->progs.xmonitor_bpf_add_to_page_cache_lru);
    if (unlikely(!obj->links.xmonitor_bpf_add_to_page_cache_lru)) {
        ret = -errno;
        fprintf(stderr, "failed to attach xmonitor_bpf_add_to_page_cache_lru: %s\n",
                strerror(-ret));
        goto cleanup;
    }

    obj->links.xmonitor_bpf_mark_page_accessed =
        bpf_program__attach(obj->progs.xmonitor_bpf_mark_page_accessed);
    if (unlikely(!obj->links.xmonitor_bpf_mark_page_accessed)) {
        ret = -errno;
        fprintf(stderr, "failed to attach xmonitor_bpf_mark_page_accessed: %s\n", strerror(-ret));
        goto cleanup;
    }

    obj->links.xmonitor_bpf_account_page_dirtied =
        bpf_program__attach(obj->progs.xmonitor_bpf_account_page_dirtied);
    if (unlikely(!obj->links.xmonitor_bpf_account_page_dirtied)) {
        ret = -errno;
        fprintf(stderr, "failed to attach xmonitor_bpf_account_page_dirtied: %s\n", strerror(-ret));
        goto cleanup;
    }

    obj->links.xmonitor_bpf_mark_buffer_dirty =
        bpf_program__attach(obj->progs.xmonitor_bpf_mark_buffer_dirty);
    if (unlikely(!obj->links.xmonitor_bpf_mark_buffer_dirty)) {
        ret = -errno;
        fprintf(stderr, "failed to attach xmonitor_bpf_mark_buffer_dirty: %s\n", strerror(-ret));
        goto cleanup;
    }

    // bpf_object__for_each_program(prog, obj) {
    //     section = bpf_program__section_name(prog);
    //     fprintf(stdout, "[%d] section: %s\n", j, section);

    //     if (sscanf(section, "kprobe/%s", symbol) != 1) {
    //         links[j] = bpf_program__attach(prog);
    //     } else {
    //         /* Attach prog only when symbol exists */
    //         if (xm_ksym_get_addr(symbol)) {
    //             // prog->log_level = 1;
    //             links[j] = bpf_program__attach(prog);
    //         } else {
    //             continue;
    //         }
    //     }

    //     if (libbpf_get_error(links[j])) {
    //         fprintf(stderr, "[%d] section: %s bpf_program__attach failed\n", j, section);
    //         links[j] = NULL;
    //         goto cleanup;
    //     }
    //     fprintf(stderr, "[%d] section: %s bpf program attach successed\n", j, section);
    //     j++;
    // }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    while (!__sig_exit) {
        // key初始为无效的键值，这迫使bpf_map_get_next_key从头开始查找
        int32_t                pid = -1, next_pid;
        struct cachestat_value value;

        CLEAR();

        fprintf(stdout, "\n%-9s %-16s %-6s %-8s %-20s %-20s %-20s %-20s %-15s %-15s %-15s %-15s\n",
                "TIME", "PCOMM", "PID", "UID", "add_to_page_cache_lru", "mark_page_accessed",
                "account_page_dirtied", "mark_buffer_dirty", "hits", "misses", "read_hit",
                "write_hit");

        while (bpf_map_get_next_key(map_fd, &pid, &next_pid) == 0) {
            if (__sig_exit) {
                fprintf(stdout, "--------------\n");
                break;
            }

            result = bpf_map_lookup_elem(map_fd, &next_pid, &value);
            if (0 == result) {
                time(&t);
                tm = localtime(&t);
                strftime(ts, sizeof(ts), "%H:%M:%S", tm);

                uint64_t mpa = value.mark_page_accessed;
                uint64_t mbd = value.mark_buffer_dirty;
                uint64_t apcl = value.add_to_page_cache_lru;
                uint64_t apd = value.account_page_dirtied;

                uint64_t access = mpa + mbd;
                uint64_t misses = apcl + apd;

                float rtaccess = 0.0;
                float wtaccess = 0.0;
                float whits = 0.0;
                float rhits = 0.0;

                if (mpa > 0) {
                    rtaccess = (float)mpa / (float)(access + misses);
                }

                if (apcl > 0) {
                    wtaccess = (float)apcl / (float)(access + misses);
                }

                if (fabs(wtaccess) > __epsilon) {
                    whits = 100 * wtaccess;
                }

                if (fabs(rtaccess) > __epsilon) {
                    rhits = 100 * rtaccess;
                }

                fprintf(stdout,
                        "%-9s %-16s %-6d %-8s %-20lu %-20lu %-20lu %-20lu %-15lu %-15lu %15f%% "
                        "%15f%% \n",
                        ts, value.comm, next_pid, get_username(value.uid),
                        value.add_to_page_cache_lru, value.mark_page_accessed,
                        value.account_page_dirtied, value.mark_buffer_dirty, access, misses, rhits,
                        whits);
            } else {
                fprintf(stderr, "ERROR: bpf_map_lookup_elem fail to get entry value of Key: '%d'\n",
                        next_pid);
            }

            pid = next_pid;
        }
        sleep(1);
    }

    fprintf(stdout, "kprobing funcs exit\n");

cleanup:
    xm_cachestat_bpf__destroy(obj);

    debug("cachestat_cli exit\n");
    return ret;
}