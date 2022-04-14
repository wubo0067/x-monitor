/*
 * @Author: CALM.WU
 * @Date: 2022-04-14 11:22:09
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-04-14 17:12:18
 */

#pragma once

enum app_assign_pids_type {
    APP_ASSIGN_PIDS_KEYINCMDLINE = 0,
    APP_ASSIGN_PIDS_KEYINCMDLINE_AND_PPID
};

struct app_filter_rule {
    enum app_assign_pids_type assign_type;
    const char *const        *keys;         // 多个匹配key
    int32_t                   key_count;    // 匹配key的个数
    int32_t                   is_matched;   // 是否匹配过
};

// 通过配置文件获取app实例的过滤规则
extern int32_t get_filter_rules(const char *config_path, struct app_filter_rule **rules);

extern void clean_filter_rules(struct app_filter_rule *rules, int32_t count);

extern void free_filter_rules(struct app_filter_rule *rules, int32_t count);
