/*
 * @Author: CALM.WU
 * @Date: 2021-11-25 10:34:55
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-11-25 10:36:57
 */

#include "regex.h"
#include "common.h"
#include "log.h"
#include "compiler.h"

static void regex_free_values(struct xm_regex *re) {
    if (NULL == re) {
        return;
    }

    if (NULL != re->values && re->count > 0) {
        for (size_t i = 0; i < re->count; ++i) {
            if (NULL != re->values[i]) {
                free(re->values[i]);
            }
        }

        free(re->values);
    }
}

int32_t regex_create(struct xm_regex **repp, const char *pattern) {
    int32_t          error;
    struct xm_regex *re;
    PCRE2_SIZE       error_offset;

    if (pattern == NULL) {
        *repp = NULL;
        return 0;
    }

    re = calloc(1, sizeof(*re));
    if (re == NULL) {
        error("calloc struct xm_regex failed");
        return -1;
    }

    re->compiled =
        pcre2_compile((PCRE2_SPTR8)pattern, PCRE2_ZERO_TERMINATED, 0, &error, &error_offset, NULL);

    if (re->compiled == NULL) {
        PCRE2_UCHAR buffer[256];
        pcre2_get_error_message(error, buffer, sizeof(buffer));
        error("PCRE2 compilation failed at offset %ld: %s\n", error_offset, buffer);
        return -1;
    }

    *repp = re;

    return 0;
}

bool regex_match(struct xm_regex *re, const char *s) {
    int32_t           error;
    pcre2_match_data *match;

    if (re == NULL) {
        return true;
    }

    match = pcre2_match_data_create_from_pattern(re->compiled, NULL);

    error = pcre2_match(re->compiled, (PCRE2_SPTR8)s, strlen(s), 0, 0, match, NULL);

    pcre2_match_data_free(match);

    return error < 0 ? false : true;
}

int32_t regex_match_values(struct xm_regex *re, const char *subject) {
    if (unlikely(NULL == re)) {
        return -1;
    }

    int32_t           rc = 0;
    pcre2_match_data *match_data = NULL;
    PCRE2_SIZE       *ovector = NULL;
    PCRE2_SIZE        subject_length = (PCRE2_SIZE)strlen(subject);

    regex_free_values(re);

    match_data = pcre2_match_data_create_from_pattern(re->compiled, NULL);

    rc = pcre2_match(re->compiled, (PCRE2_SPTR8)subject, (PCRE2_SIZE)subject_length, 0, 0,
                     match_data, NULL);
    if (unlikely(rc < 0)) {
        pcre2_match_data_free(match_data);
        if (PCRE2_ERROR_NOMATCH == rc) {
            error("No match");
        } else {
            error("Matching error %d", rc);
        }
        return -1;
    }

    ovector = pcre2_get_ovector_pointer(match_data);
    debug("Match successed at offset %d", (int32_t)ovector[0]);

    re->count = rc;
    re->values = (char **)calloc(rc, sizeof(char *));

    for (int32_t i = 0; i < rc; i++) {
        PCRE2_SIZE start = ovector[2 * i];
        PCRE2_SIZE end = ovector[2 * i + 1];
        (re->values)[i] = strndup(subject + start, end - start);
    }

    return rc;
}

void regex_destroy(struct xm_regex *re) {
    if (unlikely(re == NULL)) {
        return;
    }

    pcre2_code_free(re->compiled);
    regex_free_values(re);
    free(re);
    re = NULL;
}