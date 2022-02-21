/*
 * @Author: CALM.WU
 * @Date: 2022-02-21 11:09:55
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-02-21 17:15:13
 */

// https://github.com/moooofly/MarkSomethingDown/blob/master/Linux/TCP%20%E7%9B%B8%E5%85%B3%E7%BB%9F%E8%AE%A1%E4%BF%A1%E6%81%AF%E8%AF%A6%E8%A7%A3.md

#include "prometheus-client-c/prom.h"

#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/strings.h"
#include "utils/clocks.h"

#include "appconfig/appconfig.h"

static const char       *__proc_netstat_filename = "/proc/net/netstat";
static struct proc_file *__pf_netstat = NULL;

int32_t init_collector_proc_netstat() {
    int32_t ret = 0;
    return ret;
}

int32_t collector_proc_netstat(int32_t update_every, usec_t dt, const char *config_path) {
    int32_t ret = 0;
    return ret;
}

void fini_collector_proc_netstat() {
}
