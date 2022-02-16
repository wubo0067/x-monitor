/*
 * @Author: CALM.WU
 * @Date: 2021-11-03 11:37:51
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-02-15 17:40:27
 */

#include "x_ebpf.h"
#include "common.h"
#include "log.h"
#include "compiler.h"

#include <linux/bpf.h>

#include "../collectors/ebpf/common_bpf_user.h"

#define DEBUGFS "/sys/kernel/debug/tracing/"
#define MAX_SYMS 300000
static struct ksym syms[MAX_SYMS];
static int32_t     sym_cnt;
static const char *__ksym_empty_name = "";

static const char *__xdp_action_names[XDP_ACTION_MAX] = {

    [XDP_ABORTED] = "XDP_ABORTED", [XDP_DROP] = "XDP_DROP",         [XDP_PASS] = "XDP_PASS",
    [XDP_TX] = "XDP_TX",           [XDP_REDIRECT] = "XDP_REDIRECT", [XDP_UNKNOWN] = "XDP_UNKNOWN",
};

int32_t bpf_printf(enum libbpf_print_level level, const char *fmt, va_list args) {
    // if ( level == LIBBPF_DEBUG && !g_env.verbose ) {
    // 	return 0;
    // }
    char out_fmt[128] = { 0 };
    sprintf(out_fmt, "level:{%d} %s", level, fmt);
    // vfprintf适合参数可变列表传递
    return vfprintf(stderr, out_fmt, args);
}

static int32_t ksym_cmp(const void *p1, const void *p2) {
    return ((struct ksym *)p1)->addr - ((struct ksym *)p2)->addr;
}

int32_t load_kallsyms() {
    FILE   *f = fopen("/proc/kallsyms", "r");
    char    func[256], buf[256];
    char    symbol;
    void   *addr;
    int32_t i = 0;

    if (!f)
        return -ENOENT;

    while (fgets(buf, sizeof(buf), f)) {
        if (sscanf(buf, "%p %c %s", &addr, &symbol, func) != 3)
            break;
        if (!addr)
            continue;
        syms[i].addr = (long)addr;
        syms[i].name = strdup(func);
        i++;
    }
    fclose(f);
    sym_cnt = i;
    qsort(syms, sym_cnt, sizeof(struct ksym), ksym_cmp);
    return 0;
}

struct ksym *ksym_search(long key) {
    int32_t start = 0, end = sym_cnt;
    int32_t result;

    /* kallsyms not loaded. return NULL */
    if (sym_cnt <= 0)
        return NULL;

    while (start < end) {
        size_t mid = start + (end - start) / 2;

        result = key - syms[mid].addr;
        if (result < 0)
            end = mid;
        else if (result > 0)
            start = mid + 1;
        else
            return &syms[mid];
    }

    if (start >= 1 && syms[start - 1].addr < key && key < syms[start].addr)
        /* valid ksym */
        return &syms[start - 1];

    /* out of range. return _stext */
    return &syms[0];
}

long ksym_get_addr(const char *name) {
    int32_t i;

    for (i = 0; i < sym_cnt; i++) {
        if (strcmp(syms[i].name, name) == 0)
            return syms[i].addr;
    }

    return 0;
}

/* open kallsyms and find addresses on the fly, faster than load + search. */
extern int32_t kallsyms_find(const char *sym, unsigned long long *addr) {
    char               type, name[500];
    unsigned long long value;
    int                err = 0;
    FILE              *f;

    f = fopen("/proc/kallsyms", "r");
    if (!f)
        return -EINVAL;

    while (fscanf(f, "%llx %c %499s%*[^\n]\n", &value, &type, name) > 0) {
        if (strcmp(name, sym) == 0) {
            *addr = value;
            goto out;
        }
    }
    err = -ENOENT;

out:
    fclose(f);
    return err;
}

const char *bpf_get_ksym_name(uint64_t addr) {
    struct ksym *sym;

    if (addr == 0)
        return __ksym_empty_name;

    sym = ksym_search(addr);
    if (!sym)
        return __ksym_empty_name;

    return sym->name;
}

