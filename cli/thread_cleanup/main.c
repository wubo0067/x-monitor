/*
 * @Author: CALM.WU 
 * @Date: 2021-10-28 10:24:25 
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-12-20 14:44:43
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>

void cleaner(void *arg)
{
    printf("cleaner:%s\n", (char *)arg);
}

void *thread1(void *arg)
{
    printf("thread 1 begin\n");
    pthread_cleanup_push(cleaner, "thread 1 11 handler");
    pthread_cleanup_push(cleaner, "thread 1 22 handler");
    printf("thread 1 push ok \n");
    if (arg) {
        printf("thread 1 return 1\n");
        return ((void *)1);
    }
    // 非0的pop会调用cleanup_push方法
    pthread_cleanup_pop(1);
    pthread_cleanup_pop(1);
    return ((void *)3);
}

int32_t main(int32_t argc, char **argv)
{
    pthread_t tid;
    void     *ret;
    int32_t   err;

    err = pthread_create(&tid, NULL, thread1, NULL);
    if (err != 0) {
        printf("can't create thread:%s\n", strerror(err));
        exit(1);
    }
    err = pthread_join(tid, &ret);
    if (err != 0) {
        printf("can't join thread:%s\n", strerror(err));
        exit(1);
    }
    printf("thread 1 return value: %d\n", (int32_t)ret);
    return 0;
}