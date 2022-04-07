/*
 * @Author: CALM.WU
 * @Date: 2021-10-15 14:41:36
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-11-30 17:07:42
 */

#include "plugins_d.h"
#include "routine.h"
#include "utils/clocks.h"
#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/daemon.h"
#include "utils/log.h"
#include "utils/popen.h"

#include "appconfig/appconfig.h"

#define PLUGINSD_FILE_SUFFIX ".plugin"
#define PLUGINSD_FILE_SUFFIX_LEN strlen(PLUGINSD_FILE_SUFFIX)

static const char *__default_plugin_dir = "/usr/libexec/x-monitor/plugins.d";
static const char *__name = "PLUGINSD";
static const char *__config_name = "pluginsd";

struct external_plugin {
    char    config_name[XM_CONFIG_NAME_MAX];
    char    file_name[XM_FILENAME_MAX];
    char    full_file_name[XM_FILENAME_MAX];
    char    cmd[XM_CMD_LINE_MAX];   // the command that it executes
    int32_t exit_flag;

    volatile sig_atomic_t enabled;
    int32_t               update_every;

    volatile sig_atomic_t is_working;
    time_t                start_time;
    pthread_t             thread_id;   // routine执行的线程id
    volatile pid_t        child_pid;   //

    struct external_plugin *next;
};

struct pluginsd {
    struct external_plugin *external_plugins_root;
    int32_t                 scan_frequency;
    int32_t                 exit_flag;
    pthread_t               thread_id;
};

const int32_t DEFAULT_SCAN_MAX_FREQUENCY = 60;

static struct pluginsd __pluginsd = {
    .external_plugins_root = NULL,
    .scan_frequency = DEFAULT_SCAN_MAX_FREQUENCY,
    .exit_flag = 0,
};

__attribute__((constructor)) static void pluginsd_register_routine() {
    fprintf(stderr, "---register_pluginsd_routine---\n");
    struct xmonitor_static_routine *xsr =
        (struct xmonitor_static_routine *)calloc(1, sizeof(struct xmonitor_static_routine));
    xsr->name = __name;
    xsr->config_name = __config_name;
    xsr->enabled = 1;
    xsr->thread_id = &__pluginsd.thread_id;
    xsr->init_routine = pluginsd_routine_init;
    xsr->start_routine = pluginsd_routine_start;
    xsr->stop_routine = pluginsd_routine_stop;
    register_xmonitor_static_routine(xsr);
}

static void __external_plugin_thread_cleanup(void *arg) {
    struct external_plugin *ep = (struct external_plugin *)arg;

    if (ep->child_pid > 0) {
        siginfo_t info;
        info("external plugin '%s' killing child process pid %d", ep->config_name, ep->child_pid);
        if (kill_pid(ep->child_pid, 0) != -1) {
            info("external plugin '%s' waiting for child process pid %d to exit...",
                 ep->config_name, ep->child_pid);
            waitid(P_PID, (id_t)ep->child_pid, &info, WEXITED);
        }
        ep->child_pid = 0;
    }
    ep->is_working = 0;
    debug("external plugin '%s' worker thread has been cleaned up...", ep->config_name);
}