int32_t open_raw_sock(const char *iface) {
    struct sockaddr_ll sll;
    int32_t            sock;

    sock = socket(PF_PACKET, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, htons(ETH_P_ALL));
    if (sock < 0) {
        error("socket() create raw socket failed: %s\n", strerror(errno));
        return -errno;
    }

    memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = if_nametoindex(iface);
    sll.sll_protocol = htons(ETH_P_ALL);
    if (bind(sock, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
        error("bind() to '%s' failed: %s\n", iface, strerror(errno));
        close(sock);
        return -errno;
    }
    return sock;
}

const char *bpf_xdpaction2str(uint32_t action) {
    if (action < XDP_ACTION_MAX)
        return __xdp_action_names[action];
    return NULL;
}

int32_t bpf_get_bpf_map_info(int32_t map_fd, struct bpf_map_info *info, int32_t verbose) {
    uint32_t info_len = (uint32_t)sizeof(*info);

    if (unlikely(map_fd < 0)) {
        error("invalid map fd\n");
        return -1;
    }

    int32_t ret = bpf_obj_get_info_by_fd(map_fd, info, &info_len);
    if (unlikely(0 != ret)) {
        error("bpf_obj_get_info_by_fd() failed: %s\n", strerror(errno));
        return -1;
    }

    if (verbose) {
        debug(
            " - BPF map (bpf_map_type:%d) id:%d name:%s"
            " key_size:%d value_size:%d max_entries:%d\n",
            info->type, info->id, info->name, info->key_size, info->value_size, info->max_entries);
    }
    return 0;
}

static void xdp_stats_map_get_value_array(
    int32_t xdp_stats_map_fd, uint32_t key, struct xdp_stats_datarec *value) {
    if (unlikely(bpf_map_lookup_elem(xdp_stats_map_fd, &key, value))) {
        error("ERR: bpf_map_lookup_elem failed key:0x%X\n", key);
    }
}

static void xdp_stats_map_get_value_percpu_array(
    int32_t xdp_stats_map_fd, uint32_t key, struct xdp_stats_datarec *value) {
    int32_t nr_cpus = libbpf_num_possible_cpus();
    int32_t i;

    struct xdp_stats_datarec values[nr_cpus];

    uint64_t sum_bytes = 0;
    uint64_t sum_pkts = 0;

    if (unlikely(bpf_map_lookup_elem(xdp_stats_map_fd, &key, values)) != 0) {
        error("ERR: bpf_map_lookup_elem failed key:0x%X\n", key);
        return;
    }

    // 要汇总所有cpu的值
    for (i = 0; i < nr_cpus; i++) {
        sum_bytes += values[i].rx_bytes;
        sum_pkts += values[i].rx_packets;
    }
    value->rx_bytes = sum_bytes;
    value->rx_packets = sum_pkts;
}

void bpf_xdp_stats_print(int32_t xdp_stats_map_fd) {
    struct bpf_map_info info = { 0 };
    if (unlikely(0 != bpf_get_bpf_map_info(xdp_stats_map_fd, &info, 1))) {
        error("ERR: map via FD not compatible\\n");
        return;
    }

    uint32_t key;

    struct xdp_stats_datarec records[XDP_ACTION_MAX];

    for (key = 0; key < XDP_ACTION_MAX; key++) {
        switch (info.type) {
        case BPF_MAP_TYPE_PERCPU_ARRAY:
            xdp_stats_map_get_value_percpu_array(xdp_stats_map_fd, key, &records[key]);
            break;
        case BPF_MAP_TYPE_ARRAY:
            xdp_stats_map_get_value_array(xdp_stats_map_fd, key, &records[key]);
            break;
        default: error("ERR: map type %d not supported\\n", info.type); return;
        }
    }

    for (key = 0; key < XDP_ACTION_MAX; key++) {
        if (records[key].rx_bytes == 0 && records[key].rx_packets == 0)
            continue;
        debug(
            "%s: %lu packets, %lu bytes\n", __xdp_action_names[key],
            (uint64_t)records[key].rx_packets, (uint64_t)records[key].rx_bytes);
    }

    return;
}