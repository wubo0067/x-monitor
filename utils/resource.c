/*
 * @Author: CALM.WU
 * @Date: 2021-11-03 11:51:34
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-02-11 14:04:08
 */

#include "common.h"
#include "compiler.h"
#include "log.h"
#include "procfile.h"
#include "resource.h"

static int32_t __processors = 1;

static const char __no_user[] = "";

// 锁定内存限制 BCC 已经无条件地将此限制设置为无穷大，但 libbpf 不会自动执行此操作（按照设计）。
int32_t bump_memlock_rlimit(void) {
    struct rlimit rlim_new = {
        .rlim_cur = RLIM_INFINITY,
        .rlim_max = RLIM_INFINITY,
    };

    return setrlimit(RLIMIT_MEMLOCK, &rlim_new);
}

const char *get_username(uid_t uid) {
    struct passwd *pwd = getpwuid(uid);
    if (pwd == NULL) {
        return __no_user;
    }
    return pwd->pw_name;
}

int32_t get_system_cpus() {
    // return sysconf(_SC_NPROCESSORS_ONLN); 加强移植性

    struct proc_file *pf_stat = procfile_open("/proc/stat", NULL, PROCFILE_FLAG_DEFAULT);
    if (unlikely(!pf_stat)) {
        error("Cannot open /proc/stat. Assuming system has %d processors. error: %s", __processors,
              strerror(errno));
        return __processors;
    }

    pf_stat = procfile_readall(pf_stat);
    if (unlikely(!pf_stat)) {
        error("Cannot read /proc/stat. Assuming system has %d __processors.", __processors);
        return __processors;
    }

    __processors = 0;

    for (size_t index = 0; index < procfile_lines(pf_stat); index++) {
        if (!procfile_linewords(pf_stat, index)) {
            continue;
        }

        if (strncmp(procfile_lineword(pf_stat, index, 0), "cpu", 3) == 0) {
            __processors++;
        }
    }

    __processors--;
    if (__processors < 1) {
        __processors = 1;
    }

    procfile_close(pf_stat);

    debug("System has %d __processors.", __processors);

    return __processors;
}