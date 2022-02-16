/*
 * @Author: CALM.WU
 * @Date: 2022-01-26 17:00:40
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-01-26 17:01:11
 */

// 取得物理内存信息（相关文件/proc/meminfo）
// https://blog.51cto.com/u_13434336/2488970
// https://biscuitos.github.io/blog/HISTORY-PERCPU/
// http://calimeroteknik.free.fr/blag/?article20/really-used-memory-on-gnu-linux

#include "plugin_proc.h"

#include "prometheus-client-c/prom.h"

#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/strings.h"
#include "utils/adaptive_resortable_list.h"

#include "appconfig/appconfig.h"

static const char *      __proc_meminfo_filename = "/proc/meminfo";
static struct proc_file *__pf_meminfo            = NULL;
static ARL_BASE *        __arl_base              = NULL;

// 单位KB
static uint64_t
    // 所有可用的内存大小，物理内存减去预留位和内核使用。系统从加电开始到引导完成，firmware/BIOS要预留一些内存，内核本身要占用一些内存，
    // 最后剩下可供内核支配的内存就是MemTotal。这个值在系统运行期间一般是固定不变的，重启会改变
    __mem_total = 0,
    // 表示系统尚未使用的内存
    __mem_free = 0,
    // 真正的系统可用内存，系统中有些内存虽然已被使用但是可以回收的，比如cache/buffer、slab都有一部分可以回收，所以这部分可回收的内存加上MemFree才是系统可用的内存
    __mem_available = 0,
    // 用来给块设备做缓存的内存，(文件系统的 metadata、pages)
    __buffers = 0,
    // 分配给文件缓冲区的内存,例如vi一个文件，就会将未保存的内容写到该缓冲区
    __cached = 0,
    // 被高速缓冲存储用的交换空间（硬盘的swap）的大小
    __swap_cached = 0,
    // 经常使用的高速缓冲存储器页面文件大小
    // __active = 0,
    // 不经常使用的高速缓冲存储器文件大小
    // __inactive = 0,
    // 活跃的匿名内存
    // __active_anon = 0,
    // 不活跃的匿名内存
    // __inactive_anon = 0,
    // 活跃的文件使用内存
    // __active_file = 0,
    // 不活跃的文件使用内存
    // __inactive_file = 0,
    // 不能被释放的内存页
    // __unevictable = 0,
    // 系统调用 mlock 家族允许程序在物理内存上锁住它的部分或全部地址空间。这将阻止Linux
    // 将这个内存页调度到交换空间（swap space），即使该程序已有一段时间没有访问这段空间
    // __mlocked = 0,
    // 交换空间总内存
    __swap_total = 0,
    // 交换空间空闲内存
    __swap_free = 0,
    // 等待被写回到磁盘的
    __dirty = 0,
    // 正在被写回的
    __writeback = 0,
    // 未映射页的内存/映射到用户空间的非文件页表大小
    __anon_pages = 0,
    // 映射文件内存
    __mapped = 0,
    // 已经被分配的共享内存
    __shmem = 0,
    // 内核数据结构缓存
    __slab = 0,
    // 可收回slab内存
    __slab_reclaimable = 0,
    // 不可收回slab内存
    __slab_unreclaimable = 0,
    // 内核消耗的内存
    __kernel_stack = 0,
    // 管理内存分页的索引表的大小
    __page_tables = 0,
    // 不稳定页表的大小
    __nfs_unstable = 0,
    // 在低端内存中分配一个临时buffer作为跳转，把位于高端内存的缓存数据复制到此处消耗的内存
    __bounce = 0,
    // FUSE用于临时写回缓冲区的内存
    __writeback_tmp = 0,
    // 系统实际可分配内存
    __commit_limit = 0,
    // 系统当前已分配的内存
    __committed_as = 0,
    // 预留的虚拟内存总量
    __vmalloc_total = 0,
    // 已经被使用的虚拟内存
    __vmalloc_used = 0,
    // 可分配的最大的逻辑连续的虚拟内存
    __vmalloc_chunk = 0,
    //
    __mem_percpu = 0,
    // 当系统检测到内存的硬件故障时删除掉的内存页的总量
    __hardware_corrupted = 0,
    // 匿名大页缓存
    __anon_huge_pages = 0,
    //
    __shmem_huge_pages = 0,
    //
    //__shmem_pmdmapped = 0,
    //
    //__file_huge_pages = 0,
    //
    //__file_pmdmapped = 0,
    // 预留的大页内存总量
    __huge_pages_total = 0,
    // 空闲的大页内存
    __huge_pages_free = 0,
    // 已经被应用程序分配但尚未使用的大页内存
    __huge_pages_rsvd = 0,
    // 初始大页数与修改配置后大页数的差值
    __huge_pages_surp = 0;
