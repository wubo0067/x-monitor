/*
 * @Author: CALM.WU
 * @Date: 2021-10-18 11:47:42
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-01-18 14:13:42
 */

#include "appconfig.h"
#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/log.h"

#include "libconfig/libconfig.h"

struct appconfig {
    char             config_file[FILENAME_MAX + 1];
    config_t         cfg;
    bool             loaded;
    pthread_rwlock_t rw_lock;
};

static struct appconfig __appconfig = {
    .loaded  = false,
    .rw_lock = PTHREAD_RWLOCK_INITIALIZER,
};

int32_t appconfig_load(const char *config_file) {
    int32_t ret = 0;

    info("config file: '%s'", config_file);

    if (!__appconfig.loaded) {
        pthread_rwlock_wrlock(&__appconfig.rw_lock);

        if (!__appconfig.loaded) {
            strncpy(__appconfig.config_file, config_file, FILENAME_MAX);
            config_init(&__appconfig.cfg);

            if (config_read_file(&__appconfig.cfg, __appconfig.config_file) != CONFIG_TRUE) {
                fprintf(stderr, "config_read_file failed: %s:%d - %s",
                        config_error_file(&__appconfig.cfg), config_error_line(&__appconfig.cfg),
                        config_error_text(&__appconfig.cfg));
                __appconfig.loaded = false;
                config_destroy(&__appconfig.cfg);
                ret = -1;
            } else {
                __appconfig.loaded = true;
            }
        }
        pthread_rwlock_unlock(&__appconfig.rw_lock);
    }

    if (likely(0 == ret)) {
        // 输出配置文件
        config_write(&__appconfig.cfg, stdout);
    }
    return ret;
}

void appconfig_destroy() {
    pthread_rwlock_wrlock(&__appconfig.rw_lock);
    if (__appconfig.loaded) {
        config_destroy(&__appconfig.cfg);
        __appconfig.loaded = false;
    }
    pthread_rwlock_unlock(&__appconfig.rw_lock);
    pthread_rwlock_destroy(&__appconfig.rw_lock);
}

void appconfig_reload() {
    pthread_rwlock_wrlock(&__appconfig.rw_lock);
    if (__appconfig.loaded) {
        config_destroy(&__appconfig.cfg);
        config_init(&__appconfig.cfg);

        if (config_read_file(&__appconfig.cfg, __appconfig.config_file) != CONFIG_TRUE) {
            error("config_read_file failed: %s:%d - %s", config_error_file(&__appconfig.cfg),
                  config_error_line(&__appconfig.cfg), config_error_text(&__appconfig.cfg));
            __appconfig.loaded = false;
        } else {
            info("reload config file: '%s' successed.", __appconfig.config_file);
            config_write(&__appconfig.cfg, stdout);
            __appconfig.loaded = true;
        }
    }
    pthread_rwlock_unlock(&__appconfig.rw_lock);
}

// ----------------------------------------------------------------------------
// locking

// void appconfig_rdlock() { pthread_rwlock_rdlock( &__appconfig.rw_lock ); }

// void appconfig_wrlock() { pthread_rwlock_wrlock( &__appconfig.rw_lock ); }

// void appconfig_unlock() { pthread_rwlock_unlock( &__appconfig.rw_lock ); }

// ----------------------------------------------------------------------------

const char *appconfig_get_str(const char *key, const char *def) {
    if (unlikely(!key)) {
        return def;
    }

    const char *str = def;
    pthread_rwlock_rdlock(&__appconfig.rw_lock);
    if (likely(__appconfig.loaded)) {
        if (!config_lookup_string(&__appconfig.cfg, key, &str)) {
            warn("config_lookup_string failed: %s", key);
        }
    }
    pthread_rwlock_unlock(&__appconfig.rw_lock);
    return str;
}

int32_t appconfig_get_bool(const char *key, int32_t def) {
    if (unlikely(!key)) {
        return def;
    }

    int32_t b = def;
    pthread_rwlock_rdlock(&__appconfig.rw_lock);
    if (likely(__appconfig.loaded)) {
        if (!config_lookup_bool(&__appconfig.cfg, key, &b)) {
            warn("config_lookup_bool failed: %s", key);
        }
    }
    pthread_rwlock_unlock(&__appconfig.rw_lock);
    return b;
}

int32_t appconfig_get_int(const char *key, int32_t def) {
    if (unlikely(!key)) {
        return def;
    }

    int32_t i = def;
    pthread_rwlock_rdlock(&__appconfig.rw_lock);
    if (likely(__appconfig.loaded)) {
        if (!config_lookup_int(&__appconfig.cfg, key, &i)) {
            warn("config_lookup_int failed: %s", key);
        }
    }
    pthread_rwlock_unlock(&__appconfig.rw_lock);
    return i;
}

const char *appconfig_get_member_str(const char *path, const char *key, const char *def) {
    if (unlikely(!path || !key)) {
        return def;
    }

    const char *str = def;
    pthread_rwlock_rdlock(&__appconfig.rw_lock);

    if (likely(__appconfig.loaded)) {
        config_setting_t *setting = config_lookup(&__appconfig.cfg, path);
        if (setting) {
            if (!config_setting_lookup_string(setting, key, &str)) {
                warn("config_setting_lookup_string failed: %s", key);
            }
        }
    }

    pthread_rwlock_unlock(&__appconfig.rw_lock);
    return str;
}

int32_t appconfig_get_member_bool(const char *path, const char *key, int32_t def) {
    if (unlikely(!path || !key)) {
        return def;
    }

    int32_t b = def;
    pthread_rwlock_rdlock(&__appconfig.rw_lock);
    if (likely(__appconfig.loaded)) {
        config_setting_t *setting = config_lookup(&__appconfig.cfg, path);
        if (setting) {
            if (!config_setting_lookup_bool(setting, key, &b)) {
                warn("config_setting_lookup_bool failed: %s", key);
            }
        }
    }
    pthread_rwlock_unlock(&__appconfig.rw_lock);
    return b;
}

int32_t appconfig_get_member_int(const char *path, const char *key, int32_t def) {
    if (unlikely(!path || !key)) {
        return def;
    }

    int32_t i = def;
    pthread_rwlock_rdlock(&__appconfig.rw_lock);
    if (likely(__appconfig.loaded)) {
        config_setting_t *setting = config_lookup(&__appconfig.cfg, path);
        if (setting) {
            if (!config_setting_lookup_int(setting, key, &i)) {
                warn("config_setting_lookup_bool failed: %s", key);
            }
        }
    }
    pthread_rwlock_unlock(&__appconfig.rw_lock);
    return i;
}