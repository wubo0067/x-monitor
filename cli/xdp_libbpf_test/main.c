/*
 * @Author: CALM.WU
 * @Date: 2022-02-11 10:36:28
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-02-15 15:22:34
 */
#include <argp.h>

#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/debug.h"
#include "utils/resource.h"
#include "utils/consts.h"
#include "utils/x_ebpf.h"

#include <bpf/libbpf.h>
#include <linux/if_link.h>
#include "xdp_pass.skel.h"

// bin/xdp_libbpf_test --itf=ens160 -v

enum xdp_action_type {
    XDP_ACTION_ATTACH = 0,
    XDP_ACTION_DETACH,
};

struct args {
    int32_t              itf_index;
    enum xdp_action_type action_type;
    uint32_t             xdp_flags;
    bool                 verbose;
} env = {
    .itf_index = -1,  // 所有的网卡
    .xdp_flags = XDP_FLAGS_UPDATE_IF_NOEXIST,
    .verbose   = true,
};

static uint32_t     __prog_id;
static sig_atomic_t __sig_exit = 0;
static const char * __optstr   = "i:sfv";

static const struct option __opts[] = {
    { "itf", required_argument, NULL, 'i' }, { "type", required_argument, NULL, 't' },
    { "skb", no_argument, NULL, 's' },       { "force_load", no_argument, NULL, 'f' },
    { "verbose", no_argument, NULL, 'v' },   { 0, 0, 0, 0 },
};

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [OPTS] --itf=IFNAME\n\n"
            "OPTS:\n"
            "    -s    use skb-mode\n"
            "    -f    force loading prog\n"
            "    -v    verbose\n",
            prog);
}

static void sig_handler(int sig) {
    __u32 curr_prog_id = 0;

    __sig_exit = 1;

    if (bpf_get_link_xdp_id(env.itf_index, &curr_prog_id, env.xdp_flags)) {
        debug("bpf_get_link_xdp_id failed\n");
        exit(1);
    }
    if (__prog_id == curr_prog_id)
        bpf_set_link_xdp_fd(env.itf_index, -1, env.xdp_flags);
    else if (!curr_prog_id)
        debug("couldn't find a prog id on a given interface\n");
    else
        debug("program on interface changed, not removing\n");

    debug("xdp detach");
    return;
}

static void poll_stats(int32_t map_fd, int32_t xdp_stats_map_fd, int32_t interval) {
    uint32_t nr_cpus = libbpf_num_possible_cpus();
    debug("nr_cpus: %u\n", nr_cpus);

    uint64_t values[nr_cpus], prev[UINT8_MAX] = { 0 };

    while (!__sig_exit) {
        int32_t lookup_key = -1, next_key;

        // 轮询map中所有元素
        while (bpf_map_get_next_key(map_fd, &lookup_key, &next_key) == 0) {
            if (__sig_exit)
                break;

            uint64_t sum = 0;
            // TODO: values是个core核数的数组，这是BPF_MAP_TYPE_PERCPU_ARRAY特性？
            assert(bpf_map_lookup_elem(map_fd, &next_key, values) == 0);
            for (uint32_t i = 0; i < nr_cpus; i++) {
                sum += values[i];
            }
            // debug("proto: %d, lookup_key: %d sum: %lu, prev_sum: %lu\n", next_key, lookup_key,
            // sum,
            //       prev[next_key]);
            if (sum > prev[next_key]) {
                debug("proto %d: sum rx_cnt: %lu, %lu pkt/s", next_key, sum,
                      (sum - prev[next_key]) / interval);
            }
            prev[next_key] = sum;

            lookup_key = next_key;
        }

        bpf_xdp_stats_print(xdp_stats_map_fd);

        sleep(interval);
    }
    return;
}