// 单个大页内存的大小
// __huge_pages_size = 0,
// 映射TLB为4kB的内存数量
// __direct_map_4k = 0,
// 映射TLB为2M的内存数量
// __direct_map_2M = 0,
// 映射TLB为1G的内存数量
// __direct_map_1G = 0;

// 指标
static prom_gauge_t *__metric_memfree = NULL, *__metric_memused = NULL, *__metric_memcached = NULL,
                    *__metric_buffers = NULL, *__metric_memavailable = NULL,
                    *__metric_swapused = NULL, *__metric_swapfree = NULL,
                    *__metric_hardwarecorrupted = NULL, *__metric_committedas = NULL,
                    *__metric_dirty = NULL, *__metric_writeback = NULL,
                    *__metric_writebacktmp = NULL, *__metric_nfsunstable = NULL,
                    *__metric_bounce = NULL, *__metric_slab = NULL, *__metric_kernelstack = NULL,
                    *__metric_pagetables = NULL, *__metric_vmallocused = NULL,
                    *__metric_percpu = NULL, *__metric_slabreclaimable = NULL,
                    *__metric_slabunreclaimable = NULL, *__metric_hugepagesfree = NULL,
                    *__metric_hugepagesused = NULL, *__metric_hugepagesrsvd = NULL,
                    *__metric_hugepagessurp = NULL, *__metric_anonhugepages = NULL,
                    *__metric_shmemhugepages = NULL;

