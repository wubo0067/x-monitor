/*
 * @Author: CALM.WU
 * @Date: 2021-10-12 10:44:47
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2023-02-10 10:38:13
 */

#include "config.h"
#include "routine.h"
#include "utils/clocks.h"
#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/daemon.h"
#include "utils/log.h"
#include "utils/os.h"
#include "utils/popen.h"
#include "utils/signals.h"
#include "utils/strings.h"
#include "app_config/app_config.h"
#include "plugins.d/plugins_d.h"
#include "app_register/exporter_register.h"
#include "prometheus-client-c/promhttp.h"

#define BUF_SIZE 1024

struct option_def {
    /** The option character */
    const char val;
    /** The name of the long option. */
    const char *description;
    /** Short description what the option does */
    /** Name of the argument displayed in SYNOPSIS */
    const char *arg_name;
    /** Default value if not set */
    const char *default_value;
};

static const struct option_def option_definitions[] = {
    // opt description     arg name       default value
    { 'c', "Configuration file to load.", "filename", CONFIG_FILENAME },
    { 'D', "Do not fork. Run in the foreground.", NULL,
      "run in the background" },
    { 'h', "Display this help message.", NULL, NULL },
    { 'v', "Print x-monitor version and exit.", NULL, NULL },
    { 'V', "Print x-monitor version and exit.", NULL, NULL }
};

static char pid_file[XM_PID_FILENAME_MAX] = { 0 };

static struct MHD_Daemon *__promhttp_daemon = NULL;

struct xmonitor_static_routine_list {
    struct xmonitor_static_routine *root;
    struct xmonitor_static_routine *last;
    int32_t static_routine_count;
};

static struct xmonitor_static_routine_list __xmonitor_static_routine_list = {
    NULL, NULL, 0
};

void register_xmonitor_static_routine(struct xmonitor_static_routine *routine) {
    if (__xmonitor_static_routine_list.root == NULL) {
        __xmonitor_static_routine_list.root = routine;
        __xmonitor_static_routine_list.last = routine;
    } else {
        __xmonitor_static_routine_list.last->next = routine;
        __xmonitor_static_routine_list.last = routine;
    }
    ++__xmonitor_static_routine_list.static_routine_count;
    // fprintf(stdout, "[%d] static_routine: '%s' registered\n",
    //         __xmonitor_static_routine_list.static_routine_count,
    //         routine->name);
}

static __attribute__((section(".calmwu"), used)) void __help() {
    int32_t num_opts = (int32_t)ARRAY_SIZE(option_definitions);
    int32_t i;
    int32_t max_len_arg = 0;

    // Compute maximum argument length
    for (i = 0; i < num_opts; i++) {
        if (option_definitions[i].arg_name) {
            int len_arg = (int)strlen(option_definitions[i].arg_name);
            if (len_arg > max_len_arg)
                max_len_arg = len_arg;
        }
    }

    if (max_len_arg > 30)
        max_len_arg = 30;
    if (max_len_arg < 20)
        max_len_arg = 20;

    fprintf(stderr, "%s",
            "\n"
            " ^\n"
            " |.-.   .-.   .-.   .-.   .  x-monitor                            "
            "             \n"
            " |   '-'   '-'   '-'   '-'   real-time performance monitoring, "
            "done right!   \n"
            " +----+-----+-----+-----+-----+-----+-----+-----+-----+-----+-----"
            "+-----+--->\n"
            "\n"
            " Copyright (C) 2021-2100, Calm.wu\n"
            " Released under GNU General Public License v3 or later.\n"
            " This is free software: you are free to change and redistribute "
            "it.\n");

    fprintf(stderr, " SYNOPSIS: x-monitor [options]\n");
    fprintf(stderr, "\n");
    fprintf(stderr, " Options:\n\n");

    // Output options description.
    for (i = 0; i < num_opts; i++) {
        fprintf(stderr, "  -%c %-*s  %s", option_definitions[i].val,
                max_len_arg,
                option_definitions[i].arg_name ?
                    option_definitions[i].arg_name :
                    "",
                option_definitions[i].description);
        if (option_definitions[i].default_value) {
            fprintf(stderr, "\n   %c %-*s  Default: %s\n", ' ', max_len_arg, "",
                    option_definitions[i].default_value);
        } else {
            fprintf(stderr, "\n");
        }
    }
    fflush(stderr);
    return;
}

static void on_signal(int32_t UNUSED(signo), enum signal_action_mode mode) {
    if (E_SIGNAL_EXIT_CLEANLY == mode) {
        if (pid_file[0] != '\0') {
            info("EXIT: removing pid file '%s'", pid_file);
            if (unlink(pid_file) != 0)
                error("EXIT: cannot remove pid file '%s'", pid_file);
        }

        struct xmonitor_static_routine *routine =
            __xmonitor_static_routine_list.root;
        struct xmonitor_static_routine *free_routine = NULL;
        while (routine) {
            if (routine->enabled && routine->stop_routine) {
                routine->stop_routine();
                debug("Routine '%s' has been Cleaned up.", routine->name);
            }
            free_routine = routine;
            routine = free_routine->next;
            free(free_routine);
            free_routine = NULL;
        }

        // 指标对象这里统一释放
        prom_collector_registry_destroy(PROM_COLLECTOR_REGISTRY_DEFAULT);
        PROM_COLLECTOR_REGISTRY_DEFAULT = NULL;
        MHD_stop_daemon(__promhttp_daemon);

        //
        exporter_unregister();
        // free config
        appconfig_destroy();
        // free log
        log_fini();
    } else if (E_SIGNAL_RELOADCONFIG == mode) {
        appconfig_reload();
    }
}

