/*
 * @Author: CALM.WU
 * @Date: 2022-03-23 16:19:01
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-03-29 16:09:38
 */

/*
VmRSS                       size of memory portions. It contains the three
                            following parts (VmRSS = RssAnon + RssFile + RssShmem)
RssAnon                     size of resident anonymous memory
RssFile                     size of resident file mappings
RssShmem                    size of resident shmem memory (includes SysV shm,
                            mapping of tmpfs and shared anonymous mappings)

/proc/<pid>/statm
 Field    Content
 size     total program size (pages)            (same as VmSize in status)
 resident size of memory portions (pages)       (same as VmRSS in status)
 shared   number of pages that are shared       (i.e. backed by a file)
 trs      number of pages that are 'code'       (not including libs; broken,
                                                        includes data segment)
 lrs      number of pages of library            (always 0 on 2.6)
 drs      number of pages of data/stack         (including libs; broken,
                                                        includes library text)
 dt       number of dirty pages                 (always 0 on 2.6)

https://developer.aliyun.com/article/54405
https://www.jianshu.com/p/8203457a11cc
https://www.cnblogs.com/arnoldlu/p/9375377.html
vss>=rss>=pss>=uss

USS = Private_Clean + Private_Dirty
PSS = USS + (Shared_Clean + Shared_Dirty)/n
是平摊计算后的实际物理使用内存(有些内存会和其他进程共享，例如mmap进来的)。实际上包含下面private_clean+private_dirty，和按比例均分的shared_clean、shared_dirty。
RSS = Private_Clean + Private_Dirty + Shared_Clean + Shared_Dirty

VSS是单个进程全部可访问的虚拟地址空间，其大小可能包括还尚未在内存中驻留的部分。对于确定单个进程实际内存使用大小，VSS用处不大。
RSS是单个进程实际占用的内存大小，RSS不太准确的地方在于它包括该进程所使用共享库全部内存大小。对于一个共享库，可能被多个进程使用，实际该共享库只会被装入内存一次。
进而引出了PSS，PSS相对于RSS计算共享库内存大小是按比例的。N个进程共享，该库对PSS大小的贡献只有1/N。
USS是单个进程私有的内存大小，即该进程独占的内存部分。USS揭示了运行一个特定进程在的真实内存增量大小。如果进程终止，USS就是实际被返还给系统的内存大小。
*/

#include "process_stat.h"

#include "pagemap/pagemap.h"

#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/strings.h"

static pm_kernel_t *__pm_ker = NULL;

static pthread_once_t __init_pm_ker_once = PTHREAD_ONCE_INIT;

static void __process_mem_init_pm_kernel() {
    int32_t ret = pm_kernel_create(&__pm_ker);
    if (unlikely(ret != 0)) {
        error("Error creating kernel interface -- does this kernel have pagemap?, ret: %d", ret);
        exit(EXIT_FAILURE);
    }
}

/**
 * Collects the memory usage of a process
 *
 * @param pstat the pid_stat structure to fill
 *
 * @return Returning 0 means success, non-zero means failure.
 */
int32_t collector_process_mem_usage(struct pid_stat *pstat) {
    int32_t ret = 0;

    pthread_once(&__init_pm_ker_once, __process_mem_init_pm_kernel);

    if (unlikely(NULL == pstat || pstat->pid <= 0)) {
        error("pid_sat is NULL or pid <= 0");
        return -1;
    }

    pm_process_t *pm_proc = NULL;
    ret = pm_process_create(__pm_ker, pstat->pid, &pm_proc);
    if (unlikely(ret != 0)) {
        error("could not create process interface for pid:'%d', ret: %d", pstat->pid, ret);
        return -1;
    }

    pm_memusage_t pm_mem_usage;
    ret = pm_process_usage_flags(pm_proc, &pm_mem_usage, 0, 0);
    if (unlikely(ret != 0)) {
        error("could not get process memory usage for pid:'%d', ret: %d", pstat->pid, ret);
    }

    pm_process_destroy(pm_proc);

    // 获取进程的内存使用指标，转换成KB
    pstat->vmsize = pm_mem_usage.vss / 1024;   // /proc/pid/status.VmSize
    pstat->vmrss = pm_mem_usage.rss / 1024;
    pstat->vmswap = pm_mem_usage.swap / 1024;
    pstat->pss = pm_mem_usage.pss / 1024;
    pstat->uss = pm_mem_usage.uss / 1024;

    return 0;
}
