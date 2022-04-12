/*
 * @Author: CALM.WU
 * @Date: 2021-12-23 11:43:29
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-12-23 17:13:37
 */

#include "utils/common.h"
#include "utils/log.h"
#include "utils/regex.h"
#include "utils/strings.h"
#include "utils/compiler.h"

int32_t main(int32_t argc, char **argv) {
    if (log_init("../cli/regex_cli/log.cfg", "regex_cli") != 0) {
        fprintf(stderr, "log init failed\n");
        return -1;
    }

    if (unlikely(argc != 3)) {
        fatal("./regex_cli <pattern> <subject>\n");
        return -1;
    }

    const char *pattern = argv[1];
    const char *subject = argv[2];

    debug("subject: '%s' pattern: '%s'\n", pattern, subject);

    struct xm_regex *re = NULL;

    if (unlikely(0 != regex_create(&re, pattern))) {
        goto cleanup;
    }

    int32_t rc = regex_match_values(re, subject);
    debug("match count: %d\n", rc);

    if (NULL != re->values && re->count > 0) {
        for (size_t i = 0; i < re->count; ++i) {
            debug("%lu: %s", i + 1, re->values[i]);
        }
    }

cleanup:
    if (NULL != re) {
        regex_destroy(re);
    }

    log_fini();

    return 0;
}