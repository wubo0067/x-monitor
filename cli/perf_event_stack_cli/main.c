/*
 * @Author: CALM.WU 
 * @Date: 2021-11-12 10:13:05 
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-12-16 14:12:04
 */
#include <linux/perf_event.h>

#include <bcc/bcc_syms.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <collectc/cc_hashtable.h>
#ifdef __cplusplus
}
#endif

#include "utils/common.h"
#include "utils/x_ebpf.h"
#include "utils/resource.h"
#include "utils/debug.h"
#include "utils/strings.h"

#define SAMPLE_FREQ 100 // 100hz
#define MAX_CPU_NR 128
#define TASK_COMM_LEN 16
#define FILTERPIDS_BUF_SIZE 128
#define EBPF_KERN_OBJ_BUF_SIZE 256
#define FILTERPIDS_MAX_COUNT 8

enum ctrl_filter_key {
    CTRL_FILTER,
    CTRL_FILTER_END,
};

#define __FILTER_CONTENT_LEN 128

struct ctrl_filter_value {
    uint32_t filter_pid;
    char     filter_content[__FILTER_CONTENT_LEN];
};

struct process_stack_key {
    uint32_t kern_stackid;
    uint32_t user_stackid;
    uint32_t pid;
};

struct process_stack_value {
    char     comm[TASK_COMM_LEN];
    uint32_t count;
};

enum output_type {
    OUTPUT_STDOUT_ALL,
    OUTPUT_STDOUT_FILTER,
    OUTPUT_FILE_ALL,
    OUTPUT_FILE_FILTER,
    OUTPUT_TYPE_END,
};

static const char *const __default_kern_obj = "perf_event_stack_kern.o";
static sig_atomic_t      __sig_exit         = 0;
static int32_t           __bpf_map_fd[3];
static uint64_t          __filter_pids[FILTERPIDS_MAX_COUNT];
static CC_HashTableConf  __bcc_symcache_tab_conf;
static CC_HashTable     *__bcc_symcache_tab    = NULL;
static const char       *__default_output_file = "perf_event_stack_cli.out";
static enum output_type  __output_type         = OUTPUT_STDOUT_ALL;

//getopt_long 返回 int ，而不是 char 。如果 flag(第三个)字段是 NULL(或等效的 0)，那么 val(第四个)字段将被返回
static struct option long_options[] = {
    { "kern", required_argument, NULL, 'k' },
    { "pids", required_argument, NULL, 'p' },
    { "duration", required_argument, NULL, 'd' },
    { "output", required_argument, NULL, 'o' },
    { "help", no_argument, NULL, 'h' },
    { 0, 0, 0, 0 }
};

static void __sig_handler(int sig)
{
    __sig_exit = 1;
    debug("SIGINT/SIGTERM received, exiting...");
}

static int32_t bcc_symcache_tab_key_compare(const void *key1, const void *key2)
{
    uint64_t k1 = *(uint64_t *)key1;
    uint64_t k2 = *(uint64_t *)key2;

    //debug("key1: %llu, key2: %llu", k1, k2);
    if (k1 != k2) {
        return 1;
    }
    return 0;
}

static int32_t open_and_attach_perf_event(struct bpf_program *prog,
                                          int32_t             nr_cpus,
                                          struct bpf_link   **perf_event_links)
{
    int32_t pmu_fd = -1;

    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.sample_freq =
        SAMPLE_FREQ; // 采样频率，“采样”计数器是每N个事件生成一个中断的计数器，其中N由sample_period给出
    attr.freq    = 1; //
    attr.type    = PERF_TYPE_SOFTWARE;
    attr.config  = PERF_COUNT_SW_CPU_CLOCK;
    attr.inherit = 0;

