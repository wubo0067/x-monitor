/*
 * @Author: CALM.WU
 * @Date: 2022-04-20 15:00:02
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-04-20 17:52:26
 */

#include "apps_stat.h"

#include "utils/compiler.h"
#include "utils/log.h"
#include "utils/regex.h"
#include "utils/strings.h"

#include "collectors/process/process_stat.h"

// app_stat列表
static LIST_HEAD(__app_stat_list);
// app关联的进程列表
static LIST_HEAD(__app_process_list);

int32_t collect_apps(struct app_filter_rules *afr) {
    return 0;
}

// 统计应用的资源数据
int32_t collect_apps_usage() {
    return 0;
}

void free_apps_collector()() {
}