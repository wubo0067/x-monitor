/*
 * @Author: calmwu
 * @Date: 2022-03-16 10:11:21
 * @Last Modified by:   calmwu
 * @Last Modified time: 2022-03-16 10:11:21
 */

#include "envoy_mgr.h"
#include "routine.h"
#include "utils/clocks.h"
#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/daemon.h"
#include "utils/log.h"
#include "utils/popen.h"

#include "appconfig/appconfig.h"

static const char *__name = "PROXY_ENVOY_MGR";
static const char *__config_name = "plugin_proxy";

static struct proxy_envoy_mgr {
    int32_t   exit_flag;
    pthread_t thread_id;   // routine执行的线程ids
} __proxy_envoy_mgr = {
    .exit_flag = 0,
    .thread_id = 0,
};

__attribute__((constructor)) static void pluginsd_register_routine() {
    fprintf(stderr, "---register_proxy_envoymgr_routine---\n");
    struct xmonitor_static_routine *xsr =
        (struct xmonitor_static_routine *)calloc(1, sizeof(struct xmonitor_static_routine));
    xsr->name = __name;
    xsr->config_name = __config_name;
    xsr->enabled = 1;
    xsr->thread_id = &__proxy_envoy_mgr.thread_id;
    xsr->init_routine = envoy_manager_routine_init;
    xsr->start_routine = envoy_manager_routine_start;
    xsr->stop_routine = envoy_manager_routine_stop;
    register_xmonitor_static_routine(xsr);
}

/**
 * It reads the configuration from the configuration file and initializes the Envoy mgr.
 *
 * @return Nothing.
 */
int32_t envoy_manager_routine_init() {
    // 读取配置
    const char *envoy_bin = appconfig_get_str("plugin_proxy.bin", "");
    const char *envoy_args = appconfig_get_str("plugin_proxy.args", "");
    debug("routine '%s' plugin_proxy.bin: %s, plugin_proxy.args: %s", __name, envoy_bin,
          envoy_args);

    debug("routine '%s' init successed", __name);
    return 0;
}

/**
 * It starts a thread that runs the function `envoy_manager_routine_start`
 *
 * @param arg the argument to pass to the routine.
 *
 * @return Nothing.
 */
void *envoy_manager_routine_start(void *arg) {
    debug("routine '%s' start", __name);

    while (!__proxy_envoy_mgr.exit_flag) {
        sleep(1);
    }

    debug("routine '%s' exit", __name);
    return NULL;
}

/**
 * * Set the exit flag to 1, which will cause the thread to exit.
 * * Join the thread.
 * * Print a debug message
 */
void envoy_manager_routine_stop() {
    __proxy_envoy_mgr.exit_flag = 1;
    pthread_join(__proxy_envoy_mgr.thread_id, NULL);

    debug("routine '%s' has completely stopped", __name);
}