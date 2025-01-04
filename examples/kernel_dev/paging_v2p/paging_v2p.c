/*
 * @Author: calmwu
 * @Date: 2025-01-04 17:06:54
 * @Last Modified by: calmwu
 * @Last Modified time: 2025-01-04 18:43:51
 */

#define pr_fmt(fmt) "%s:%s(): " fmt, KBUILD_MODNAME, __func__

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kthread.h>
#include <linux/rwsem.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/utsname.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sched/task.h>
#include <linux/list.h>
#include <linux/mm.h>

#include "../kutils/misc.h"

#define MODULE_TAG "Module:[cw_paging_v2p]"

static uint64_t __cr0, __cr3;

static void __dump_pgtable_macros(void)
{
    // 读取 cr0 寄存器
    __cr0 = read_cr0();
    __cr3 = read_cr3_pa();

    pr_info(MODULE_TAG " cr0:0x%016llx, cr3:0x%016llx\n", __cr0, __cr3);

    //这些宏是用来指示线性地址中相应字段所能映射的区域大小的对数的
    // 页全局目录偏移量（Page Global Directory Shift）
    // PGDIR_SHIFT 定义了页全局目录（PGD）的索引和偏移量。PGD 是页表的顶级。
    pr_info(MODULE_TAG "PGDIR_SHIFT = %d\n", PGDIR_SHIFT);
    // PMD_SHIFT：中级页目录偏移量（Page Middle Directory Shift）
    // PMD_SHIFT 定义了中级页目录（PMD）的索引和偏移量。PMD 是页表的第二级。
    pr_info(MODULE_TAG "P4D_SHIFT = %d\n", P4D_SHIFT);
    // 上级页目录偏移量（Page Upper Directory Shift）
    // PUD_SHIFT 定义了上级页目录（PUD）的索引和偏移量。PUD 是页表的第三级。
    pr_info(MODULE_TAG "PUD_SHIFT = %d\n", PUD_SHIFT);
    // 第四级页目录偏移量（Page 4th Directory Shift）
    // P4D_SHIFT 定义了第四级页目录（P4D）的索引和偏移量。P4D 是页表的第四级（仅在 64 位系统中存在）。
    pr_info(MODULE_TAG "PMD_SHIFT = %d\n", PMD_SHIFT);
    // PAGE_SHIFT 定义了页的大小（以字节为单位）的对数。例如，如果 PAGE_SHIFT = 12，则页大小为 2^12 = 4096 字节。
    pr_info(MODULE_TAG "PAGE_SHIFT = %d\n", PAGE_SHIFT);

    // 这些宏定义是 Linux 内核中用于描述页表层次结构的重要参数。它们定义了页表的各个级别中指针的数量
    // 页全局目录指针数量（Pointers per Page Global Directory）
    // PTRS_PER_PGD 定义了页全局目录（PGD）中指针的数量。每个 PGD 指向一个第四级页目录（P4D）。
    pr_info(MODULE_TAG "PTRS_PER_PGD = %d\n", PTRS_PER_PGD);
    // 第四级页目录指针数量（Pointers per Page 4th Directory）
    // PTRS_PER_P4D 定义了第四级页目录（P4D）中指针的数量。每个 P4D 指向一个上级页目录（PUD）。
    pr_info(MODULE_TAG "PTRS_PER_P4D = %d\n", PTRS_PER_P4D);
    // PTRS_PER_PUD 定义了上级页目录（PUD）中指针的数量。每个 PUD 指向一个中级页目录（PMD）。
    pr_info(MODULE_TAG "PTRS_PER_PUD = %d\n", PTRS_PER_PUD);
    // PTRS_PER_PMD 定义了中级页目录（PMD）中指针的数量。每个 PMD 指向一个页表。
    pr_info(MODULE_TAG "PTRS_PER_PMD = %d\n", PTRS_PER_PMD);
    // PTRS_PER_PTE 定义了页表项（PTE）中指针的数量。每个 PTE 指向一个页框（物理页）
    pr_info(MODULE_TAG "PTRS_PER_PTE = %d\n", PTRS_PER_PTE);
}

