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

    config_setting_t *cs = appconfig_lookup("collector_app");
    if (unlikely(!cs)) {
        fatal("config lookup path:collector_app failed");
        return -1;
    }

    debug("-------------config lookup path:collector_app ok!-------------");

    // config_write(cs->config, stdout);

    uint32_t elem_count = config_setting_length(cs);
    debug("path:collector_app include dir:%s, type:%d elem size:%d",
          config_get_include_dir(cs->config), config_setting_type(cs), elem_count);

    for (int32_t index = 0; index < elem_count; ++index) {
        config_setting_t *elem = config_setting_get_elem(cs, index);
        if (unlikely(!elem)) {
            error("config lookup path:collector_app  %d elem failed", index);
            break;
        }

        // 判断类型
        int16_t elem_type = config_setting_type(elem);
        debug("index:%d elem type:%d name: '%s'", index, elem_type, config_setting_name(elem));
    }

    appconfig_destroy();
    log_fini();

    return 0;
}