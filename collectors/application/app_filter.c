/*
 * @Author: CALM.WU
 * @Date: 2022-04-14 11:22:17
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-04-14 17:22:46
 */

#include "app_filter.h"

#include "utils/clocks.h"
#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/regex.h"

#include "appconfig/appconfig.h"

struct int32_t get_filter_rules(const char *config_path, struct app_filter_rule **rules) {
    int32_t                 rule_count = 0;
    struct app_filter_rule *filter_rules = NULL;

    // 获取配置文件中的app过滤规则

    *rules = filter_rules;
    return rule_count;
}

void clean_filter_rules(struct app_filter_rule *rules, int32_t count) {
    if (likely(rules && count > 0)) {
        for (int32_t i = 0; i < count; i++) {
            rules[i].is_matched = 0;
        }
    }
}

void free_filter_rules(struct app_filter_rule *rules, int32_t count) {
    if (likely(rules && count > 0)) {
        for (int32_t i = 0; i < count; i++) {
            struct app_filter_rule *rule = &rules[i];
            for (int32_t j = 0; j < rule->key_count; j++) {
                free((void *)rule->keys[j]);
            }
        }
        free(rules);
    }
}