/*
 * @Author: CALM.WU 
 * @Date: 2021-10-15 10:20:32 
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-11-19 10:42:10
 */

#pragma once

#include <pthread.h>
#include <stdint.h>

extern int32_t xm_mutex_init(pthread_mutex_t *mutex);
extern int32_t xm_mutex_lock(pthread_mutex_t *mutex);
extern int32_t xm_mutex_trylock(pthread_mutex_t *mutex);
extern int32_t xm_mutex_unlock(pthread_mutex_t *mutex);

extern int32_t xm_rwlock_init(pthread_rwlock_t *rwlock);
extern int32_t xm_rwlock_destroy(pthread_rwlock_t *rwlock);
extern int32_t xm_rwlock_rdlock(pthread_rwlock_t *rwlock);
extern int32_t xm_rwlock_wrlock(pthread_rwlock_t *rwlock);
extern int32_t xm_rwlock_unlock(pthread_rwlock_t *rwlock);
extern int32_t xm_rwlock_tryrdlock(pthread_rwlock_t *rwlock);
extern int32_t xm_rwlock_trywrlock(pthread_rwlock_t *rwlock);