    for (int32_t i = 0; i < nr_cpus; i++) {
        // 这将测量指定CPU上的所有进程/线程
        // -1表示所有进程/线程
        pmu_fd = sys_perf_event_open(&attr, -1, i, -1, 0);
        if (pmu_fd < 0) {
            fprintf(stderr, "sys_perf_event_open failed. %s\n",
                    strerror(errno));
            return -1;
        } else {
            debug("on cpu: %d sys_perf_event_open successed, pmu_fd: %d", i,
                  pmu_fd);
        }

        perf_event_links[i] = bpf_program__attach_perf_event(prog, pmu_fd);
        if (libbpf_get_error(perf_event_links[i])) {
            fprintf(stderr, "failed to attach perf event on cpu: %d\n", i);
            close(pmu_fd);
            perf_event_links[i] = NULL;
            return -1;
        } else {
            debug("prog: '%s' attach perf event on cpu: %d successed",
                  bpf_program__name(prog), i);
        }
    }

    return 0;
}

static void print_stack(struct process_stack_key   *key,
                        struct process_stack_value *value)
{
    uint64_t          ip[PERF_MAX_STACK_DEPTH] = {}, hkey;
    int32_t           i                        = 0;
    struct bcc_symbol sym;
    enum cc_stat      stat = CC_OK;

    hkey = key->pid;

    if (__output_type == OUTPUT_STDOUT_FILTER ||
        __output_type == OUTPUT_FILE_FILTER) {
        if (!cc_hashtable_contains_key(__bcc_symcache_tab, &hkey)) {
            //debug("pid: %d not in filter pids", key->pid);
            return;
        }
    }

    debug("\n");
    debug("<<pid:%d, comm:'%s', count:%d>>", key->pid, value->comm,
          value->count);

    // 通过key得到内核堆栈id、用户堆栈id，以此从堆栈map中获取堆栈信息
    // 获取每层的内核堆栈
    debug("\t<<%20s id: %u>>", "kernel stack", key->kern_stackid);
    if (bpf_map_lookup_elem(__bpf_map_fd[1], &key->kern_stackid, ip) != 0) {
        // 没有内核堆栈
        debug("\t---");
    } else {
        for (i = PERF_MAX_STACK_DEPTH - 1; i >= 0; i--) {
            if (ip[i] == 0) {
                continue;
            }
            debug("\t0x%16lx\t%-20s", ip[i], bpf_get_ksym_name(ip[i]));
        }
    }
    debug("\t<<%20s id: %u>>", "user stack", key->user_stackid);
    if (bpf_map_lookup_elem(__bpf_map_fd[1], &key->user_stackid, ip) != 0) {
        // 没有用户堆栈
        debug("\t", "---");
    } else {
        for (i = PERF_MAX_STACK_DEPTH - 1; i >= 0; i--) {
            if (ip[i] == 0) {
                continue;
            }

            // 根据pid查询bcc_symbol
            void *bcc_symcache = NULL;
            stat = cc_hashtable_get(__bcc_symcache_tab, &hkey, &bcc_symcache);
            if (stat != CC_OK) {
                debug("\t0x%016lx", ip[i]);
            } else {
                if (bcc_symcache) {
                    memset(&sym, 0, sizeof(sym));
                    if (0 == bcc_symcache_resolve(bcc_symcache, ip[i], &sym)) {
                        debug("\t0x%016lx\t%-10s\t%-20s\t0x%-016lx", ip[i],
                              sym.name, sym.module, sym.offset);
                    }
                } else {
                    debug("\t0x%016lx", ip[i]);
                }
            }
        }
    }
}

static void print_stacks()
{
    struct process_stack_key   key   = {}, next_key;
    struct process_stack_value value = {};

    uint32_t stack_id = 0, next_stack_id;

    while (bpf_map_get_next_key(__bpf_map_fd[0], &key, &next_key) == 0) {
        bpf_map_lookup_elem(__bpf_map_fd[0], &next_key, &value);
        print_stack(&next_key, &value);
        bpf_map_delete_elem(__bpf_map_fd[0], &next_key);
        key = next_key;
    }

    debug("\n");

    // clear process stack map
    while (bpf_map_get_next_key(__bpf_map_fd[1], &stack_id, &next_stack_id) ==
           0) {
        bpf_map_delete_elem(__bpf_map_fd[1], &next_key);
        stack_id = next_stack_id;
    }
}

