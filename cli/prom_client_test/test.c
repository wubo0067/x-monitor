/*
 * @Author: calmwu
 * @Date: 2022-03-10 09:09:24
 * @Last Modified by: calmwu
 * @Last Modified time: 2022-03-10 09:12:05
 */

#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/log.h"
#include "prometheus-client-c/prom.h"
#include "prometheus-client-c/prom_collector_t.h"
#include "prometheus-client-c/prom_collector_registry_t.h"
#include "prometheus-client-c/prom_map_i.h"
#include "prometheus-client-c/prom_metric_t.h"
#include "prometheus-client-c/prom_metric_i.h"
#include "prometheus-client-c/prom_linked_list_i.h"
#include "prometheus-client-c/promhttp.h"

static prom_gauge_t      *bar_gauge;
static prom_counter_t    *bar_counter;
static struct MHD_Daemon *__promhttp_daemon = NULL;

static const char *__key = "UdpLite";

static void __show_collectors(prom_map_t *collectors) {
    int32_t index = 0;
    for (prom_linked_list_node_t *current_node = collectors->keys->head; current_node != NULL;
         current_node = current_node->next, index++) {
        const char *collector_name = (const char *)current_node->item;
        debug("PROM_COLLECTOR_REGISTRY_DEFAULT->collectors[%d] name: '%s'", index, collector_name);
    }
}

static void __show_metrics(prom_map_t *metrics) {
    int32_t index = 0;
    for (prom_linked_list_node_t *current_node = metrics->keys->head; current_node != NULL;
         current_node = current_node->next, index++) {
        const char *metric_name = (const char *)current_node->item;
        debug("metrics[%d] name: '%s'", index, metric_name);
    }
}

static prom_linked_list_compare_t __compare_key(void *item_a, void *item_b) {
    const char *str_a = (const char *)item_a;
    const char *str_b = (const char *)item_b;
    return strcmp(str_a, str_b);
}

static void __add_app_metrics() {
    debug("+++add_app_metrics+++");
    // 构造app的collector
    prom_collector_t *app_collector = prom_collector_new("app_example");
    // 注册这个collector
    prom_collector_registry_register_collector(PROM_COLLECTOR_REGISTRY_DEFAULT, app_collector);
    // 构造几个metric
    char           app_metric_name[64] = { 0 };
    prom_metric_t *app_metrics[10] = { NULL };
    for (int32_t index = 0; index < 10; index++) {
        snprintf(app_metric_name, sizeof(app_metric_name), "app_metric_%d", index);
        app_metrics[index] =
            prom_gauge_new(app_metric_name, app_metric_name, 1, (const char *[]){ "label" });
        prom_collector_add_metric(app_collector, app_metrics[index]);
    }

    // 设置指
    for (int32_t index = 0; index < 10; index++) {
        prom_gauge_set(app_metrics[index], index, (const char *[]){ "app" });
    }

    __show_metrics(app_collector->metrics);
}

static void __delete_app_metrics() {
    debug("---delete_app_metrics---");
    prom_map_delete(PROM_COLLECTOR_REGISTRY_DEFAULT->collectors, "app_example");
}

static void __test_1() {
    if (0 == strcmp(__key, "Udp")) {
        debug("this is Udp");
    }

    prom_map_t *map = prom_map_new();
    prom_map_set(map, "foo", "bar");
    prom_map_set(map, "bing", "bang");

    debug("prom_map_size: %ld", prom_map_size(map));
    for (prom_linked_list_node_t *current_node = map->keys->head; current_node != NULL;
         current_node = current_node->next) {
        const char *key = (const char *)current_node->item;
        debug("prom_map item.key: %s", key);
    }

    prom_map_delete(map, "foo");

    prom_map_destroy(map);
}

