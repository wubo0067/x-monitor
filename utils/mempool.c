/*
 * @Author: CALM.WU
 * @Date: 2022-01-24 14:47:41
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-01-25 17:31:23
 */

#include "compiler.h"
#include "log.h"
#include "mempool.h"

#define XM_ALIGNMENT_SIZE sizeof(uint64_t)

struct xm_mempool_block_s {
    uint32_t block_size;              // The size of the memory block(), in bytes
    uint32_t free_unit_count;         // The number of free units in the block
    uint32_t free_unit_pos;           // The sequence number of the free unit, starting from 0
    struct xm_mempool_block_s *next;  // The next block
    char                       data[FLEX_ARRAY];  // The data
};

struct xm_mempool_s {
    pthread_spinlock_t lock;

    uint32_t                   unit_size;             // The size of the unit, in bytes
    int32_t                    init_mem_unit_count;   // The number of memory units to initialize
    int32_t                    grow_mem_unit_count;   // The number of memory units to grow
    int32_t                    curr_mem_block_count;  // The number of memory blocks currently
    struct xm_mempool_block_s *root;                  // The root block
};

static struct xm_mempool_block_s *xm_memblock_create(uint32_t unit_size, int32_t unit_count) {
    struct xm_mempool_block_s *block = NULL;

    block = (struct xm_mempool_block_s *)malloc(sizeof(struct xm_mempool_block_s)
                                                + unit_size * unit_count);

    if (unlikely(NULL == block)) {
        error("malloc xm_mempool_block_s failed");
        return NULL;
    }

    block->next = NULL;
    // 只有malloc调用该函数，第一个unit已经分配出去了，所以free_unit_pos = 1, free_unit_count也减一
    block->free_unit_pos   = 1;
    block->free_unit_count = unit_count - 1;
    block->block_size      = unit_size * unit_count;

    char *offset = block->data;
    for (int32_t i = 1; i < unit_count + 1; i++) {
        // example, unit[0].next_freepos = 1
        *((int32_t *)offset) = i;
        offset += unit_size;
        debug("set mem block unit[%d].next_freepos = %d", i - 1, i);
    }
    return block;
}

struct xm_mempool_s *xm_mempool_init(uint32_t unit_size, int32_t init_mem_unit_count,
                                     int32_t grow_mem_unit_count) {
    struct xm_mempool_s *pool = NULL;

    if (unlikely(init_mem_unit_count <= 0 || grow_mem_unit_count <= 0)) {
        error("invalid init_mem_unit_count(%d) or grow_mem_unit_count(%d)", init_mem_unit_count,
              grow_mem_unit_count);
        return NULL;
    }

    pool = (struct xm_mempool_s *)calloc(1, sizeof(struct xm_mempool_s));
    if (unlikely(NULL == pool)) {
        error("calloc failed");
        return NULL;
    }

    // unit_size必须大于sizeof(int32_t)，这里存放下一个free unit的下标
    // 这个空间是复用的，空闲时记录空闲下标，分配时全部可用
    /* round up to a 'XM_ALIGNMENT_SIZE' alignment */
    pool->unit_size            = ROUNDUP(unit_size, XM_ALIGNMENT_SIZE);
    pool->curr_mem_block_count = 0;
    pool->init_mem_unit_count  = init_mem_unit_count;
    pool->grow_mem_unit_count  = grow_mem_unit_count;
    pool->root                 = NULL;
    pthread_spin_init(&pool->lock, PTHREAD_PROCESS_SHARED);

    debug("mempool init ok! unit_size: %d, init_mem_unit_count: %d, grow_mem_unit_count: %d",
          pool->unit_size, pool->init_mem_unit_count, pool->grow_mem_unit_count);

    return pool;
}

void xm_mempool_fini(struct xm_mempool_s *pool) {
    struct xm_mempool_block_s *block = NULL;
    struct xm_mempool_block_s *temp  = NULL;

    if (unlikely(NULL == pool || NULL == pool->root)) {
        return;
    }

    block = pool->root;
    while (block) {
        temp  = block;
        block = block->next;
        free(temp);
    }

    pthread_spin_destroy(&pool->lock);
    free(pool);
    pool = NULL;

    debug("mempool fini ok!");
}

void *xm_mempool_malloc(struct xm_mempool_s *pool) {
    struct xm_mempool_block_s *block   = NULL;
    void *                     ret_ptr = NULL;

    if (unlikely(NULL == pool)) {
        error("pool is NULL");
        return NULL;
    }

    pthread_spin_lock(&pool->lock);

    block = pool->root;

    if (unlikely(NULL == block)) {
        block = xm_memblock_create(pool->unit_size, pool->init_mem_unit_count);
        if (unlikely(NULL == block)) {
            pthread_spin_unlock(&pool->lock);
            error("xm_memblock_create failed");
            return NULL;
        }
        debug("alloc_unit_pos: 0, next_alloc_unit_pos: %d block->free_unit_count: %d",
              block->free_unit_pos, block->free_unit_count);
        pool->root = block;
        pool->curr_mem_block_count++;
        ret_ptr = (void *)block->data;
        pthread_spin_unlock(&pool->lock);
        return ret_ptr;
    }

    // 查找一个有空闲unit的block
    while (block && block->free_unit_count == 0) {
        block = block->next;
    }

    if (likely(block)) {
        // 找到一个空闲unit在block中
        uint32_t curr_free_pos = block->free_unit_pos;
        ret_ptr                = (void *)((char *)block->data + curr_free_pos * pool->unit_size);
        block->free_unit_pos   = *((int32_t *)ret_ptr);
        block->free_unit_count--;
        debug("alloc_unit_pos: %d, next_alloc_unit_pos: %d block->free_unit_count: %d",
              curr_free_pos, block->free_unit_pos, block->free_unit_count);
        pthread_spin_unlock(&pool->lock);
        return ret_ptr;
    }

    // 没有空闲unit的block，创建新的block
    debug("no free unit in block, create new block");
    block = xm_memblock_create(pool->unit_size, pool->grow_mem_unit_count);
    if (unlikely(NULL == block)) {
        pthread_spin_unlock(&pool->lock);
        error("xm_memblock_create failed");
        return NULL;
    }
    debug("alloc_unit_pos: 0, next_alloc_unit_pos: %d block->free_unit_count: %d",
          block->free_unit_pos, block->free_unit_count);
    // 加入到链表头部
    block->next = pool->root;
    pool->root  = block;
    pool->curr_mem_block_count++;
    ret_ptr = (void *)block->data;
    pthread_spin_unlock(&pool->lock);
    return ret_ptr;
}

