/*
 * @Author: CALM.WU
 * @Date: 2022-04-14 11:22:17
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-04-14 17:22:46
 */

#include "filter_rule.h"

#include "utils/clocks.h"
#include "utils/compiler.h"
#include "utils/log.h"
#include "utils/regex.h"

#include "appconfig/appconfig.h"

/**
 * It reads the configuration file and gets the app filter rules.
 *
 * @param config_path the path of the configuration file, example: collector_plugin_apps
 *
 * @return A pointer to a struct app_filter_rules.
 */
struct app_filter_rules *get_filter_rules(const char *config_path) {
    int32_t                  enable = 0;
    const char              *app_type = NULL;
    const char              *filter_content_type = NULL;
    struct app_filter_rule  *afr = NULL;
    struct app_filter_rules *rules = NULL;

    // 获取配置文件中的app过滤规则
    config_setting_t *cpa_cs = appconfig_lookup(config_path);
    if (unlikely(!cpa_cs)) {
        error("config lookup path:'%s' failed", config_path);
        return NULL;
    }

    int32_t cpa_elem_count = config_setting_length(cpa_cs);
    debug("config path:'%s' elem size:%d", config_path, cpa_elem_count);

    if (unlikely(0 == cpa_elem_count)) {
        return NULL;
    }

    rules = calloc(1, sizeof(struct app_filter_rules));
    if (unlikely(!rules)) {
        error("calloc app_filter_rule_list failed");
        return NULL;
    }

    INIT_LIST_HEAD(&rules->rule_list);

    for (int32_t index = 0; index < cpa_elem_count; index++) {
        config_setting_t *elem = config_setting_get_elem(cpa_cs, index);
        if (unlikely(!elem)) {
            error("config lookup path:'%s' %d elem failed", config_path, index);
            goto failed;
        }

        int16_t     elem_type = config_setting_type(elem);
        const char *elem_name = config_setting_name(elem);
        if (!strncmp("app_", elem_name, 4) && config_setting_is_group(elem)) {
            config_setting_lookup_bool(elem, "enable", &enable);
            config_setting_lookup_string(elem, "type", &app_type);
            config_setting_lookup_string(elem, "filter_content_type", &filter_content_type);

            debug("config path:'%s' %d elem type:%d, name:%s, enable:%s, "
                  "app_type:%s, filter_content_type:%s",
                  config_path, index, elem_type, elem_name, enable ? "true" : "false", app_type,
                  filter_content_type);
        }
    }

    goto successed;

failed:
    if (unlikely(!rules)) {
        free(rules);
        rules = NULL;
    }
successed:
    return rules;
}

/**
 * It iterates through a linked list of rules, and sets a is_matched in each rule to 0
 *
 * @param rules the list of rules to be cleaned
 */
void clean_filter_rules(struct app_filter_rules *rules) {
    struct list_head       *iter = NULL;
    struct app_filter_rule *pr = NULL;

    if (likely(rules)) {
        __list_for_each(iter, &rules->rule_list) {
            pr = list_entry(iter, struct app_filter_rule, l_rule);
            if (unlikely(!pr)) {
                continue;
            }
            pr->is_matched = 0;
        }
    }
}

/**
 * It's a function that frees a linked list of structs
 *
 * @param rules the pointer to the struct app_filter_rules
 */
void free_filter_rules(struct app_filter_rules *rules) {
    struct list_head       *iter = NULL;
    struct app_filter_rule *pr = NULL;

    if (likely(rules)) {
    redo:
        __list_for_each(iter, &rules->rule_list) {
            pr = list_entry(iter, struct app_filter_rule, l_rule);
            if (unlikely(!pr)) {
                continue;
            }

            for (int32_t i = 0; i < pr->key_count; i++) {
                free((pr->keys)[i]);
                (pr->keys)[i] = NULL;
            }

            list_del(&pr->l_rule);
            free(pr);
            pr = NULL;
            goto redo;
        }
    }
}