int32_t main(int32_t argc, char *argv[]) {
    char UNUSED(buf[BUF_SIZE]) = { 0 };
    pid_t UNUSED(child_pid) = 0;
    int32_t dont_fork = 0;
    int32_t config_loaded = 0;
    int32_t ret = 0;

    // parse options
    {
        int32_t opts_count = (int32_t)ARRAY_SIZE(option_definitions);
        char opt_str[(opts_count * 2) + 1];
        memset(opt_str, 0, sizeof(opt_str));

        int32_t opt_str_i = 0;
        for (int32_t i = 0; i < opts_count; i++) {
            opt_str[opt_str_i] = option_definitions[i].val;
            opt_str_i++;
            if (option_definitions[i].arg_name) {
                opt_str[opt_str_i] = ':';
                opt_str_i++;
            }
        }

        int32_t opt = 0;

        while ((opt = getopt(argc, argv, opt_str)) != -1) {
            switch (opt) {
            case 'c':
                if (appconfig_load(optarg) < 0) {
                    return -1;
                } else {
                    // 初始化 log
                    const char *log_config_file =
                        appconfig_get_str("application.log_config_file", NULL);
                    if (log_init(log_config_file, "xmonitor") < 0) {
                        return -1;
                    }
                    config_loaded = 1;
                }
                break;
            case 'D':
                dont_fork = 1;
                break;
            case 'V':
            case 'v':
                fprintf(stderr,
                        "\tVersion: %s.%s\n\tGit: %s:%s\n\tBuild Time: %s\n",
                        XMonitor_VERSION_MAJOR, XMonitor_VERSION_MINOR,
                        BRANCH_NAME, COMMIT_HASH, BUILD_TIME);
                return 0;
            case 'h':
            default:
                __help();
                return 0;
            }
        }
    }

    if (!config_loaded) {
        __help();
    }

    test_clock_monotonic_coarse();

    get_cpu_cores_num();
    get_system_hz();
    srand(now_realtime_sec());

    ret = bump_memlock_rlimit();
    if (unlikely(0 != ret)) {
        fatal("failed to increase memlock rlimit: %s\n", strerror(errno));
        return -1;
    }

    if (unlikely(0 != exporter_register())) {
        return 0;
    }

    ret = prom_collector_registry_default_init();
    if (unlikely(0 != ret)) {
        error("prom_collector_registry_default_init failed, ret: %d", ret);
        return -1;
    }

    // 信号初始化
    signals_block();
    signals_init();

    // INIT routines
    struct xmonitor_static_routine *routine =
        __xmonitor_static_routine_list.root;
    for (; routine; routine = routine->next) {
        // 判断是否 enable
        if (routine->config_name) {
            routine->enabled =
                appconfig_get_member_bool(routine->config_name, "enable", 0);
            debug("Routine '%s' config '%s' is %s", routine->name,
                  routine->config_name,
                  routine->enabled ? "enabled" : "disabled");
        }

        if (routine->enabled && NULL != routine->init_routine) {
            // 初始化
            ret = routine->init_routine();
            if (0 == ret) {
                info("init xmonitor-static-routine '%s' successed",
                     routine->name);
            } else {
                error("init xmonitor-static-routine '%s' failed",
                      routine->name);
                return 0;
            }
        } else {
            debug("xmonitor-static-routine '%s' is disabled.", routine->name);
        }
    }
    // 给 http 注册 registry
    promhttp_set_active_collector_registry(NULL);

    // 守护进程
    strlcpy(pid_file,
            appconfig_get_str("application.pid_file", DEFAULT_PIDFILE),
            XM_PID_FILENAME_MAX);
    const char *user = appconfig_get_str("application.run_as_user", NULL);
    become_daemon(dont_fork, pid_file, user);

    // 计算密集型调度策略
    sched_setscheduler(getpid(), SCHED_BATCH, NULL);

    // 启动 metrics http exporter
    uint16_t metrics_http_export_port = (uint16_t)appconfig_get_int(
        "application.metrics_http_exporter.port", 8000);
    __promhttp_daemon = promhttp_start_daemon(
        MHD_USE_SELECT_INTERNALLY, metrics_http_export_port, NULL, NULL);
    if (unlikely(!__promhttp_daemon)) {
        error("promhttp_start_daemon failed");
        return -1;
    }

    // START routines
    routine = __xmonitor_static_routine_list.root;
    for (; routine; routine = routine->next) {
        if (routine->enabled && NULL != routine->start_routine) {
            ret = pthread_create(routine->thread_id, NULL,
                                 routine->start_routine, NULL);
            if (unlikely(0 != ret)) {
                error("xmonitor-static-routine '%s' pthread_create() failed "
                      "with code %d",
                      routine->name, ret);
            } else {
                info("xmonitor-static-routine '%s' successed to create new "
                     "thread.",
                     routine->name);
            }
        }
    }

    // 解除信号阻塞
    signals_unblock();
    // 信号处理
    signals_handle(on_signal);

    return 0;
}