int32_t xm_mempool_free(struct xm_mempool_s *pool, void *pfree) {
    struct xm_mempool_block_s *block      = NULL;
    struct xm_mempool_block_s *temp_block = NULL;

    if (unlikely(NULL == pool || NULL == pfree)) {
        error("pool or pfree is NULL");
        return -1;
    }

    pthread_spin_lock(&pool->lock);

    block = pool->root;

    // 根据pfree找到对应的block
    while (block
           && (block->data > ((char *)pfree)
               || ((char *)pfree) >= (char *)(block->data + block->block_size))) {
        temp_block = block;
        block      = block->next;
    }

    if (unlikely(NULL == block)) {
        error("pfree: %p is not in the mempool", pfree);
        pthread_spin_unlock(&pool->lock);
        return -1;
    }

    uint32_t before_recycling_free_unit_count = block->free_unit_count;
    // block开始回收
    block->free_unit_count++;
    // 设置pfree的next_free_unit_pos
    *((uint32_t *)pfree) = block->free_unit_pos;
    // 设置block的free_unit_pos为pfree的下标
    block->free_unit_pos = ((char *)pfree - block->data) / (pool->unit_size);
    debug("before_recycling_free_unit_count: %u, pfree.free_unit_pos: %u block.free_unit_count: "
          "%u, block.free_unit_pos: %u",
          before_recycling_free_unit_count, *((int32_t *)pfree), block->free_unit_count,
          block->free_unit_pos);

    // 判断是否回收block
    if (block->block_size == block->free_unit_count * pool->unit_size) {
        // 如果block完全空闲
        if (pool->curr_mem_block_count > 1 && pool->root->free_unit_count) {
            // 如果pool有多个block，且第一个block有空闲unit
            if (block == pool->root) {
                // 如果要回收的block是第一个block
                pool->root = block->next;
            } else {
                temp_block->next = block->next;
            }
            debug("recycling block: %p", block);
            free(block);
            pool->curr_mem_block_count--;
        }
    } else if (pool->root->free_unit_count == 0) {
        // 如果pool的第一个block没有空闲unit，这把这个block放到pool的第一个block
        temp_block->next = block->next;
        block->next      = pool->root;
        pool->root       = block;
    }

    pfree = NULL;

    pthread_spin_unlock(&pool->lock);
    return 0;
}

void xm_print_mempool_info(struct xm_mempool_s *pool) {
    struct xm_mempool_block_s *block = NULL;
    int32_t                    i     = 0;

    if (unlikely(NULL == pool)) {
        error("pool is NULL");
        return;
    }

    pthread_spin_lock(&pool->lock);
    debug("-----------------mempool info-----------------");
    debug("*\tunit_size = %u\n*\tcurr_mem_block_count = %d\n*\tinit_mem_unit_count = %d\n*\t"
          "grow_mem_unit_count = %d",
          pool->unit_size, pool->curr_mem_block_count, pool->init_mem_unit_count,
          pool->grow_mem_unit_count);
    debug("-----------------------------------------------");

    block = pool->root;
    if (likely(NULL != block)) {
        for (; block != NULL; block = block->next) {
            debug("-----------------mempool block info[%d]-----------------", i++);
            debug("*\tblock = %p\n*\tblock_size = %u\n*\tfree_unit_count = %u\n*\tfree_unit_pos = "
                  "%u\n*\tdata = %p",
                  block, block->block_size, block->free_unit_count, block->free_unit_pos,
                  block->data);
            debug("-----------------------------------------------");
        }
    }

    pthread_spin_unlock(&pool->lock);
}

void xm_print_mempool_block_info_by_pointer(struct xm_mempool_s *pool, void *ptr) {
    struct xm_mempool_block_s *block = NULL;

    if (unlikely(NULL == pool)) {
        error("pool is NULL");
        return;
    }

    if (unlikely(NULL == ptr)) {
        error("pblock is NULL");
        return;
    }

    pthread_spin_lock(&pool->lock);

    block = pool->root;

    // 根据pfree找到对应的block
    while (block
           && (block->data > ((char *)ptr)
               || ((char *)ptr) >= (char *)(block->data + block->block_size))) {
        block = block->next;
    }

    if (unlikely(NULL == block)) {
        error("pblock: %p is not in the mempool", ptr);
        pthread_spin_unlock(&pool->lock);
        return;
    } else {
        debug("### ptr[%p] in memblock[%p] ###", ptr, block->data);
        debug("-----------------mempool block info-----------------");
        debug("*\tblock = %p\n*\tblock_size = %u\n*\tfree_unit_count = %u\n*\tfree_unit_pos = "
              "%u\n*\tdata = %p",
              block, block->block_size, block->free_unit_count, block->free_unit_pos, block->data);
        debug("-----------------------------------------------");
    }

    pthread_spin_unlock(&pool->lock);
}