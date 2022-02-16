/*
 * @Author: CALM.WU
 * @Date: 2022-01-25 11:02:43
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-01-25 17:24:49
 */

#include "utils/common.h"
#include "utils/log.h"
#include "utils/mempool.h"

struct __test {
    int32_t a;
    int32_t b;
    int32_t c;
};

static int32_t rand_comparison(const void *a, const void *b) {
    (void)a;
    (void)b;

    return rand() % 2 ? +1 : -1;
}

static void shuffle(void *base, size_t nmemb, size_t size) {
    qsort(base, nmemb, size, rand_comparison);
}

static void __test_malloc_1(struct xm_mempool_s *xmp) {
    void *mem = xm_mempool_malloc(xmp);

    xm_print_mempool_block_info_by_pointer(xmp, mem);

    xm_mempool_free(xmp, mem);

    xm_print_mempool_block_info_by_pointer(xmp, mem);
}

static void __test_malloc_2(struct xm_mempool_s *xmp) {
    void *mem;

    for (int32_t i = 0; i < 13; i++) {
        mem = xm_mempool_malloc(xmp);
    }

    xm_print_mempool_block_info_by_pointer(xmp, mem);
    xm_print_mempool_info(xmp);
}

static void __test_malloc_3(struct xm_mempool_s *xmp) {
    void *  mems[10];
    int32_t i = 0;

    debug("^^^^mempool malloc 10 units");
    for (i = 0; i < 10; i++) {
        mems[i] = xm_mempool_malloc(xmp);
    }

    xm_print_mempool_block_info_by_pointer(xmp, mems[9]);

    debug("^^^^mempool free 10 units");
    srand(0);
    shuffle(mems, 10, sizeof(void *));

    for (i = 9; i >= 0; i--) {
        xm_mempool_free(xmp, mems[i]);
    }

    debug("^^^^mempool malloc 10 units");
    for (i = 0; i < 10; i++) {
        mems[i] = xm_mempool_malloc(xmp);
    }

    xm_print_mempool_info(xmp);
}

static void __test_malloc_4(struct xm_mempool_s *xmp) {
    void *  mems[13];
    int32_t i = 0;

    debug("^^^^mempool malloc 13 units");
    for (i = 0; i < 13; i++) {
        mems[i] = xm_mempool_malloc(xmp);
    }

    xm_print_mempool_info(xmp);

    debug("^^^^mempool free 11-13 units");
    xm_mempool_free(xmp, mems[10]);
    xm_mempool_free(xmp, mems[11]);
    xm_mempool_free(xmp, mems[12]);

    xm_print_mempool_info(xmp);
}

int32_t main(int32_t argc, char **argv) {
    if (log_init("../cli/mempool_test/log.cfg", "mempool_test") != 0) {
        fprintf(stderr, "log init failed\n");
        return -1;
    }

    debug("mempool_test");

    struct xm_mempool_s *xmp = xm_mempool_init(sizeof(struct __test), 10, 10);

    // xm_print_mempool_info(xmp);

    //__test_malloc_1(xmp);

    //__test_malloc_3(xmp);

    // __test_malloc_2(xmp);

    __test_malloc_4(xmp);

    xm_mempool_fini(xmp);

    log_fini();

    return 0;
}