/*
 * @Author: CALM.WU
 * @Date: 2022-04-14 11:22:17
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-04-14 17:22:46
 */

#include "apps_filter_rule.h"

#include "utils/clocks.h"
#include "utils/compiler.h"
#include "utils/log.h"
#include "utils/regex.h"
#include "utils/strings.h"
#include "utils/files.h"

#include "appconfig/appconfig.h"

static int32_t __generate_rules(const char *source, const char *app_type_name,
                                const char *filter_regex_pattern, int32_t appname_match_index,
                                const char *additional_keys_str, const char *app_assign_pids_type,
                                struct app_filter_rules *rules) {
    char **filter_file_list = NULL;

    if (!source || !filter_regex_pattern || appname_match_index < 1 || !rules) {
        return -1;
    }
    struct xm_regex *re = NULL;
    int32_t          rc = 0;
    FILE            *fp_filter = NULL;

    // 解析source，用,分隔多个文件
    filter_file_list = strsplit(source, ",");
    if (likely(filter_file_list)) {

        for (int32_t i = 0; filter_file_list[i]; i++) {

            const char *filter_file = filter_file_list[i];
            debug("[PLUGIN_APPSTATUS] filter source-%d: '%s'", i, filter_file);
            // 判断文件是否存在
            if (likely(file_exists(filter_file))) {

                // 文件存在, 打开文件，按行过滤匹配
                fp_filter = fopen(filter_file, "r");
                if (unlikely(!fp_filter)) {
                    error("[PLUGIN_APPSTATUS] open filter source-%d '%s' failed", i, filter_file);
                    continue;
                }

                char   *line = NULL;
                size_t  line_size = 0;
                ssize_t read_size = 0;

                // 创建匹配规则
                if (unlikely(0 != regex_create(&re, filter_regex_pattern))) {
                    error("[PLUGIN_APPSTATUS] create regex failed by filter_regex_pattern: %s",
                          filter_regex_pattern);
                    fclose(fp_filter);
                    continue;
                }

                // 对文件进行行匹配
                while ((read_size = getline(&line, &line_size, fp_filter)) != -1) {
                    line[read_size - 1] = '\0';

                    // 匹配app_name和keys
                    rc = regex_match_values(re, line);
                    if (unlikely(rc < 3)) {
                        continue;
                    }

                    if (unlikely(appname_match_index > rc)) {
                        error("[PLUGIN_APPSTATUS] appname_match_index: %d exceeded matching "
                              "results count: %d",
                              appname_match_index, rc);
                        continue;
                    }

                    debug("[PLUGIN_APPSTATUS] '%s' regex match count: %d", line, rc);

                    // 构造filter_rule
                    struct app_process_filter_rule *rule =
                        calloc(1, sizeof(struct app_process_filter_rule));
                    if (unlikely(!rule)) {
                        error("[PLUGIN_APPSTATUS] calloc struct app_process_filter_rule failed");
                        continue;
                    }

                    strlcpy(rule->app_type_name, app_type_name, XM_APP_NAME_SIZE);
                    strlcpy(rule->app_name, re->values[appname_match_index], XM_APP_NAME_SIZE);

                    // TODO: 把附加key都解析出来吧
                    // strlcpy(rule->additional_key_str, additional_keys_str,
                    //         XM_APP_NAME_SIZE);
                    if (0 == strcmp(app_assign_pids_type, "match-keys-for-pid_and_ppid")) {
                        rule->assign_type = APP_ASSIGN_PIDS_KEYS_MATCH_PID_AND_PPID;
                    } else {
                        rule->assign_type = APP_ASSIGN_PIDS_KEYS_MATCH_PID;
                    }

                    size_t additional_key_count = 0;
                    char **additional_keys =
                        strsplit_count(additional_keys_str, ",", &additional_key_count);

                    // 匹配出key数量, 排除自己和appname
                    rule->keys = (char **)calloc(rc - 2 + additional_key_count, sizeof(char *));
                    for (int32_t l = 1; l < rc; l++) {
                        if (unlikely(l == appname_match_index)) {
                            continue;
                        }
                        rule->keys[rule->key_count] = strdup(re->values[l]);
                        rule->key_count++;
                    }

                    // 添加附加key
                    if (likely(additional_keys && additional_key_count > 0)) {
                        for (size_t t = 0; t < additional_key_count; t++) {
                            rule->keys[rule->key_count] = strdup(additional_keys[t]);
                            rule->key_count++;
                        }
                    }

                    if (likely(additional_keys)) {
                        free(additional_keys);
                        additional_keys = NULL;
                    }

                    // 加入链表
                    INIT_LIST_HEAD(&rule->l_member);
                    list_add(&rule->l_member, &rules->rule_list_head);
                    rules->rule_count++;
                    warn("[PLUGIN_APPSTATUS] add %d filter rule for app_type '%s', app '%s', "
                         "key_count: %d",
                         rules->rule_count, rule->app_type_name, rule->app_name, rule->key_count);
                }

                if (likely(re)) {
                    regex_destroy(re);
                    re = NULL;
                }

                free(line);
                fclose(fp_filter);
            } else {
                warn("[PLUGIN_APPSTATUS] filter file '%s' not exists", filter_file);
            }
        }
    }

    free(filter_file_list);
    return rules->rule_count;
}

