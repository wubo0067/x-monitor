/*
 * @Author: CALM.WU
 * @Date: 2022-04-13 15:18:43
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-04-13 16:39:37
 */

#include "plugin_app.h"

#include "routine.h"
#include "utils/clocks.h"
#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/mountinfo.h"
#include "utils/simple_pattern.h"

#include "appconfig/appconfig.h"

static const char *__name = "PLUGIN_APP";
static const char *__config_name = "collector_plugin_app";

struct collector_app {
    int32_t           exit_flag;
    pthread_t         thread_id;   // routine执行的线程ids
    int32_t           update_every;
    int32_t           check_for_new_mountinfos_every;
    SIMPLE_PATTERN   *excluded_mountpoints;
    SIMPLE_PATTERN   *excluded_filesystems;
    struct mountinfo *disk_mountinfo_root;
};

static struct collector_diskspace __collector_diskspace = {
    .exit_flag = 0,
    .thread_id = 0,
    .update_every = 1,
    .check_for_new_mountinfos_every = 15,
    .disk_mountinfo_root = NULL,
};

int32_t application_routine_init() {
}

void *application_routine_start(void *arg) {
}

void application_routine_stop() {
}