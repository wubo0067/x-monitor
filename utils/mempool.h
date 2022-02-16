/*
 * @Author: CALM.WU
 * @Date: 2022-01-24 10:28:21
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-01-25 11:24:51
 */

#pragma once

#include "common.h"

#define FLEX_ARRAY /* empty */
#define XM_DEFAULT_INIT_MEM_UNIT_COUNT 100
#define XM_DEFAULT_GROW_MEM_UNIT_COUNT 100

#ifdef __cplusplus
extern "C" {
#endif

struct xm_mempool_s;

struct xm_mempool_s *xm_mempool_init(uint32_t unit_size, int32_t init_mem_unit_count,
                                     int32_t grow_mem_unit_count);

void xm_mempool_fini(struct xm_mempool_s *pool);

void *xm_mempool_malloc(struct xm_mempool_s *pool);

int32_t xm_mempool_free(struct xm_mempool_s *pool, void *pfree);

void xm_print_mempool_info(struct xm_mempool_s *pool);

void xm_print_mempool_block_info_by_pointer(struct xm_mempool_s *pool, void *pblock);

#define XM_MEMPOOL_INIT(type)                                             \
    (xm_mempool_init(sizeof(struct type), XM_DEFAULT_INIT_MEM_UNIT_COUNT, \
                     XM_DEFAULT_GROW_MEM_UNIT_COUNT))

#ifdef __cplusplus
}
#endif