/*
 * @Author: CALM.WU
 * @Date: 2021-11-25 10:32:17
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-11-25 10:47:48
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#ifdef __cplusplus
extern "C" {
#endif

struct xm_regex {
    pcre2_code *compiled;
    char      **values;
    size_t      count;
};

int32_t regex_create(struct xm_regex **repp, const char *pattern);

bool regex_match(struct xm_regex *re, const char *s);

int32_t regex_match_values(struct xm_regex *re, const char *subject);

void regex_destroy(struct xm_regex *re);

#ifdef __cplusplus
}
#endif