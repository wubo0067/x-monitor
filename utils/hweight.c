/*
 * @Author: calmwu
 * @Date: 2022-05-04 10:13:52
 * @Last Modified by: calmwu
 * @Last Modified time: 2022-05-04 12:06:36
 */

#include "hweight.h"
#include "compiler.h"

#ifndef __HAVE_ARCH_SW_HWEIGHT
uint32_t hweight32(uint32_t w) {
#ifdef CONFIG_ARCH_HAS_FAST_MULTIPLIER
    w -= (w >> 1) & 0x55555555;
    w = (w & 0x33333333) + ((w >> 2) & 0x33333333);
    w = (w + (w >> 4)) & 0x0f0f0f0f;
    return (w * 0x01010101) >> 24;
#else
    uint32_t res = w - ((w >> 1) & 0x55555555);
    res = (res & 0x33333333) + ((res >> 2) & 0x33333333);
    res = (res + (res >> 4)) & 0x0F0F0F0F;
    res = res + (res >> 8);
    return (res + (res >> 16)) & 0x000000FF;
#endif
}
#endif

uint32_t hweight16(uint32_t w) {
    uint32_t res = w - ((w >> 1) & 0x5555);
    res = (res & 0x3333) + ((res >> 2) & 0x3333);
    res = (res + (res >> 4)) & 0x0F0F;
    return (res + (res >> 8)) & 0x00FF;
}

uint32_t hweight8(uint32_t w) {
    uint32_t res = w - ((w >> 1) & 0x55);
    res = (res & 0x33) + ((res >> 2) & 0x33);
    return (res + (res >> 4)) & 0x0F;
}

#ifndef __HAVE_ARCH_SW_HWEIGHT
uint32_t hweight64(uint64_t w) {
#if LONG_BITS == 32
    return __sw_hweight32((uint32_t)(w >> 32)) + __sw_hweight32((uint32_t)w);
#elif LONG_BITS == 64
#ifdef CONFIG_ARCH_HAS_FAST_MULTIPLIER
    w -= (w >> 1) & 0x5555555555555555ul;
    w = (w & 0x3333333333333333ul) + ((w >> 2) & 0x3333333333333333ul);
    w = (w + (w >> 4)) & 0x0f0f0f0f0f0f0f0ful;
    return (w * 0x0101010101010101ul) >> 56;
#else
    uint64_t res = w - ((w >> 1) & 0x5555555555555555ul);
    res = (res & 0x3333333333333333ul) + ((res >> 2) & 0x3333333333333333ul);
    res = (res + (res >> 4)) & 0x0F0F0F0F0F0F0F0Ful;
    res = res + (res >> 8);
    res = res + (res >> 16);
    return (res + (res >> 32)) & 0x00000000000000FFul;
#endif
#endif
}
#endif