static void *external_plugin_thread_worker(void *arg) {
    struct external_plugin *ep = (struct external_plugin *)arg;
    debug("exteranl plugin '%s' thread worker is running", ep->config_name);

    // 设置本线程取消动作的执行时机，type由两种取值：PTHREAD_CANCEL_DEFFERED和PTHREAD_CANCEL_ASYCHRONOUS，
    // 仅当Cancel状态为Enable时有效，分别表示收到信号后继续运行至下一个取消点再退出和立即执行取消动作（退出）；
    // oldtype如果不为NULL则存入运来的取消动作类型值。
    if (pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL) != 0)
        error("cannot set pthread cancel type to DEFERRED.");

    // 设置本线程对Cancel信号的反应，state有两种值：PTHREAD_CANCEL_ENABLE（缺省）和PTHREAD_CANCEL_DISABLE，
    // 分别表示收到信号后设为CANCLED状态和忽略CANCEL信号继续运行；old_state如果不为NULL则存入原来的Cancel状态以便恢复。
    if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL) != 0)
        error("cannot set pthread cancel state to ENABLE.");

    // 保证pthread_cancel会执行external_plugin_thread_cleanup，杀掉子进程
    pthread_cleanup_push(__external_plugin_thread_cleanup, arg);

    while (!ep->exit_flag) {
        // 执行扩展插件
        FILE *child_fp = mypopen(ep->cmd, &ep->child_pid);
        if (unlikely(!child_fp)) {
            error("Cannot popen(\"%s\", \"r\").", ep->cmd);
            break;
        }

        debug("connected to '%s' running on pid %d", ep->cmd, ep->child_pid);
        char buf[XM_STDOUT_LINE_BUF_SIZE] = { 0 };

        while (1) {
            // 读取plugin的标准输出内容
            if (fgets(buf, XM_STDOUT_LINE_BUF_SIZE - 1, child_fp) == NULL) {
                if (feof(child_fp)) {
                    info("fgets() return EOF.");
                    break;
                } else if (ferror(child_fp)) {
                    info("fgets() return error.");
                    break;
                } else {
                    info("fgets() return unknown.");
                    break;
                }
            }
            buf[strlen(buf) - 1] = '\0';
            info("from '%s' recv: [%s]", ep->config_name, buf);
        }

        error("'%s' (pid %d) disconnected after successful data collections (ENDs).",
              ep->config_name, ep->child_pid);

        kill_pid(ep->child_pid, 0);

        int32_t child_exit_code = mypclose(child_fp, ep->child_pid);
        info("from '%s' exit with code %d", ep->config_name, child_exit_code);

        ep->child_pid = 0;
        ep->enabled = appconfig_get_member_bool(ep->config_name, "enable", 0);
        if (unlikely(!ep->enabled)) {
            debug("external plugin config '%s' is disabled", ep->config_name);
            // 异常退出，如果配置也标记不可用，则退出线程，否则重启plugin
            break;
        }
        // 等待1秒后重启
        sleep(3);
    }

    pthread_cleanup_pop(1);

    return NULL;
}

int32_t pluginsd_routine_init() {
    // 插件目录
    const char *plugins_dir =
        appconfig_get_str("application.plugins_directory", __default_plugin_dir);
    debug("application.plugins_directory: %s", plugins_dir);

    // 扫描频率
    __pluginsd.scan_frequency = appconfig_get_int("pluginsd.check_for_new_plugins_every", 5);
    if (__pluginsd.scan_frequency <= 0) {
        __pluginsd.scan_frequency = 1;
    } else if (__pluginsd.scan_frequency > DEFAULT_SCAN_MAX_FREQUENCY) {
        __pluginsd.scan_frequency = DEFAULT_SCAN_MAX_FREQUENCY;
        warn("the pluginsd.check_for_new_plugins_every: %d is too large, set to %d",
             __pluginsd.scan_frequency, DEFAULT_SCAN_MAX_FREQUENCY);
    }
    debug("pluginsd.check_for_new_plugins_every: %d", __pluginsd.scan_frequency);

    debug("routine '%s' init successed", __name);

    return 0;
}

