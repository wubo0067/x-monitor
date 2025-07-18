/*
 * @Author: CALM.WU
 * @Date: 2025-07-17 18:00:59
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2025-07-17 18:03:48
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "utils.h"

uint64_t powerof(int32_t base, int32_t exponent)
{
    uint64_t ret = 1;
    if (base == 0)
        return 0;
    if (base <= 0 || exponent < 0)
        return -1UL;
    if (exponent == 0)
        return 1;

    while (exponent--) {
        ret *= base;
    }
    return ret;
}
