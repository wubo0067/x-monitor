/*
 * @Author: CALM.WU
 * @Date: 2021-12-23 11:43:29
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-12-23 17:13:37
 */

#include "utils/common.h"
#include "utils/log.h"
#include "utils/simple_pattern.h"
#include "utils/strings.h"

void test_match(const char *pattern, const char *str) {
    SIMPLE_PATTERN *sp = simple_pattern_create(pattern, NULL, SIMPLE_PATTERN_EXACT);

    // simple_pattern_dump(sp);

    if (simple_pattern_matches(sp, str)) {
        debug("+++ '%s' matches with pattern: '%s'\n", str, pattern);
    }
    else {
        debug("---'%s' mismatches with pattern: '%s'\n", str, pattern);
    }

    simple_pattern_free(sp);
}

int32_t main(int32_t argc, char **argv) {
    char *log_cfg = argv[1];

    if (log_init(log_cfg, "simplepattern_teset") != 0) {
        fprintf(stderr, "log init failed\n");
        return -1;
    }

    test_match("*foobar* !foo* !*bar *", "foo");
    test_match("*foobar* foo !*bar *", "foo");
    test_match("*foobar* foo !*bar *", "foobar");
    test_match("!foobar foo !*bar *", "foobar");
    test_match("!*foobar foo !*bar *", "csdfoobar");
    test_match("*", "csdfoobar");
    // mismatch
    test_match("!c*", "csdfoobar");
    // mismatch
    test_match("!*c*", "sdsdcsdfoobar");
    // match
    test_match("/proc/* /sys/* /var/run/user/* /run/user/* /snap/* /var/lib/docker/*", "/sys/fs/cgroup/");

    log_fini();

    return 0;
}