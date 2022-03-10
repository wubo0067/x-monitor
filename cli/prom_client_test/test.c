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

static prom_gauge_t   *bar_gauge;
static prom_counter_t *bar_counter;

static const char *__key = "UdpLite";

int32_t main(int32_t argc, char **argv) {
    if (log_init("../cli/prom_client_test/log.cfg", "prom_client_test") != 0) {
        fprintf(stderr, "log init failed\n");
        return -1;
    }

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

    debug("-------------------------------------");

    int32_t ret = prom_collector_registry_default_init();
    if (unlikely(0 != ret)) {
        error("prom_collector_registry_default_init failed, ret: %d", ret);
        return -1;
    }

    bar_gauge = prom_collector_registry_must_register_metric(
        prom_gauge_new("bar_gauge", "gauge for bar", 1, (const char *[]){ "label" }));

    bar_counter = prom_collector_registry_must_register_metric(
        prom_counter_new("bar_counter", "counter for bar", 0, NULL));

    // prom_collector_t *test_collector = prom_collector_new("test");
    prom_collector_t *default_collector =
        (prom_collector_t *)prom_map_get(PROM_COLLECTOR_REGISTRY_DEFAULT->collectors, "default");
    if (unlikely(NULL == default_collector)) {
        error("get PROM_COLLECTOR_REGISTRY_DEFAULT.collectors failed");
        return -1;
    } else {
        debug("get PROM_COLLECTOR_REGISTRY_DEFAULT.collectors successed");
    }

    debug("PROM_COLLECTOR_REGISTRY_DEFAULT.default collector have %ld metrics",
          prom_map_size(default_collector->metrics));

    // 将这个collector注册到默认的collector registry
    // prom_collector_registry_register_collector(PROM_COLLECTOR_REGISTRY_DEFAULT, test_collector);

    //
    // debug("after register test collector, current collector count:%ld",
    //       prom_map_size(PROM_COLLECTOR_REGISTRY_DEFAULT->collectors));

    // 从map中释放这个指标
    prom_map_delete(default_collector->metrics, ((prom_metric_t *)bar_gauge)->name);

    // 销毁这个指标，这里不需要
    // prom_gauge_destroy(bar_gauge);
    // bar_gauge = NULL;

    debug(
        "after prom_map_delete PROM_COLLECTOR_REGISTRY_DEFAULT.default collector have %ld metrics",
        prom_map_size(default_collector->metrics));

    // prom_collector_destroy(test_collector);

    prom_collector_registry_destroy(PROM_COLLECTOR_REGISTRY_DEFAULT);

    log_fini();
}