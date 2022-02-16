/*
 * @Author: CALM.WU 
 * @Date: 2021-10-27 16:45:19 
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-12-20 14:52:24
 */

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

void cleaner(void *arg) {
    printf("cleaner: %s\n", (char *)arg);
}

void *mythread(void *arg) {
    // 设置本线程对信号的反应
    /*
    类型有两种，只有在PTHREAD_CANCEL_ENABLE状态下有效
    PTHREAD_CANCEL_ASYNCHRONOUS 立即执行取消信号
    PTHREAD_CANCEL_DEFERRED 运行到下一个取消点
    */
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    /*
    PTHREAD_CANCEL_ENABLE 默认，收到cancel信号马上设置退出状态。
    PTHREAD_CANCEL_DISABLE 收到cancel信号继续运行。
    */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    /*
    void pthread_testcancel(void);
    当线程取消功能处于启用状态且取消状态设置为延迟状态时，pthread_testcancel()函数有效。如果在取消功能处处于禁用状态下调用pthread_testcancel()，则该函数不起作用。
    请务必仅在线程取消线程操作安全的序列中插入pthread_testcancel()。除通过pthread_testcancel()调用以编程方式建立的取消点意外，pthread标准还指定了几个取消点
    */

    pthread_cleanup_push(cleaner, "thread cleanup push");

    while (1) {
        // printf系统调用可引起阻塞，是系统默认的取消点
        printf("thread is running\n");
        // 设置取消点
        // pthread_testcancel();
        sleep(1);
    }
    // 这个执行完后，后面的代码不再执行
    pthread_cleanup_pop(1);

    printf("thread is not running\n");
    sleep(2);
}

int main() {
    pthread_t t1;
    int       err;
    err = pthread_create(&t1, NULL, mythread, NULL);
    // 调用cancel后，while(1)中printf会被中断，线程在此退出
    sleep(3);
    printf("cancel t1 thread before\n");
    pthread_cancel(t1);
    printf("cancel t1 thread after\n");
    pthread_join(t1, NULL);
    sleep(3);
    printf("Main thread is exit\n");
    return 0;
}