int32_t main(int32_t argc, char **argv) {
    int32_t ret = 0;
    int32_t opt;

    debugLevel = 9;
    debugFile  = fdopen(STDOUT_FILENO, "w");

    while ((opt = getopt_long(argc, argv, __optstr, __opts, NULL)) != -1) {
        switch (opt) {
        case 'v':
            env.verbose = true;
            break;
        case 'i':
            env.itf_index = if_nametoindex(optarg);
            if (unlikely(!env.itf_index)) {
                fprintf(stderr, "invalid interface name: %s, err: %s\n", optarg, strerror(errno));
                exit(EXIT_FAILURE);
            }
            debug("ift_index:%d, itf_name:%s", env.itf_index, optarg);
            break;
        case 's':
            env.xdp_flags |= XDP_FLAGS_SKB_MODE;
            debug("xdp flags set skb mode");
            break;
        case 'f':
            env.xdp_flags &= ~XDP_FLAGS_UPDATE_IF_NOEXIST;
            debug("xdp flags force load");
            break;
        case 't':
            if (!strcmp(optarg, "attach")) {
                env.action_type = XDP_ACTION_ATTACH;
            } else if (!strcmp(optarg, "detach")) {
                env.action_type = XDP_ACTION_DETACH;
            } else {
                fprintf(stderr, "invalid action type: %s\n", optarg);
                exit(EXIT_FAILURE);
            }
            break;
        default:
            usage(basename(argv[0]));
            return 1;
        }
    }

    if (!(env.xdp_flags & XDP_FLAGS_SKB_MODE)) {
        env.xdp_flags |= XDP_FLAGS_DRV_MODE;
    }

    // if (optind == argc) {
    //     debug("optind:%d, argc:%d", optind, argc);
    //     usage(basename(argv[0]));
    //     return 1;
    // }

    libbpf_set_strict_mode(LIBBPF_STRICT_ALL);
    // Libbpf 日志
    if (env.verbose) {
        libbpf_set_print(bpf_printf);
    }

    ret = bump_memlock_rlimit();
    if (ret) {
        fprintf(stderr, "failed to increase memlock rlimit: %s\n", strerror(errno));
        return -1;
    }

    // 打开
    struct xdp_pass_bpf *obj = xdp_pass_bpf__open();
    if (unlikely(!obj)) {
        fprintf(stderr, "failed to open and/or load BPF object\n");
        return -1;
    } else {
        debug("BPF object opened");
    }

    // 设置初始值
    memset(obj->rodata->target_name, 0, sizeof(obj->rodata->target_name));
    strcpy(obj->rodata->target_name, "xdp_libbpf");

    // 加载
    ret = xdp_pass_bpf__load(obj);
    if (unlikely(0 != ret)) {
        fprintf(stderr, "failed to load BPF object: %d\n", ret);
        goto cleanup;
    } else {
        debug("BPF object loaded");
    }

    // debug("xdp_prog_simple name: %s sec_name: %s", obj->progs.xdp_prog_simple->name,
    //       obj->progs.xdp_prog_simple->sec_name);

    int32_t map_fd = bpf_map__fd(obj->maps.ipproto_rx_cnt_map);
    debug("bpf map ipproto_rx_cnt_map fd:%d", map_fd);

    int32_t xdp_stats_map_fd = bpf_map__fd(obj->maps.xdp_stats_map);
    debug("bpf map xdp_stats_map fd:%d", xdp_stats_map_fd);

    int32_t prog_fd = bpf_program__fd(obj->progs.xdp_prog_simple);
    debug("bpf prog xdp_prog_simple fd:%d", map_fd);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // 附加 它是可选的，你可以通过直接使用 libbpf API 获得更多控制）；
    // ret = xdp_pass_bpf__attach(obj);
    // ret = bpf_xdp_attach(prog_fd, env.itf_index, __xdp_flags, NULL);
    // 使用bpf_set_link_xdp_fd执行attach成功，和bpf_xdp_attach差异在于old_prog_fd这个参数
    ret = bpf_set_link_xdp_fd(env.itf_index, prog_fd, env.xdp_flags);
    if (ret < 0) {
        fprintf(stderr, "link set xdp fd failed. ret: %d err: %s\n", ret, strerror(errno));
        goto cleanup;
    } else {
        debug("BPF programs attached");
    }

    struct bpf_prog_info info     = {};
    uint32_t             info_len = sizeof(info);

    ret = bpf_obj_get_info_by_fd(prog_fd, &info, &info_len);
    if (ret) {
        fprintf(stderr, "can't get prog info: %s\n", strerror(errno));
    }
    __prog_id = info.id;
    debug("prog id: %d", __prog_id);

    poll_stats(map_fd, 2);

    // bpf_xdp_detach(env.itf_index, __xdp_flags, NULL);

cleanup:
    xdp_pass_bpf__destroy(obj);

    debug("%s exit", argv[0]);
    debugClose();
    return 0;
}