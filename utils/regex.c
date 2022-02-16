/*
 * @Author: CALM.WU 
 * @Date: 2021-11-25 10:34:55 
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-11-25 10:36:57
 */

#include "regex.h"
#include "common.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

struct xm_regex {
    pcre2_code *compiled;
};

int32_t regex_create(struct xm_regex **rep, const char *regex)
{
    int32_t          error;
    struct xm_regex *re;
    PCRE2_SIZE       error_offset;

    if (regex == NULL) {
        *rep = NULL;
        return 0;
    }

    re = calloc(1, sizeof(*re));
    if (re == NULL) {
        perror("calloc");
        return -1;
    }

    re->compiled = pcre2_compile((PCRE2_SPTR8)regex, PCRE2_ZERO_TERMINATED, 0,
                                 &error, &error_offset, NULL);

    if (re->compiled == NULL) {
        PCRE2_UCHAR buffer[256];
        pcre2_get_error_message(error, buffer, sizeof(buffer));
        fprintf(stderr, "PCRE2 compilation failed at offset %ld: %s\n",
                error_offset, buffer);
        return -1;
    }

    *rep = re;

    return 0;
}

bool regex_match(struct xm_regex *re, const char *s)
{
    int32_t           error;
    pcre2_match_data *match;

    if (re == NULL) {
        return true;
    }

    match = pcre2_match_data_create_from_pattern(re->compiled, NULL);

    error =
        pcre2_match(re->compiled, (PCRE2_SPTR8)s, strlen(s), 0, 0, match, NULL);

    pcre2_match_data_free(match);

    return error < 0 ? false : true;
}