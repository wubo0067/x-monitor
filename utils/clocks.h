/*
 * @Author: CALM.WU 
 * @Date: 2021-10-14 14:34:11 
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-10-15 10:55:53
 */

#pragma once

#include "common.h"

typedef uint64_t usec_t;

// 纳秒
#ifndef NSEC_PER_SEC
#define NSEC_PER_SEC 1000000000ULL
#endif

// 微秒
#ifndef USEC_PER_SEC
#define USEC_PER_SEC 1000000ULL // 一秒钟的微秒数
#endif
#ifndef NSEC_PER_USEC
#define NSEC_PER_USEC 1000ULL // 一微秒的纳秒数
#endif

// 毫秒
#ifndef MSEC_PER_SEC
#define MSEC_PER_SEC 1000ULL
#endif
struct heartbeat {
    // 单调时间，字面意思是单调时间，实际上它指的是系统启动以后流逝的时间，
    // 这是由变量jiffies来记录的。系统每次启动时jiffies初始化为0，每来一个timer interrupt，jiffies加1，也就是说它代表系统启动后流逝的tick数
    usec_t monotonic;
    // 实际上就是指的是现实的时间，这是由变量xtime来记录的。系统每次启动时将CMOS上的RTC时间读入xtime，这个值是"自1970-01-01起经历的秒数
    usec_t realtime;
};

//
extern time_t now_realtime_sec();
extern usec_t now_realtime_usec();

extern time_t now_monotonic_sec();
extern usec_t now_monotonic_usec();

extern void heartbeat_init(struct heartbeat *hb);
// 返回自上一次心跳以来经过的时间（以微秒为单位）
extern usec_t heartbeat_next(struct heartbeat *hb, usec_t tick);

/*
CLOCK_REALTIME：可设定的系统级实时时钟。用于度量真实时间。
CLOCK_MONOTONIC ：不可设定的恒定态时钟。系统启动后就不会发生改变。
CLOCK_PROCESS_CPUTIME_ID：每进程CPU时间的时钟。测量调用进程所消耗的用户和系统CPU时间。
CLOCK_THREAD_CPUTIME_ID：每线程CPU时间的时钟。测量调用线程所消耗的用户和系统CPU时间。
CLOCK_MONOTONIC_RAW：提供了对纯基于硬件时间的访问。不受NTP时间调整的影响。
CLOCK_REALTIME_COARSE：类似于CLOCK_REALTIME，适用于希望以最小代价获取较低分辨率时间戳的程序。返回值分辨率为jiffy。
CLOCK_MONOTONIC_COARSE：类似于CLOCK_MONOTONIC，适用于希望以最小代价获取较低分辨率时间戳的程序。返回值分辨率为jiffy。
*/
extern void test_clock_monotonic_coarse();

// sleep for 微秒
extern int32_t sleep_usec(usec_t usec);