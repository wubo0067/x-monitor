/*
 * @Author: CALM.WU
 * @Date: 2021-10-12 11:15:36
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-01-18 11:38:58
 */

#include "common.h"
#include "compiler.h"
#include "log.h"

zlog_category_t *g_log_cat = NULL;

static pthread_mutex_t __log_mutex = PTHREAD_MUTEX_INITIALIZER;

static inline void __log_lock(void) {
    pthread_mutex_lock(&__log_mutex);
}

static inline void __log_unlock(void) {
    pthread_mutex_unlock(&__log_mutex);
}

int32_t log_init(const char *log_config_file, const char *log_category_name) {
    int32_t ret = 0;

    if (NULL == g_log_cat) {
        __log_lock();
        if (NULL == g_log_cat) {
            if (zlog_init(log_config_file)) {
                fprintf(stderr, "zlog init failed. config file:%s\n", log_config_file);
                ret = -1;
                goto init_error;
            }

            g_log_cat = zlog_get_category(log_category_name);
            if (unlikely(NULL == g_log_cat)) {
                fprintf(stderr, "zlog get category failed. category name:%s\n", log_category_name);
                ret = -2;
                goto init_error;
            }
        }
        __log_unlock();
    }
    return ret;

init_error:
    __log_unlock();
    if (-2 == ret) {
        zlog_fini();
    }
    return ret;
}

void log_fini() {
    __log_lock();
    if (likely(NULL != g_log_cat)) {
        zlog_fini();
        g_log_cat = NULL;
    }
    __log_unlock();
}