// 通过线性地址获取物理地址
static uint64_t __vaddr2paddr(uint64_t vaddr)
{
    //首先为每个目录项创建一个变量将它们保存起来
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;

    uint64_t paddr = 0;
    uint64_t page_addr = 0;
    uint64_t page_offset = 0;

    //第一个参数是当前进程的 mm_struct 结构（我们申请的线性地址空间是内核，所以应该查内核页表，
    //又因为所有的进程都共享同一个内核页表，所以可以用当前进程的 mm_struct 结构来进行查找）
    pgd = pgd_offset(current->mm, vaddr);
    pr_info(MODULE_TAG "pgd_val = 0x%lx, pgd_index = %llu\n", pgd_val(*pgd),
            pgd_index(vaddr));
    if (pgd_none(*pgd)) {
        pr_err(MODULE_TAG "not mapped in pgd\n");
        return -1;
    }

    //查找到的页全局目录项 pgd 作为下级查找的参数传入到 p4d_offset 中
    p4d = p4d_offset(pgd, vaddr);
    pr_info(MODULE_TAG "p4d_val = 0x%lx, p4d_index = %lu\n", p4d_val(*p4d),
            p4d_index(vaddr));
    if (p4d_none(*p4d)) {
        pr_err(MODULE_TAG "not mapped in p4d\n");
        return -1;
    }

    pud = pud_offset(p4d, vaddr);
    pr_info(MODULE_TAG "pud_val = 0x%lx, pud_index = %lu\n", pud_val(*pud),
            pud_index(vaddr));
    if (pud_none(*pud)) {
        pr_err(MODULE_TAG "not mapped in pud\n");
        return -1;
    }

    pmd = pmd_offset(pud, vaddr);
    pr_info(MODULE_TAG "pmd_val = 0x%lx, pmd_index = %lu\n", pmd_val(*pmd),
            pmd_index(vaddr));
    if (pmd_none(*pmd)) {
        pr_err(MODULE_TAG "not mapped in pmd\n");
        return -1;
    }

    //与上面略有不同，这里表示在内核页表中查找，在进程页表中查找是另外一个完全不同的函数   这里最后取得了页表的线性地址
    pte = pte_offset_kernel(pmd, vaddr);
    pr_info(MODULE_TAG "pte_val = 0x%lx, ptd_index = %lu\n", pte_val(*pte),
            pte_index(vaddr));

    if (pte_none(*pte)) {
        pr_err(MODULE_TAG "not mapped in pte\n");
        return -1;
    }

    //从页表的线性地址中取出该页表所映射页框的物理地址
    page_addr = pte_val(*pte) & PAGE_MASK; //取出其高 48 位
    //取出页偏移地址，页偏移量也就是线性地址中的低 12 位
    page_offset = vaddr & ~PAGE_MASK;
    //将两个地址拼接起来，就得到了想要的物理地址了
    paddr = page_addr | page_offset;
    pr_info(MODULE_TAG "page_addr = 0x%016llx, page_offset = 0x%016llx\n",
            page_addr, page_offset);
    pr_info(MODULE_TAG "vaddr = 0x%016llx, paddr = 0x%016llx\n", vaddr, paddr);
    return paddr;
}

static int32_t __init __cw_paging_v2p_init(void)
{
    uint64_t vaddr = 0;

    pr_info(MODULE_TAG " hello arch:'%s', release:'%s', version:'%s'\n",
            init_uts_ns.name.machine, init_uts_ns.name.release,
            init_uts_ns.name.version);

    __dump_pgtable_macros();
    // 从内核的页缓存中找到一个空闲页
    vaddr = __get_free_page(GFP_KERNEL);
    if (vaddr == 0) {
        pr_err(MODULE_TAG "__get_free_page failed..\n");
        return -1;
    }
    //在地址中写入 hello
    sprintf((char *)vaddr, "hello world from kernel");
    pr_info(MODULE_TAG "get_page_vaddr=0x%016llx\n", vaddr);
    __vaddr2paddr(vaddr);

    // 释放页
    free_page(vaddr);

    pr_info(MODULE_TAG " init successfully!\n");
    return 0;
}

static void __exit __cw_paging_v2p_exit(void)
{
    pr_info(MODULE_TAG " bye!\n");
}

module_init(__cw_paging_v2p_init);
module_exit(__cw_paging_v2p_exit);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("calmwu <wubo0067@hotmail.com>");
MODULE_DESCRIPTION("cw_pagin_lowmem");
MODULE_VERSION("0.1");