int32_t main(int32_t argc, char **argv)
{
    int32_t ret, pmu_fd, option_index = 0, c, statistical_duration = 0,
                         pid_count = 0, index = 0;
    struct bpf_object  *obj;
    struct bpf_program *prog;
    struct bpf_link   **perf_event_links;
    char                bpf_kern_o[EBPF_KERN_OBJ_BUF_SIZE];
    char                filter_pids_buf[FILTERPIDS_BUF_SIZE];

    debugLevel = 9;
    debugFile  = fdopen(STDOUT_FILENO, "w");

    while ((c = getopt_long(argc, argv, "k:p:d:o:h", long_options,
                            &option_index)) != -1) {
        switch (c) {
        case 'k':
            strncpy(bpf_kern_o, optarg, strlen(optarg));
            break;
        case 'p':
            strncpy(filter_pids_buf, optarg, strlen(optarg));
            if ((pid_count =
                     str_split_to_nums(filter_pids_buf, ":", __filter_pids,
                                       FILTERPIDS_MAX_COUNT)) < 0) {
                fprintf(stderr, "invalid filter pids: %s\n", filter_pids_buf);
                return -1;
            } else {
                for (int i = 0; i < pid_count; i++) {
                    debug("filter pid: %d", __filter_pids[i]);
                }
            }
            break;
        case 'd':
            statistical_duration = (int32_t)strtol(optarg, NULL, 10);
            if (statistical_duration <= 10) {
                statistical_duration = 10;
            }
            break;
        case 'o':
            __output_type = (enum output_type)atoi(optarg);
            if (__output_type < 0 || __output_type >= OUTPUT_TYPE_END) {
                fprintf(stderr,
                        "invalid output type: %d, must be set 0,1,2,3\n",
                        __output_type);
                return -1;
            }
            if (__output_type == OUTPUT_FILE_ALL ||
                __output_type == OUTPUT_FILE_FILTER) {
                debugFile = fopen(__default_output_file, "w");
            }
        case 'h':
            debug(
                "./perf_event_stack_cli --kern=xmbpf_perf_event_stack_kern.5.12.o --pids=pid:pid:pid --duration=10 --output=0");
            break;
        default:
            debug("unknown option: %c", c);
            exit(EXIT_FAILURE);
        }
    }

    debug("Loading BPF object file: '%s', filter pids: '%s' pid count: %d",
          bpf_kern_o, filter_pids_buf, pid_count);

    int nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);

    if (load_kallsyms()) {
        fprintf(stderr, "failed to process /proc/kallsyms\n");
        return -1;
    }

    libbpf_set_print(bpf_printf);

    ret = bump_memlock_rlimit();
    if (ret) {
        fprintf(stderr, "failed to increase memlock rlimit: %s\n",
                strerror(errno));
        return -1;
    }

    obj = bpf_object__open_file(bpf_kern_o, NULL);
    if (libbpf_get_error(obj)) {
        fprintf(stderr, "ERROR: opening BPF object file '%s' failed\n",
                bpf_kern_o);
        ret = -2;
        goto cleanup;
    }

    /* load BPF program */
    if (bpf_object__load(obj)) {
        fprintf(stderr, "ERROR: loading BPF object file failed\n");
        ret = -2;
        goto cleanup;
    }

    // 宽度
    __bpf_map_fd[0] =
        bpf_object__find_map_fd_by_name(obj, "process_stack_count");
    if (__bpf_map_fd[0] < 0) {
        fprintf(
            stderr,
            "ERROR: finding a map 'process_stack_count' in obj file failed\n");
        goto cleanup;
    }

    __bpf_map_fd[1] = bpf_object__find_map_fd_by_name(obj, "process_stack_map");
    if (__bpf_map_fd[1] < 0) {
        fprintf(
            stderr,
            "ERROR: finding a map 'process_stack_map' in obj file failed\n");
        goto cleanup;
    }

    __bpf_map_fd[2] = bpf_object__find_map_fd_by_name(obj, "ctrl_filter_map");
    if (__bpf_map_fd[1] < 0) {
        fprintf(stderr,
                "ERROR: finding a map 'ctrl_filter_map' in obj file failed\n");
        goto cleanup;
    }

    // 设置过滤的pid
    // filter_key              = CTRL_FILTER;
    // filter_value.filter_pid = filter_pid;
    // strncpy(filter_value.filter_content, __default_kern_obj,
    //         strlen(__default_kern_obj));

    // ret =
    //     bpf_map_update_elem(__bpf_map_fd[2], &filter_key, &filter_value, BPF_ANY);
    // if (0 != ret) {
    //     fprintf(stderr,
    //             "ERROR: bpf_map_update_elem filter value failed, ret: %d\n",
    //             ret);
    //     goto cleanup;
    // }
    cc_hashtable_conf_init(&__bcc_symcache_tab_conf);
    __bcc_symcache_tab_conf.hash             = GENERAL_HASH;
    __bcc_symcache_tab_conf.key_length       = sizeof(uint64_t);
    __bcc_symcache_tab_conf.initial_capacity = pid_count;
    __bcc_symcache_tab_conf.key_compare      = bcc_symcache_tab_key_compare;
    cc_hashtable_new_conf(&__bcc_symcache_tab_conf, &__bcc_symcache_tab);

    for (index = 0; index < pid_count; index++) {
        void *symcache = bcc_symcache_new((int32_t)__filter_pids[index], NULL);
        debug("add pid:%d symcache into hash", __filter_pids[index]);
        cc_hashtable_add(__bcc_symcache_tab, &__filter_pids[index], symcache);
    }

    // 打开perf event
    prog = bpf_object__find_program_by_name(
        obj, "xmonitor_bpf_collect_stack_traces");
    if (!prog) {
        fprintf(
            stderr,
            "finding prog: 'xmonitor_bpf_collect_stack_traces' in obj file failed\n");
        goto cleanup;
    }

    perf_event_links =
        (struct bpf_link **)calloc(nr_cpus, sizeof(struct bpf_link *));
    debug("nr cpu count: %d", nr_cpus);

    if (open_and_attach_perf_event(prog, nr_cpus, perf_event_links) < 0) {
        goto cleanup;
    } else {
        debug("open perf event with prog: '%s' successed",
              bpf_program__name(prog));
    }

    signal(SIGINT, __sig_handler);
    signal(SIGTERM, __sig_handler);

    while (!__sig_exit) {
        print_stacks();
        sleep(statistical_duration);
        debug("--------------statistical_duration--------------");
    }

cleanup:
    if (perf_event_links) {
        for (int32_t i = 0; i < nr_cpus; i++) {
            if (perf_event_links[i]) {
                bpf_link__destroy(perf_event_links[i]);
            }
        }
        free(perf_event_links);
    }

    bpf_object__close(obj);

    if (__bcc_symcache_tab) {
        CC_HashTableIter iter;
        cc_hashtable_iter_init(&iter, __bcc_symcache_tab);

        TableEntry *entry;
        while (cc_hashtable_iter_next(&iter, &entry) != CC_ITER_END) {
            bcc_free_symcache(entry->value, *(int32_t *)entry->key);
        }

        cc_hashtable_destroy(__bcc_symcache_tab);
        __bcc_symcache_tab = NULL;
    }

    debug("%s exit", argv[0]);
    debugClose();
    return 0;
}