int32_t init_collector_proc_meminfo() {
    __arl_base = arl_create("proc_meminfo", NULL, 3);

    if (unlikely(NULL == __arl_base)) {
        return -1;
    }

    arl_expect(__arl_base, "MemTotal", &__mem_total);
    arl_expect(__arl_base, "MemFree", &__mem_free);
    arl_expect(__arl_base, "MemAvailable", &__mem_available);
    arl_expect(__arl_base, "Buffers", &__buffers);
    arl_expect(__arl_base, "Cached", &__cached);
    arl_expect(__arl_base, "SwapCached", &__swap_cached);
    // arl_expect(__arl_base, "Active", &__active);
    // arl_expect(__arl_base, "Inactive", &__inactive);
    // arl_expect(__arl_base, "Active(anon)", &__active_anon);
    // arl_expect(__arl_base, "Inactive(anon)", &__inactive_anon);
    // arl_expect(__arl_base, "Active(file)", &__active_file);
    // arl_expect(__arl_base, "Inactive(file)", &__inactive_file);
    arl_expect(__arl_base, "SwapTotal", &__swap_total);
    arl_expect(__arl_base, "SwapFree", &__swap_free);
    arl_expect(__arl_base, "Dirty", &__dirty);
    arl_expect(__arl_base, "Writeback", &__writeback);
    arl_expect(__arl_base, "AnonPages", &__anon_pages);
    arl_expect(__arl_base, "Mapped", &__mapped);
    arl_expect(__arl_base, "Shmem", &__shmem);
    arl_expect(__arl_base, "Slab", &__slab);
    arl_expect(__arl_base, "SRecliaimable", &__slab_reclaimable);
    arl_expect(__arl_base, "SUnreclaimable", &__slab_unreclaimable);
    arl_expect(__arl_base, "KernelStack", &__kernel_stack);
    arl_expect(__arl_base, "PageTables", &__page_tables);
    arl_expect(__arl_base, "NFS_Unstable", &__nfs_unstable);
    arl_expect(__arl_base, "Bounce", &__bounce);
    arl_expect(__arl_base, "WritebackTmp", &__writeback_tmp);
    arl_expect(__arl_base, "CommitLimit", &__commit_limit);
    arl_expect(__arl_base, "Committed_AS", &__committed_as);
    arl_expect(__arl_base, "VmallocTotal", &__vmalloc_total);
    arl_expect(__arl_base, "VmallocUsed", &__vmalloc_used);
    arl_expect(__arl_base, "VmallocChunk", &__vmalloc_chunk);
    arl_expect(__arl_base, "Percpu", &__mem_percpu);
    arl_expect(__arl_base, "HardwareCorrupted", &__hardware_corrupted);
    arl_expect(__arl_base, "AnonHugePages", &__anon_huge_pages);
    arl_expect(__arl_base, "ShmemHugePages", &__shmem_huge_pages);
    arl_expect(__arl_base, "HugePages_Total", &__huge_pages_total);
    arl_expect(__arl_base, "HugePages_Free", &__huge_pages_free);
    arl_expect(__arl_base, "HugePages_Rsvd", &__huge_pages_rsvd);
    arl_expect(__arl_base, "HugePages_Surp", &__huge_pages_surp);
    // arl_expect(__arl_base, "Hugepagesize", &__huge_pages_size);
    // arl_expect(__arl_base, "DirectMap4k", &__direct_map_4k);
    // arl_expect(__arl_base, "DirectMap2M", &__direct_map_2M);
    // arl_expect(__arl_base, "DirectMap1G", &__direct_map_1G);

    // 初始化指标
    __metric_memfree = prom_collector_registry_must_register_metric(
        prom_gauge_new("mem_free", "System RAM Free", 2, (const char *[]){ "host", "meminfo" }));
    __metric_memused = prom_collector_registry_must_register_metric(
        prom_gauge_new("mem_used", "System RAM Used", 2, (const char *[]){ "host", "meminfo" }));
    __metric_memcached = prom_collector_registry_must_register_metric(prom_gauge_new(
        "mem_cached", "System RAM Cached", 2, (const char *[]){ "host", "meminfo" }));
    __metric_buffers   = prom_collector_registry_must_register_metric(prom_gauge_new(
        "mem_buffers", "System RAM Buffers", 2, (const char *[]){ "host", "meminfo" }));

    __metric_memavailable = prom_collector_registry_must_register_metric(
        prom_gauge_new("mem_available", "Available RAM for applications", 2,
                       (const char *[]){ "host", "meminfo" }));

    __metric_swapused = prom_collector_registry_must_register_metric(
        prom_gauge_new("swap_used", "System Swap Used", 2, (const char *[]){ "host", "meminfo" }));
    __metric_swapfree = prom_collector_registry_must_register_metric(
        prom_gauge_new("swap_free", "System Swap Free", 2, (const char *[]){ "host", "meminfo" }));

    __metric_hardwarecorrupted = prom_collector_registry_must_register_metric(
        prom_gauge_new("hardware_corrupted", "Corrupted Memory, detected by ECC", 2,
                       (const char *[]){ "host", "meminfo" }));

    __metric_committedas = prom_collector_registry_must_register_metric(prom_gauge_new(
        "commited_as", "Committed (Allocated) Memory", 2, (const char *[]){ "host", "meminfo" }));

    __metric_dirty     = prom_collector_registry_must_register_metric(prom_gauge_new(
        "dirty", "Writeback Dirty Memory", 2, (const char *[]){ "host", "meminfo" }));
    __metric_writeback = prom_collector_registry_must_register_metric(
        prom_gauge_new("writeback", "Writeback Memory", 2, (const char *[]){ "host", "meminfo" }));
    __metric_writebacktmp = prom_collector_registry_must_register_metric(prom_gauge_new(
        "writeback_tmp", "Fuse Writeback Memory", 2, (const char *[]){ "host", "meminfo" }));
    __metric_nfsunstable  = prom_collector_registry_must_register_metric(prom_gauge_new(
        "nfs_unstable", "NFS Writeback Memory", 2, (const char *[]){ "host", "meminfo" }));
    __metric_bounce       = prom_collector_registry_must_register_metric(
        prom_gauge_new("bounce", "Bounce Memory", 2, (const char *[]){ "host", "meminfo" }));

    __metric_slab        = prom_collector_registry_must_register_metric(prom_gauge_new(
        "slab", "Slab Memory Used by Kernel", 2, (const char *[]){ "host", "meminfo" }));
    __metric_kernelstack = prom_collector_registry_must_register_metric(
        prom_gauge_new("kernelstack", "Kernel Stack Memory Used by Kernel", 2,
                       (const char *[]){ "host", "meminfo" }));
    __metric_pagetables = prom_collector_registry_must_register_metric(
        prom_gauge_new("pagetables", "Page Tables Memory Used by Kernel", 2,
                       (const char *[]){ "host", "meminfo" }));
    __metric_vmallocused = prom_collector_registry_must_register_metric(prom_gauge_new(
        "vmalloc_used", "Vmalloc Used Memory by Kernel", 2, (const char *[]){ "host", "meminfo" }));
    __metric_percpu      = prom_collector_registry_must_register_metric(prom_gauge_new(
        "percpu", "Per-CPU Memory Used by Kernel", 2, (const char *[]){ "host", "meminfo" }));

    __metric_slabreclaimable = prom_collector_registry_must_register_metric(
        prom_gauge_new("slab_reclaimable", "Slab Reclaimable Kernel Memory", 2,
                       (const char *[]){ "host", "meminfo" }));
    __metric_slabunreclaimable = prom_collector_registry_must_register_metric(
        prom_gauge_new("slab_unreclaimable", "Slab Unreclaimable Kernel Memory", 2,
                       (const char *[]){ "host", "meminfo" }));

    __metric_hugepagesused = prom_collector_registry_must_register_metric(
        prom_gauge_new("hugepages_used", "Dedicated HugePages Used Memory", 2,
                       (const char *[]){ "host", "meminfo" }));
    __metric_hugepagesfree = prom_collector_registry_must_register_metric(
        prom_gauge_new("hugepages_free", "Dedicated HugePages Free Memory", 2,
                       (const char *[]){ "host", "meminfo" }));
    __metric_hugepagesrsvd = prom_collector_registry_must_register_metric(
        prom_gauge_new("hugepages_svd", "Dedicated HugePages SVD Memory", 2,
                       (const char *[]){ "host", "meminfo" }));
    __metric_hugepagessurp = prom_collector_registry_must_register_metric(
        prom_gauge_new("hugepages_surp", "Dedicated HugePages Surplus Memory", 2,
                       (const char *[]){ "host", "meminfo" }));

    __metric_anonhugepages = prom_collector_registry_must_register_metric(
        prom_gauge_new("anon_hugepages", "Transparent HugePages Anonymous Memory", 2,
                       (const char *[]){ "host", "meminfo" }));
    __metric_shmemhugepages = prom_collector_registry_must_register_metric(
        prom_gauge_new("shmem_hugepages", "Transparent HugePages Shared Memory", 2,
                       (const char *[]){ "host", "meminfo" }));

    debug("[PLUGIN_PROC:proc_meminfo] init successed");
    return 0;
}

