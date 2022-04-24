/*
 * @Author: CALM.WU
 * @Date: 2022-04-24 14:41:27
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-04-24 15:07:26
 */

// https://github.com/localvoid/cxx-benchmark-count-digits
// https://tomsworkspace.github.io/2021/02/27/GCC%E8%87%AA%E5%B8%A6%E7%9A%84%E4%B8%80%E4%BA%9Bbuiltin%E5%86%85%E5%BB%BA%E5%87%BD%E6%95%B0/
// https://github.com/localvoid/cxx-benchmark-itoa/blob/master/src/2pass_2b_3.cpp

#include "numbers.h"
#include <string.h>

static const uint64_t powers_of_10[] = { 0,
                                         10,
                                         100,
                                         1000,
                                         10000,
                                         100000,
                                         1000000,
                                         10000000,
                                         100000000,
                                         1000000000,
                                         10000000000,
                                         100000000000,
                                         1000000000000,
                                         10000000000000,
                                         100000000000000,
                                         1000000000000000,
                                         10000000000000000,
                                         100000000000000000,
                                         1000000000000000000,
                                         10000000000000000000U };

static const uint32_t powers_of_10_u32[] = { 0,      10,      100,      1000,      10000,
                                             100000, 1000000, 10000000, 100000000, 1000000000 };

static const char digit_pairs[201] = { "00010203040506070809"
                                       "10111213141516171819"
                                       "20212223242526272829"
                                       "30313233343536373839"
                                       "40414243444546474849"
                                       "50515253545556575859"
                                       "60616263646566676869"
                                       "70717273747576777879"
                                       "80818283848586878889"
                                       "90919293949596979899" };

uint32_t digits10_uint64(uint64_t n) {
    uint64_t t = (64 - __builtin_clzll(n | 1)) * 1233 >> 12;
    return t - (n < powers_of_10[t]) + 1;
}

uint32_t digits10_uint32(uint32_t n) {
    uint32_t t = (32 - __builtin_clz(n | 1)) * 1233 >> 12;
    return t - (n < powers_of_10_u32[t]) + 1;
}

uint32_t digits10_uint16(uint16_t v) {
    uint32_t n = v;
    uint32_t t = (32 - __builtin_clz(n | 1)) * 1233 >> 12;
    return t - (n < powers_of_10_u32[t]) + 1;
}

uint32_t digits10_uint8(uint8_t v) {
    uint32_t n = v;
    uint32_t t = (32 - __builtin_clz(n | 1)) * 1233 >> 12;
    return t - (n < powers_of_10_u32[t]) + 1;
}

uint32_t uint64_2_str(uint64_t v, char *const dst) {
    const uint32_t size = digits10_uint64(v);
    char          *c = &dst[size - 1];

    while (v >= 100) {
        const int32_t r = v % 100;
        v /= 100;
        memcpy(c - 1, digit_pairs + 2 * r, 2);
        c -= 2;
    }
    if (v < 10) {
        *c = '0' + v;
    } else {
        memcpy(c - 1, digit_pairs + 2 * v, 2);
    }
    return size;
}