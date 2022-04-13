/*
 * @Author: CALM.WU
 * @Date: 2021-10-14 14:34:44
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-10-15 10:55:56
 */

#include "clocks.h"
#include "compiler.h"
#include "log.h"

static int32_t __clock_monotonic_coarse_valid = 1;

static inline time_t now_sec(clockid_t clk_id) {
    struct timespec ts;
    if (unlikely(clock_gettime(clk_id, &ts) == -1)) {
        return 0;
    }
    return ts.tv_sec;
}

// 返回当前时间，单位微秒
static inline usec_t now_usec(clockid_t clk_id) {
    struct timespec ts;
    if (unlikely(clock_gettime(clk_id, &ts) == -1)) {
        error("clock_gettime clk_id:%d failed", clk_id);
        return 0;
    }
    return ts.tv_sec * USEC_PER_SEC + (ts.tv_nsec % NSEC_PER_SEC) / NSEC_PER_USEC;
}

// 返回1970-01-01起经历的秒数
time_t now_realtime_sec() {
    return now_sec(CLOCK_REALTIME);
}

// 返回1970-01-01起经历的微秒数
usec_t now_realtime_usec() {
    return now_usec(CLOCK_REALTIME);
}

time_t now_monotonic_sec() {
    return now_sec(likely(__clock_monotonic_coarse_valid) ? CLOCK_MONOTONIC_COARSE :
                                                            CLOCK_MONOTONIC);
}

usec_t now_monotonic_usec() {
    return now_usec(likely(__clock_monotonic_coarse_valid) ? CLOCK_MONOTONIC_COARSE :
                                                             CLOCK_MONOTONIC);
}

void heartbeat_init(struct heartbeat *hb) {
    hb->monotonic = hb->realtime = 0;
}

// 返回两次heartbeat之间的时间差，单位微秒，用realtime clock，两次hb可能跨过了一个tick周期
usec_t heartbeat_next(struct heartbeat *hb, usec_t tick) {
    struct heartbeat now;
    // 使用jiffies时间
    now.monotonic = now_monotonic_usec();
    now.realtime = now_realtime_usec();

    // 下一次心跳时间，必须是tick(毫秒)整数倍
    usec_t next_monotonic = now.monotonic - (now.monotonic % tick) + tick;

    // sleep到下一次心跳时间
    while (now.monotonic < next_monotonic) {
        sleep_usec(next_monotonic - now.monotonic);
        now.monotonic = now_monotonic_usec();
        now.realtime = now_realtime_usec();
    }

    if (likely(hb->realtime != 0ULL)) {
        usec_t dt_monotonic = now.monotonic - hb->monotonic;
        usec_t dt_realtime = now.realtime - hb->realtime;

        hb->monotonic = now.monotonic;
        hb->realtime = now.realtime;

        if (unlikely(dt_monotonic >= tick + tick / 2)) {
            errno = 0;
            error("heartbeat missed %lu monotonic microseconds", dt_monotonic - tick);
        }

        return dt_realtime;
    } else {
        hb->monotonic = now.monotonic;
        hb->realtime = now.realtime;
    }
    return 0;
}

void test_clock_monotonic_coarse() {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC_COARSE, &ts) == -1 && errno == EINVAL)
        __clock_monotonic_coarse_valid = 0;
}

int32_t sleep_usec(usec_t usec) {
    struct timespec rem, req = { .tv_sec = (time_t)(usec / 1000000),
                                 .tv_nsec = (suseconds_t)((usec % 1000000) * 1000) };

    while (nanosleep(&req, &rem) == -1) {
        if (likely(errno == EINTR)) {
            debug("nanosleep() interrupted (while sleeping for %lu microseconds).", usec);
            req.tv_sec = rem.tv_sec;
            req.tv_nsec = rem.tv_nsec;
        } else {
            error("Cannot nanosleep() for %lu microseconds.", usec);
            break;
        }
    }

    return 0;
}

/**
 * Convert a date and time to a number of seconds since the Unix epoch
 *
 * @param year0 The year.
 * @param mon0 Month of the year (1-12)
 * @param day the day of the month (1-31)
 * @param hour The hour of the day.
 * @param min The number of minutes after the hour.
 * @param sec Seconds since midnight.
 *
 * @return The number of seconds since 1970-01-01 00:00:00.
 */
int64_t mktime64(const uint32_t year0, const uint32_t mon0, const uint32_t day, const uint32_t hour,
                 const uint32_t min, const uint32_t sec) {
    uint32_t mon = mon0, year = year0;

    /* 1..12 -> 11,12,1..10 */
    if (0 >= (int)(mon -= 2)) {
        mon += 12; /* Puts Feb last since it has leap day */
        year -= 1;
    }

    return ((((int64_t)(year / 4 - year / 100 + year / 400 + 367 * mon / 12 + day) + year * 365
              - 719499)
                 * 24
             + hour /* now have hours - midnight tomorrow handled here */
             ) * 60
            + min /* now have minutes */
            ) * 60
           + sec; /* finally seconds */
}