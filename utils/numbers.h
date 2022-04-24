/*
 * @Author: CALM.WU
 * @Date: 2022-04-24 14:40:41
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-04-24 15:05:47
 */

#pragma once

#include <stdint.h>

extern uint32_t digits10_uint64(uint64_t n);
extern uint32_t digits10_uint32(uint32_t n);
extern uint32_t digits10_uint16(uint16_t n);
extern uint32_t digits10_uint8(uint8_t n);

extern uint32_t uint64_2_str(uint64_t v, char *const dst);