int32_t collector_proc_meminfo(int32_t UNUSED(update_every), usec_t UNUSED(dt),
                               const char *config_path) {
    debug("[PLUGIN_PROC:proc_meminfo] config:%s running", config_path);

    const char *f_meminfo =
        appconfig_get_member_str(config_path, "monitor_file", __proc_meminfo_filename);

    if (unlikely(!__pf_meminfo)) {
        __pf_meminfo = procfile_open(f_meminfo, " \t:", PROCFILE_FLAG_DEFAULT);
        if (unlikely(!__pf_meminfo)) {
            error("Cannot open %s", f_meminfo);
            return -1;
        }
    }

    __pf_meminfo = procfile_readall(__pf_meminfo);
    if (unlikely(!__pf_meminfo)) {
        error("Cannot read %s", f_meminfo);
        return -1;
    }

    size_t lines = procfile_lines(__pf_meminfo);

    // 更新采集数值
    arl_begin(__arl_base);

    for (size_t l = 0; l < lines; l++) {
        size_t words = procfile_linewords(__pf_meminfo, l);
        if (unlikely(words < 2))
            continue;

        const char *meminfo_key = procfile_lineword(__pf_meminfo, l, 0);
        if (unlikely(arl_check(__arl_base, meminfo_key, procfile_lineword(__pf_meminfo, l, 1)))) {
            break;
        }
    }

    // 设置指标

    // MemFree MemUsed MemCached Buffers
    uint64_t mem_cached = __cached + __slab_reclaimable - __shmem;
    uint64_t mem_used   = __mem_total - __mem_free - mem_cached - __buffers;
    prom_gauge_set(__metric_memfree, __mem_free,
                   (const char *[]){ premetheus_instance_label, "mem" });
    prom_gauge_set(__metric_memused, mem_used,
                   (const char *[]){ premetheus_instance_label, "mem" });
    prom_gauge_set(__metric_memcached, mem_cached,
                   (const char *[]){ premetheus_instance_label, "mem" });
    prom_gauge_set(__metric_buffers, __buffers,
                   (const char *[]){ premetheus_instance_label, "mem" });

    // MemAvailable
    prom_gauge_set(__metric_memavailable, __mem_available,
                   (const char *[]){
                       premetheus_instance_label,
                       "available",
                   });
    debug("[PLUGIN_PROC:proc_meminfo] mem_free:%lu kB mem_used:%lu kB mem_cached:%lu kB "
          "buffers:%lu kB mem_available:%lu kB",
          __mem_free, mem_used, mem_cached, __buffers, __mem_available);

    // SwapUsed SwapFree
    uint64_t swap_used = __swap_total - __swap_free;
    prom_gauge_set(__metric_swapused, swap_used,
                   (const char *[]){ premetheus_instance_label, "swap" });
    prom_gauge_set(__metric_swapfree, __swap_free,
                   (const char *[]){ premetheus_instance_label, "swap" });
    debug("[PLUGIN_PROC:proc_meminfo] swap_used:%lu kB swap_free:%lu kB", swap_used, __swap_free);

    // HardwareCorrupted
    prom_gauge_set(__metric_hardwarecorrupted, __hardware_corrupted,
                   (const char *[]){ premetheus_instance_label, "hwcorrupt" });

    // Committed_AS Committed (Allocated) Memory
    prom_gauge_set(__metric_committedas, __committed_as,
                   (const char *[]){ premetheus_instance_label, "committed" });

    debug(
        "[PLUGIN_PROC:proc_meminfo] hardware_corrupted:%lu kB Committed (Allocated) Memory:%lu kB",
        __hardware_corrupted, __committed_as);

    // Dirty Writeback FuseWriteback NfsWriteback Bounce
    prom_gauge_set(__metric_dirty, __dirty,
                   (const char *[]){ premetheus_instance_label, "writeback" });
    prom_gauge_set(__metric_writeback, __writeback,
                   (const char *[]){ premetheus_instance_label, "writeback" });
    prom_gauge_set(__metric_writebacktmp, __writeback_tmp,
                   (const char *[]){ premetheus_instance_label, "writeback" });
    prom_gauge_set(__metric_nfsunstable, __nfs_unstable,
                   (const char *[]){ premetheus_instance_label, "writeback" });
    prom_gauge_set(__metric_bounce, __bounce,
                   (const char *[]){ premetheus_instance_label, "writeback" });
    debug("[PLUGIN_PROC:proc_meminfo] Dirty:%lu kB Writeback:%lu kB WritebackTmp:%lu kB "
          "NfsUnstable:%lu kB Bounce:%lu kB",
          __dirty, __writeback, __writeback_tmp, __nfs_unstable, __bounce);

    // Slab KernelStack PageTables VmallocUsed Percpu
    prom_gauge_set(__metric_slab, __slab, (const char *[]){ premetheus_instance_label, "kernel" });
    prom_gauge_set(__metric_kernelstack, __kernel_stack,
                   (const char *[]){ premetheus_instance_label, "kernel" });
    prom_gauge_set(__metric_pagetables, __page_tables,
                   (const char *[]){
                       premetheus_instance_label,
                       "kernel",
                   });
    prom_gauge_set(__metric_vmallocused, __vmalloc_used,
                   (const char *[]){ premetheus_instance_label, "kernel" });
    prom_gauge_set(__metric_percpu, __mem_percpu,
                   (const char *[]){ premetheus_instance_label, "kernel" });
    debug("[PLUGIN_PROC:proc_meminfo] Slab:%lu kB KernelStack:%lu kB PageTables:%lu kB "
          "VmallocUsed:%lu kB Percpu:%lu kB",
          __slab, __kernel_stack, __page_tables, __vmalloc_used, __mem_percpu);

    // reclaimable unreclaimable
    prom_gauge_set(__metric_slabreclaimable, __slab_reclaimable,
                   (const char *[]){ premetheus_instance_label, "slab" });
    prom_gauge_set(__metric_slabunreclaimable, __slab_unreclaimable,
                   (const char *[]){ premetheus_instance_label, "slab" });
    debug("[PLUGIN_PROC:proc_meminfo] SlabReclaimable:%lu kB SlabUnreclaimable:%lu kB",
          __slab_reclaimable, __slab_unreclaimable);

    // HugePages_Total - HugePages_Free - HugePages_Rsvd HugePages_Free HugePages_Rsvd
    // HugePages_Surp
    prom_gauge_set(__metric_hugepagesused,
                   __huge_pages_total - __huge_pages_free - __huge_pages_rsvd,
                   (const char *[]){ premetheus_instance_label, "hugepages" });
    prom_gauge_set(__metric_hugepagesfree, __huge_pages_free,
                   (const char *[]){
                       premetheus_instance_label,
                       "hugepages",
                   });
    prom_gauge_set(__metric_hugepagesrsvd, __huge_pages_rsvd,
                   (const char *[]){
                       premetheus_instance_label,
                       "hugepages",
                   });
    prom_gauge_set(__metric_hugepagessurp, __huge_pages_surp,
                   (const char *[]){
                       premetheus_instance_label,
                       "hugepages",
                   });
    debug("[PLUGIN_PROC:proc_meminfo] HugePagesUsed:%lu HugePagesFree:%lu HugePagesRsvd:%lu "
          "HugePagesSurp:%lu",
          __huge_pages_total - __huge_pages_free - __huge_pages_rsvd, __huge_pages_free,
          __huge_pages_rsvd, __huge_pages_surp);

    // AnonHugePages ShmemHugePages
    prom_gauge_set(__metric_anonhugepages, __anon_huge_pages,
                   (const char *[]){ premetheus_instance_label, "transparent_hugepages" });
    prom_gauge_set(__metric_shmemhugepages, __shmem_huge_pages,
                   (const char *[]){ premetheus_instance_label, "transparent_hugepages" });
    debug("[PLUGIN_PROC:proc_meminfo] AnonHugePages:%lu kB ShmemHugePages:%lu kB",
          __anon_huge_pages, __shmem_huge_pages);
    return 0;
}

void fini_collector_proc_meminfo() {
    if (likely(!__arl_base)) {
        arl_free(__arl_base);
        __arl_base = NULL;
    }

    if (likely(__pf_meminfo)) {
        procfile_close(__pf_meminfo);
        __pf_meminfo = NULL;
    }
    debug("[PLUGIN_PROC:proc_meminfo] stopped");
}