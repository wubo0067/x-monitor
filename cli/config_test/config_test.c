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

    debug("-------------test create app filter rules!-------------");

    struct app_filter_rules *rules = create_filter_rules("collector_plugin_appstatus");

    if (likely(rules)) {
        struct list_head               *iter = NULL;
        struct app_process_filter_rule *rule = NULL;

        __list_for_each(iter, &rules->rule_list_head) {
            rule = list_entry(iter, struct app_process_filter_rule, l_member);

            debug("app_type_name:'%s' assign_type:%d, app_name:'%s', key_count:%d",
                  rule->app_type_name, rule->assign_type, rule->app_name, rule->key_count);
            for (int32_t i = 0; i < rule->key_count; ++i) {
                debug("\t%d key:'%s'", i, rule->keys[i]);
            }
        }

        free_filter_rules(rules);
    } else {
        error("create_filter_rules failed");
    }

    appconfig_destroy();
    log_fini();

    return 0;
}