/**
 * It reads the configuration file and gets the app filter rules.
 *
 * @param config_path the path of the configuration file, example: collector_plugin_apps
 *
 * @return A pointer to a struct app_filter_rules.
 */
struct app_filter_rules *create_filter_rules(const char *config_path) {
    int32_t                  enable = 0;
    const char              *app_type_name = NULL;
    const char              *filter_source = NULL;
    const char              *filter_regex_pattern = NULL;
    const char              *additional_keys_str = NULL;
    int32_t                  appname_match_index = 1;
    const char              *app_assign_pids_type = NULL;
    struct app_filter_rules *rules = NULL;

    // 获取配置文件中的app过滤规则
    config_setting_t *cs = appconfig_lookup(config_path);
    if (unlikely(!cs)) {
        error("[PLUGIN_APPSTATUS] config lookup path:'%s' failed", config_path);
        return NULL;
    }

    int32_t cs_elem_count = config_setting_length(cs);
    debug("[PLUGIN_APPSTATUS] config path:'%s' elem size:%d", config_path, cs_elem_count);

    if (unlikely(0 == cs_elem_count)) {
        return NULL;
    }

    // 构造规则链表
    rules = calloc(1, sizeof(struct app_filter_rules));
    if (unlikely(!rules)) {
        error("[PLUGIN_APPSTATUS] calloc app_filter_rule_list failed");
        return NULL;
    }

    INIT_LIST_HEAD(&rules->rule_list_head);

    for (int32_t index = 0; index < cs_elem_count; index++) {
        config_setting_t *elem = config_setting_get_elem(cs, index);
        if (unlikely(!elem)) {
            error("[PLUGIN_APPSTATUS] config lookup path:'%s' %d elem failed", config_path, index);
            goto failed;
        }

        const char *elem_name = config_setting_name(elem);
        if (!strncmp("app_", elem_name, 4) && config_setting_is_group(elem)) {
            config_setting_lookup_bool(elem, "enable", &enable);
            config_setting_lookup_string(elem, "type", &app_type_name);
            config_setting_lookup_string(elem, "filter_sources", &filter_source);
            config_setting_lookup_string(elem, "filter_regex_pattern", &filter_regex_pattern);
            config_setting_lookup_int(elem, "appname_match_index", &appname_match_index);
            config_setting_lookup_string(elem, "additional_keys_str", &additional_keys_str);
            config_setting_lookup_string(elem, "app_assign_pids_type", &app_assign_pids_type);

            // 开始构造规则，从文件中过滤出appname和关键字
            __generate_rules(filter_source, app_type_name, filter_regex_pattern,
                             appname_match_index, additional_keys_str, app_assign_pids_type, rules);
        }
    }
    debug("[PLUGIN_APPSTATUS] app_filter_rules size:%d", rules->rule_count);
    return rules;

failed:
    if (unlikely(!rules)) {
        free(rules);
        rules = NULL;
    }
    return NULL;
}

/**
 * It iterates through a linked list of rules, and sets a is_matched in each rule to 0
 *
 * @param rules the list of rules to be cleaned
 */
void clean_filter_rules(struct app_filter_rules *rules) {
    struct list_head               *iter = NULL;
    struct app_process_filter_rule *rule = NULL;

    if (likely(rules)) {
        __list_for_each(iter, &rules->rule_list_head) {
            rule = list_entry(iter, struct app_process_filter_rule, l_member);
            if (unlikely(!rule)) {
                continue;
            }
            rule->is_matched = 0;
        }
    }
}

/**
 * It's a function that frees a linked list of structs
 *
 * @param rules the pointer to the struct app_filter_rules
 * https://www.cs.uic.edu/~hnagaraj/articles/linked-list/ 删除链表所有元素
 */
void free_filter_rules(struct app_filter_rules *rules) {
    struct list_head               *iter = NULL;
    struct app_process_filter_rule *rule = NULL;

    if (likely(rules)) {
    redo:
        __list_for_each(iter, &rules->rule_list_head) {
            rule = list_entry(iter, struct app_process_filter_rule, l_member);
            if (unlikely(!rule)) {
                continue;
            }

            free(rule->keys);
            list_del(&rule->l_member);
            free(rule);
            rule = NULL;
            goto redo;
        }
    }
}