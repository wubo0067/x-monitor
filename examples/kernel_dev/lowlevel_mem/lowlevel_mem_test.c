/*
 * @Author: CALM.WU
 * @Date: 2025-07-17 14:31:27
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2025-07-17 18:14:28
 */

#define pr_fmt(fmt) "%s:%s(): " fmt, KBUILD_MODNAME, __func__

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <asm/io.h> /* virt_to_phys(), phys_to_virt(), ... */

#include "../kutils/misc.h"
#include "../kutils/utils.h"

#define MODULE_TAG "Module:[cw_lowlevel_mem]"

static void *k_addr1, *k_addr2, *k_addr3, *k_addr4, *k_addr5;
static int32_t __k_addr2_order = 3;

static void __show_phy_pages(void *k_addr, size_t len, bool contiguity_check)
{
    void *k_addr_start = k_addr;
    const char *hdr =
            "-pg#-  --------va--------   --------pa--------   ---PFN---\n";
    // 物理地址
    phys_addr_t paddr;
    int32_t loops = len / PAGE_SIZE, i;
    long pfn, prev_pfn = 1;

#ifdef CONFIG_X86_64
    // 检查虚拟地址是否有效
    if (!virt_addr_valid(k_addr_start)) {
        pr_err(MODULE_TAG "Invalid kernel virtual address: (0x%px)\n",
               k_addr_start);
        return;
    }
#endif
    pr_info("%s():start kaddr %px, len %zu, contiguity_check is %s\n", __func__,
            k_addr_start, len, contiguity_check ? "on" : "off");
    pr_info("%s", hdr);

    if (len % PAGE_SIZE) {
        loops++;
    }

    for (i = 0; i < loops; i++) {
        // 虚拟地址转换为物理地址
        paddr = virt_to_phys(k_addr_start + i * PAGE_SIZE);
        // 物理地址转换为页帧号
        pfn = PHYS_PFN(paddr);

        if (!!contiguity_check) {
            // 判断当前物理页帧号（pfn）是否和前一个物理页帧号（prev_pfn）连续。如果不连续，说明物理页出现了断裂。
            if (i > 0 && pfn != prev_pfn + 1) {
                pr_notice(MODULE_TAG
                          "Non-contiguous page detected at index %d\n",
                          i);
                break;
            }
        }

#if (BITS_PER_LONG == 64)
        pr_info("%05d  0x%px   %pa   %9ld\n",
#else
        pr_info("%05d  0x%px   %pa   %7ld\n",
#endif
                i, k_addr_start + (i * PAGE_SIZE), &paddr, pfn);
        if (!!contiguity_check)
            prev_pfn = pfn;
    }

    return;
}

static int32_t __bsa_allocator(void)
{
    uint64_t alloc_page_count = 0;
    const struct page *pg_ptr = NULL;

    pr_info("0. Show identity mapping: RAM page frames : kernel virtual pages :: 1:1\n"
            "(PAGE_SIZE = %ld bytes)\n",
            PAGE_SIZE);

    pr_info("[--------- show_phy_pages() output follows:\n");
    __show_phy_pages((void *)PAGE_OFFSET, 5 * PAGE_SIZE, 1);
    pr_info(" --------- show_phy_pages() output done]\n");

    /*1. Allocate one page with the __get_free_page() */
    k_addr1 = (void *)__get_free_page(GFP_KERNEL);
    if (!k_addr1) {
        pr_err(MODULE_TAG
               "Failed to allocate one page with __get_free_page()\n");
        goto err_1;
    }
    pr_info("#.    BSA/PA API     Amt alloc'ed        KVA\n");
    pr_info("1.  __get_free_page()     1 page    %px\n", k_addr1);

    /*2. Allocate 2^order pages whit the __get_free_pages() */
    alloc_page_count = powerof(2, __k_addr2_order);
    k_addr2 =
            (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, __k_addr2_order);
    if (!k_addr2) {
        pr_err(MODULE_TAG
               "Failed to allocate %llu pages with __get_free_pages()\n",
               alloc_page_count);
        goto err_2;
    }
    pr_info("2. __get_free_pages()  2^%d page(s)  %px\n", __k_addr2_order,
            k_addr2);
    pr_info("[--------- show_phy_pages() output follows:\n");
    __show_phy_pages(k_addr2, alloc_page_count * PAGE_SIZE, 1);
    pr_info(" --------- show_phy_pages() output done]\n");

    /*3. Allocate and init one page with the get_zeroed_page() */
    k_addr3 = (void *)get_zeroed_page(GFP_KERNEL);
    if (!k_addr3) {
        pr_err(MODULE_TAG
               "Failed to allocate one page with get_zeroed_page()\n");
        goto err_3;
    }
    pr_info("3.   get_zeroed_page()     1 page    %px\n", k_addr3);

    /*4. Allocate one page with the alloc_page() API, 如果需要使用虚拟地址，需要调用 page_address() 进行转换*/
    pg_ptr = alloc_page(GFP_KERNEL | __GFP_ZERO);
    if (!pg_ptr) {
        pr_err(MODULE_TAG "Failed to allocate one page with alloc_page()\n");
        goto err_4;
    }
    k_addr4 = page_address(pg_ptr);
    pr_info("4.       alloc_page()   1 page      %px\n"
            " (struct page addr = %px)\n",
            (void *)k_addr4, pg_ptr);

    /*5. Allocate 2^5 = 32 pages with the alloc_pages() API*/
    k_addr5 = page_address(alloc_pages(GFP_KERNEL | __GFP_ZERO, 5));
    if (!k_addr5) {
        pr_err(MODULE_TAG "Failed to allocate 32 pages with alloc_pages()\n");
        goto err_5;
    }
    pr_info("5.      alloc_pages()  %lld pages     %px\n", powerof(2, 5),
            (void *)k_addr5);

    return 0;

err_5:
    free_page((unsigned long)k_addr4);
err_4:
    free_page((unsigned long)k_addr3);
err_3:
    free_pages((unsigned long)k_addr2, __k_addr2_order);
err_2:
    free_page((unsigned long)k_addr1);
err_1:
    return -ENOMEM;
}

static int __init __cw_lowlevel_mem_test_init(void)
{
    pr_info(MODULE_TAG "Initializing lowlevel memory test module\n");

    __bsa_allocator();

    return 0;
}

static void __exit __cw_lowlevel_mem_test_exit(void)
{
    free_page((unsigned long)k_addr1);
    free_pages((unsigned long)k_addr2, __k_addr2_order);
    free_page((unsigned long)k_addr3);
    free_page((unsigned long)k_addr4);
    free_pages((unsigned long)k_addr5, 5);
    pr_info(MODULE_TAG "Exiting lowlevel memory test module\n");
}

module_init(__cw_lowlevel_mem_test_init);
module_exit(__cw_lowlevel_mem_test_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("calmwu <wubo0067@hotmail.com>");
MODULE_DESCRIPTION("cw_lowlevel_mem_test - A test module for lowlevel memory");
MODULE_VERSION("0.0.1");