static void __test_2() {
    __show_collectors(PROM_COLLECTOR_REGISTRY_DEFAULT->collectors);

    bar_gauge = prom_collector_registry_must_register_metric(
        prom_gauge_new("bar_gauge", "gauge for bar", 1, (const char *[]){ "label" }));

    bar_counter = prom_collector_registry_must_register_metric(
        prom_counter_new("bar_counter", "counter for bar", 0, NULL));

    prom_collector_t *default_collector =
        (prom_collector_t *)prom_map_get(PROM_COLLECTOR_REGISTRY_DEFAULT->collectors, "default");
    if (unlikely(NULL == default_collector)) {
        error("get PROM_COLLECTOR_REGISTRY_DEFAULT.collectors failed");
        return;
    } else {
        debug("get PROM_COLLECTOR_REGISTRY_DEFAULT.collectors successed");
    }

    __show_metrics(default_collector->metrics);
    debug("+++PROM_COLLECTOR_REGISTRY_DEFAULT.default collector have %ld metrics",
          prom_map_size(default_collector->metrics));

    // 从map中释放这个指标
    prom_map_delete(default_collector->metrics, ((prom_metric_t *)bar_gauge)->name);
    __show_metrics(default_collector->metrics);
    debug("---PROM_COLLECTOR_REGISTRY_DEFAULT.default collector have %ld metrics",
          prom_map_size(default_collector->metrics));

    debug("-------------------------------------");

    prom_collector_t *test_collector = prom_collector_new("test");

    // 将这个collector注册到PROM_COLLECTOR_REGISTRY_DEFAULT.collectors中
    prom_collector_registry_register_collector(PROM_COLLECTOR_REGISTRY_DEFAULT, test_collector);
    __show_collectors(PROM_COLLECTOR_REGISTRY_DEFAULT->collectors);
    debug("+++add test collector, %ld", prom_map_size(PROM_COLLECTOR_REGISTRY_DEFAULT->collectors));

    // 将collector从PROM_COLLECTOR_REGISTRY_DEFAULT.collectors注销
    prom_map_delete(PROM_COLLECTOR_REGISTRY_DEFAULT->collectors, test_collector->name);
    __show_collectors(PROM_COLLECTOR_REGISTRY_DEFAULT->collectors);
    debug("---delete test collector, %ld",
          prom_map_size(PROM_COLLECTOR_REGISTRY_DEFAULT->collectors));

    // -------------再次注册一个collector---------------------
    test_collector = prom_collector_new("test");

    prom_collector_registry_register_collector(PROM_COLLECTOR_REGISTRY_DEFAULT, test_collector);
    __show_collectors(PROM_COLLECTOR_REGISTRY_DEFAULT->collectors);
    debug("+++add test collector, %ld", prom_map_size(PROM_COLLECTOR_REGISTRY_DEFAULT->collectors));

    // 将collector从PROM_COLLECTOR_REGISTRY_DEFAULT.collectors注销
    prom_map_delete(PROM_COLLECTOR_REGISTRY_DEFAULT->collectors, test_collector->name);
    __show_collectors(PROM_COLLECTOR_REGISTRY_DEFAULT->collectors);
    debug("---delete test collector, %ld",
          prom_map_size(PROM_COLLECTOR_REGISTRY_DEFAULT->collectors));

    // 销毁这个指标，这里不需要
    // prom_gauge_destroy(bar_gauge);
    // bar_gauge = NULL;

    // prom_collector_destroy(test_collector);
}

int32_t main(int32_t argc, char **argv) {
    if (log_init("../cli/log.cfg", "prom_client_test") != 0) {
        fprintf(stderr, "log init failed\n");
        return -1;
    }

    debug("-------------------------------------");

    int32_t ret = prom_collector_registry_default_init();
    if (unlikely(0 != ret)) {
        error("prom_collector_registry_default_init failed, ret: %d", ret);
        return -1;
    }
    promhttp_set_active_collector_registry(NULL);

    __promhttp_daemon = promhttp_start_daemon(MHD_USE_SELECT_INTERNALLY, 30006, NULL, NULL);
    // ** !!!!!!!!!!!!!!!!
    // prom_linked_list_set_compare_fn(PROM_COLLECTOR_REGISTRY_DEFAULT->collectors->keys,
    //                                 __compare_key);
    for (int32_t i = 0; i < 10; i++) {
        __add_app_metrics(PROM_COLLECTOR_REGISTRY_DEFAULT->collectors);
        __show_collectors(PROM_COLLECTOR_REGISTRY_DEFAULT->collectors);

        sleep(3);

        __delete_app_metrics();
        __show_collectors(PROM_COLLECTOR_REGISTRY_DEFAULT->collectors);
        sleep(3);
    }

    prom_collector_registry_destroy(PROM_COLLECTOR_REGISTRY_DEFAULT);

    PROM_COLLECTOR_REGISTRY_DEFAULT = NULL;
    MHD_stop_daemon(__promhttp_daemon);

    log_fini();
}