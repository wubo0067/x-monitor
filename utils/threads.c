/*
 * @Author: CALM.WU 
 * @Date: 2021-10-15 10:27:25 
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-10-15 10:28:17
 */

#include "threads.h"
#include "common.h"
#include "log.h"
#include "compiler.h"

// ----------------------------------------------------------------------------
// mutex
int32_t xm_mutex_init(pthread_mutex_t *mutex)
{
    int32_t ret = 0;
    if (mutex == NULL) {
        return -1;
    }
    ret = pthread_mutex_init(mutex, NULL);
    if (ret != 0) {
        error("pthread_mutex_init failed, ret:%d\n", ret);
        return -1;
    }
    return ret;
}

int32_t xm_mutex_lock(pthread_mutex_t *mutex)
{
    int32_t ret = 0;
    if (mutex == NULL) {
        return -1;
    }
    ret = pthread_mutex_lock(mutex);
    if (ret != 0) {
        error("pthread_mutex_lock failed, ret:%d\n", ret);
        return -1;
    }
    return ret;
}

int32_t xm_mutex_trylock(pthread_mutex_t *mutex)
{
    int32_t ret = 0;
    if (mutex == NULL) {
        return -1;
    }
    ret = pthread_mutex_trylock(mutex);
    if (ret != 0) {
        error("pthread_mutex_trylock failed, ret:%d\n", ret);
        return -1;
    }
    return ret;
}

int32_t xm_mutex_unlock(pthread_mutex_t *mutex)
{
    int32_t ret = 0;
    if (mutex == NULL) {
        return -1;
    }
    ret = pthread_mutex_unlock(mutex);
    if (ret != 0) {
        error("pthread_mutex_unlock failed, ret:%d\n", ret);
        return -1;
    }
    return ret;
}

// ----------------------------------------------------------------------------
// rwlock
int32_t xm_rwlock_init(pthread_rwlock_t *rwlock)
{
    int32_t ret = 0;
    if (rwlock == NULL) {
        return -1;
    }
    ret = pthread_rwlock_init(rwlock, NULL);
    if (ret != 0) {
        error("pthread_rwlock_init failed, ret:%d\n", ret);
        return -1;
    }
    return ret;
}

int32_t xm_rwlock_destroy(pthread_rwlock_t *rwlock)
{
    int32_t ret = 0;
    if (rwlock == NULL) {
        return -1;
    }
    ret = pthread_rwlock_destroy(rwlock);
    if (ret != 0) {
        error("pthread_rwlock_destroy failed, ret:%d\n", ret);
        return -1;
    }
    return ret;
}

int32_t xm_rwlock_rdlock(pthread_rwlock_t *rwlock)
{
    int32_t ret = 0;
    if (rwlock == NULL) {
        return -1;
    }
    ret = pthread_rwlock_rdlock(rwlock);
    if (ret != 0) {
        error("pthread_rwlock_rdlock failed, ret:%d\n", ret);
        return -1;
    }
    return ret;
}

int32_t xm_rwlock_wrlock(pthread_rwlock_t *rwlock)
{
    int32_t ret = 0;
    if (rwlock == NULL) {
        return -1;
    }
    ret = pthread_rwlock_wrlock(rwlock);
    if (ret != 0) {
        error("pthread_rwlock_wrlock failed, ret:%d\n", ret);
        return -1;
    }
    return ret;
}

int32_t xm_rwlock_unlock(pthread_rwlock_t *rwlock)
{
    int32_t ret = 0;
    if (rwlock == NULL) {
        return -1;
    }
    ret = pthread_rwlock_unlock(rwlock);
    if (ret != 0) {
        error("pthread_rwlock_unlock failed, ret:%d\n", ret);
        return -1;
    }
    return ret;
}

int32_t xm_rwlock_tryrdlock(pthread_rwlock_t *rwlock)
{
    int32_t ret = 0;
    if (rwlock == NULL) {
        return -1;
    }
    ret = pthread_rwlock_tryrdlock(rwlock);
    if (ret != 0) {
        error("pthread_rwlock_tryrdlock failed, ret:%d\n", ret);
        return -1;
    }
    return ret;
}

int32_t xm_rwlock_trywrlock(pthread_rwlock_t *rwlock)
{
    int32_t ret = 0;
    if (rwlock == NULL) {
        return -1;
    }
    ret = pthread_rwlock_trywrlock(rwlock);
    if (ret != 0) {
        error("pthread_rwlock_trywrlock failed, ret:%d\n", ret);
        return -1;
    }
    return ret;
}