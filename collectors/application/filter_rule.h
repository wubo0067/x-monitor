/*
 * @Author: CALM.WU
 * @Date: 2022-04-14 11:22:09
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-04-14 17:12:18
 */

#pragma once

#include "utils/common.h"
#include "utils/consts.h"
#include "utils/list.h"

enum app_assign_pids_type {
    APP_ASSIGN_PIDS_KEYINCMDLINE = 0,
    APP_ASSIGN_PIDS_KEYINCMDLINE_AND_PPID
};

struct app_filter_rule {
    struct list_head l_rule;

    const char                app_type_name[XM_CONFIG_MEMBER_NAME_SIZE];
    enum app_assign_pids_type assign_type;

    char  **keys;         // 多个匹配key
    int32_t key_count;    // 匹配key的个数
    int32_t is_matched;   // 是否匹配过
};

struct app_filter_rules {
    struct list_head rule_list;
    int16_t          rule_count;
};

// 通过配置文件生成app实例的过滤规则，返回规则的个数
extern struct app_filter_rules *get_filter_rules(const char *config_path);

extern void clean_filter_rules(struct app_filter_rules *rules);

extern void free_filter_rules(struct app_filter_rules *rules);