void *pluginsd_routine_start(void *arg) {
    debug("routine '%s' start", __name);

    // https://www.cnblogs.com/guxuanqing/p/8385077.html
    // pthread_cleanup_push( pluginsd_cleanup, NULL );

    while (!__pluginsd.exit_flag) {
        const char *dir_cfg =
            appconfig_get_str("application.plugins_directory", __default_plugin_dir);

        // 扫描插件目录，找到执行程序
        DIR *dir = opendir(dir_cfg);
        if (unlikely(NULL == dir)) {
            warn("cannot open plugins directory '%s', reason: %s", dir_cfg, strerror(errno));
            sleep(__pluginsd.scan_frequency);
            continue;
        }

        struct dirent *entry = NULL;
        while (likely(NULL != (entry = readdir(dir)))) {
            if (unlikely(__pluginsd.exit_flag)) {
                break;
            }

            if (unlikely(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0))
                continue;

            debug("pluginsd check examining file '%s'", entry->d_name);

            // 检查文件名是否以.plugin结尾
            size_t len = strlen(entry->d_name);
            if (unlikely(len <= PLUGINSD_FILE_SUFFIX_LEN))
                continue;

            if (unlikely(
                    strcmp(entry->d_name + len - PLUGINSD_FILE_SUFFIX_LEN, PLUGINSD_FILE_SUFFIX)
                    != 0)) {
                debug("file '%s' does not end in '%s'", entry->d_name, PLUGINSD_FILE_SUFFIX);
                continue;
            }

            // 检查配置是否可以运行
            char external_plugin_cfgname[XM_CONFIG_NAME_MAX];
            snprintf(external_plugin_cfgname, XM_CONFIG_NAME_MAX, "pluginsd.%.*s",
                     (int)(len - PLUGINSD_FILE_SUFFIX_LEN), entry->d_name);

            int32_t enabled = appconfig_get_member_bool(external_plugin_cfgname, "enable", 0);
            if (unlikely(!enabled)) {
                debug("external plugin config '%s' is disabled", external_plugin_cfgname);
                continue;
            }

            // 判断是否已经在运行
            struct external_plugin *ep = NULL;
            struct external_plugin *prev_ep = __pluginsd.external_plugins_root;
            for (ep = __pluginsd.external_plugins_root; ep; prev_ep = ep, ep = ep->next) {
                if (unlikely(0 == strcmp(ep->file_name, entry->d_name))) {
                    break;
                }
            }
            if (ep) {
                if (1 == ep->is_working) {
                    debug("external plugin '%s' is already running", ep->config_name);
                    continue;
                } else {
                    info("external plugin '%s' is_working: %d", ep->config_name, ep->is_working);
                    // 释放ep
                    if (prev_ep != ep) {
                        prev_ep->next = ep->next;
                    } else {
                        __pluginsd.external_plugins_root = NULL;
                    }
                    info("clean old external plugin '%s'", ep->config_name);
                    free(ep);
                    ep = NULL;
                }
            }

            // 没有运行，启动
            if (!ep) {
                ep = (struct external_plugin *)calloc(1, sizeof(struct external_plugin));

                strlcpy(ep->config_name, external_plugin_cfgname, XM_CONFIG_NAME_MAX);
                strlcpy(ep->file_name, entry->d_name, XM_FILENAME_MAX);
                // -Wformat-truncation=
                snprintf(ep->full_file_name, XM_FILENAME_MAX - 1, "%s/%s", dir_cfg, entry->d_name);
                // 检查文件是否可执行
                if (unlikely(access(ep->full_file_name, X_OK) != 0)) {
                    warn("cannot execute file '%s'", ep->full_file_name);
                    free(ep);
                    ep = NULL;
                    continue;
                }

                ep->enabled = enabled;
                ep->start_time = now_realtime_sec();
                ep->update_every =
                    appconfig_get_member_int(external_plugin_cfgname, "update_every", 5);

                // 生成执行命令
                char *def = "";
                snprintf(ep->cmd, XM_CMD_LINE_MAX - 1, "exec %s %d %s", ep->full_file_name,
                         ep->update_every,
                         appconfig_get_member_str(external_plugin_cfgname, "command_options", def));

                debug("file_name:'%s' full_file_name:'%s', cmd:'%s'", ep->file_name,
                      ep->full_file_name, ep->cmd);

                // 为命令创建执行线程
                int32_t ret =
                    pthread_create(&ep->thread_id, NULL, external_plugin_thread_worker, ep);
                if (unlikely(ret != 0)) {
                    error("cannot create thread for external plugin '%s'", external_plugin_cfgname);
                    free(ep);
                    ep = NULL;
                    continue;
                } else {
                    // add header
                    if (likely(__pluginsd.external_plugins_root)) {
                        ep->next = __pluginsd.external_plugins_root;
                    }
                    __pluginsd.external_plugins_root = ep;
                    ep->is_working = 1;
                }
            }
        }
        closedir(dir);
        sleep(__pluginsd.scan_frequency);
    }

    debug("routine '%s' exit", __name);
    return 0;
}

void pluginsd_routine_stop() {
    __pluginsd.exit_flag = 1;
    pthread_join(__pluginsd.thread_id, NULL);

    for (struct external_plugin *p = __pluginsd.external_plugins_root; p; p = p->next) {
        p->exit_flag = 1;
        // 对external_plugin的工作线程发送cancel信号, 线程运行到cancel点后退出
        pthread_cancel(p->thread_id);
        // 等待external_plugin的工作线程结束
        pthread_join(p->thread_id, NULL);
        info("[%s] external plugin '%s' worker thread exit!", __name, p->config_name);
    }
    debug("routine '%s' has completely stopped", __name);
}