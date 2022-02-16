/*
 * @Author: CALM.WU
 * @Date: 2022-01-25 11:02:43
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-01-25 17:24:49
 */

#include "utils/common.h"
#include "utils/log.h"
#include "utils/adaptive_resortable_list.h"

static ARL_BASE *__arl_base = NULL;

#define P_ALRCHECK_RET(n)            \
    do {                             \
        debug("arl_check :%d", (n)); \
    } while (0)

int32_t main(int32_t argc, char **argv) {
    if (log_init("../cli/arl_test/log.cfg", "arl_test") != 0) {
        fprintf(stderr, "log init failed\n");
        return -1;
    }

    debug("arl_test");

    static uint64_t
        // 所有可用的内存大小，物理内存减去预留位和内核使用。系统从加电开始到引导完成，firmware/BIOS要预留一些内存，内核本身要占用一些内存，
        // 最后剩下可供内核支配的内存就是MemTotal。这个值在系统运行期间一般是固定不变的，重启会改变
        mem_total = 0,
        // 表示系统尚未使用的内存
        mem_free = 0,
        // 真正的系统可用内存，系统中有些内存虽然已被使用但是可以回收的，比如cache/buffer、slab都有一部分可以回收，所以这部分可回收的内存加上MemFree才是系统可用的内存
        mem_available = 0,
        // 用来给块设备做缓存的内存，(文件系统的 metadata、pages)
        buffers = 0;

    // 第三个参数和动态分配节点释放有关
    __arl_base = arl_create("arl_test", NULL, 2);

    arl_expect(__arl_base, "MemTotal", &mem_total);
    arl_expect(__arl_base, "MemFree", &mem_free);
    arl_expect(__arl_base, "MemAvailable", &mem_available);
    arl_expect(__arl_base, "Buffers", &buffers);

    debug("round 1-----------------");

    arl_begin(__arl_base);
    arl_check(__arl_base, "MemTotal", "1234");
    arl_check(__arl_base, "MemFree", "5678");
    arl_check(__arl_base, "MemAvailable", "9012");
    arl_check(__arl_base, "Buffers", "3456");

    debug("MemTotal: %lu", mem_total);
    debug("MemFree: %lu", mem_free);
    debug("MemAvailable: %lu", mem_available);
    debug("Buffers: %lu", buffers);

    debug("round 2-----------------");

    arl_begin(__arl_base);
    arl_check(__arl_base, "MemTotal", "10485760");
    arl_check(__arl_base, "MemFree", "16211188");
    arl_check(__arl_base, "MemAvailable", "131396");
    arl_check(__arl_base, "Buffers", "1238312");

    debug("MemTotal: %lu", mem_total);
    debug("MemFree: %lu", mem_free);
    debug("MemAvailable: %lu", mem_available);
    debug("Buffers: %lu", buffers);

    debug("round 3-----------------");

    arl_begin(__arl_base);
    P_ALRCHECK_RET(arl_check(__arl_base, "SReclaimable", "131396"));
    debug("head_name: %s, next_keyword_name: %s", __arl_base->head->name,
          __arl_base->next_keyword->name);

    arl_begin(__arl_base);
    P_ALRCHECK_RET(arl_check(__arl_base, "SUnreclaim", "119672"));
    debug("head_name: %s, next_keyword_name: %s", __arl_base->head->name,
          __arl_base->next_keyword->name);

    arl_begin(__arl_base);
    debug("head_name: %s, next_keyword_name: %s", __arl_base->head->name,
          __arl_base->next_keyword->name);
    arl_begin(__arl_base);
    debug("head_name: %s, next_keyword_name: %s", __arl_base->head->name,
          __arl_base->next_keyword->name);
    arl_begin(__arl_base);
    debug("head_name: %s, next_keyword_name: %s", __arl_base->head->name,
          __arl_base->next_keyword->name);

    arl_free(__arl_base);

    log_fini();

    return 0;
}