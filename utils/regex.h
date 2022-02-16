/*
 * @Author: CALM.WU 
 * @Date: 2021-11-25 10:32:17 
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-11-25 10:47:48
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct xm_regex;

int  regex_create(struct xm_regex **rep, const char *regex);
bool regex_match(struct xm_regex *re, const char *s);

#ifdef __cplusplus
}
#endif