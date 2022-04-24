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
#include "utils/files.h"
#include "appconfig/appconfig.h"
#include "collectors/application/apps_filter_rule.h"

int32_t main(int32_t argc, char **argv) {
    if (log_init("../cli/log.cfg", "config_test") != 0) {
        fprintf(stderr, "log init failed\n");
        return -1;
    }

    if (unlikely(argc != 2)) {
        fatal("./config_test <config-file-fullpath>\n");
        return -1;
    }

    const char *config_file = argv[1];

    debug("config_file: '%s'", config_file);

    if (!file_exists(config_file)) {
        fatal("config file '%s' not exists", config_file);
        return -1;
    }

    if (unlikely(appconfig_load(config_file) < 0)) {
        fatal("load config file '%s' failed", config_file);
        return -1;
    }

    config_setting_t *cs = appconfig_lookup("collector_plugin_apps");
    if (unlikely(!cs)) {
        fatal("config lookup path:collector_plugin_apps failed");
        return -1;
    }

    debug("-------------config lookup path:collector_plugin_apps ok!-------------");

    // config_write(cs->config, stdout);

    uint32_t elem_count = config_setting_length(cs);
    debug("path:collector_plugin_apps include dir:%s, type:%d elem size:%d",
          config_get_include_dir(cs->config), config_setting_type(cs), elem_count);

    int32_t     enable = 0;
    const char *app_type_name = NULL;
    const char *filter_sources = NULL;
    const char *additional_keys_str = NULL;

    for (int32_t index = 0; index < elem_count; ++index) {
        config_setting_t *elem = config_setting_get_elem(cs, index);
        if (unlikely(!elem)) {
            error("config lookup path:collector_plugin_apps  %d elem failed", index);
            break;
        }

        // 判断类型
        int16_t     elem_type = config_setting_type(elem);
        const char *elem_name = config_setting_name(elem);

        if (!strncmp("app_", elem_name, 4) && config_setting_is_group(elem)) {
            config_setting_lookup_bool(elem, "enable", &enable);
            config_setting_lookup_string(elem, "type", &app_type_name);
            config_setting_lookup_string(elem, "filter_sources", &filter_sources);
            config_setting_lookup_string(elem, "additional_keys_str", &additional_keys_str);

            debug("config path:collector_plugin_apps %d elem type:%d, name:%s, enable:%s, "
                  "app_type_name:%s, filter_sources:%s, additional_keys_str:'%s'",
                  index, elem_type, elem_name, enable ? "true" : "false", app_type_name,
                  filter_sources, additional_keys_str);
        } else {
            debug("config path:collector_plugin_apps %d elem type:%d name: '%s'", index, elem_type,
                  elem_name);
        }
    }

    debug("-------------test create app filter rules!-------------");

    struct app_filter_rules *rules = create_filter_rules("collector_plugin_apps");
    if (unlikely(!rules)) {
        error("create_filter_rules failed");
    }

    if (likely(rules)) {
        struct list_head               *iter = NULL;
        struct app_process_filter_rule *rule = NULL;

        __list_for_each(iter, &rules->rule_list) {
            rule = list_entry(iter, struct app_process_filter_rule, l_member);

            debug("app_type_name:'%s' assign_type:%d, app_name:'%s', key_count:%d",
                  rule->app_type_name, rule->assign_type, rule->app_name, rule->key_count);
            for (int32_t i = 0; i < rule->key_count; ++i) {
                debug("\t%d key:'%s'", i, rule->keys[i]);
            }
        }

        free_filter_rules(rules);
    }

    appconfig_destroy();
    log_fini();

    return 0;
}