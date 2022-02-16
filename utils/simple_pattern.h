/*
 * @Author: CALM.WU
 * @Date: 2021-12-23 11:26:44
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-12-23 16:57:10
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

enum SIMPLE_PREFIX_MODE {
    SIMPLE_PATTERN_EXACT,
    SIMPLE_PATTERN_PREFIX,
    SIMPLE_PATTERN_SUFFIX,
    SIMPLE_PATTERN_SUBSTRING
};

typedef void SIMPLE_PATTERN;

// create a simple_pattern from the string given
// default_mode is used in cases where EXACT matches, without an asterisk,
// should be considered PREFIX matches.
extern SIMPLE_PATTERN *simple_pattern_create(const char *list, const char *separators,
                                             enum SIMPLE_PREFIX_MODE default_mode);

// test if string str is matched from the pattern and fill 'wildcarded' with the parts matched by '*'
extern int simple_pattern_matches_extract(SIMPLE_PATTERN *list, const char *str, char *wildcarded,
                                          size_t wildcarded_size);

// test if string str is matched from the pattern
#define simple_pattern_matches(list, str) simple_pattern_matches_extract(list, str, NULL, 0)

// free a simple_pattern that was created with simple_pattern_create()
// list can be NULL, in which case, this does nothing.
extern void simple_pattern_free(SIMPLE_PATTERN *list);

extern void  simple_pattern_dump(SIMPLE_PATTERN *p);
extern int   simple_pattern_is_potential_name(SIMPLE_PATTERN *p);
extern char *simple_pattern_iterate(SIMPLE_PATTERN **p);

// Auxiliary function to create a pattern
char *simple_pattern_trim_around_equal(char *src);