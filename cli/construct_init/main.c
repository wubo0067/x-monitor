/*
 * @Author: CALM.WU
 * @Date: 2021-10-22 16:28:26
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-12-06 11:14:05
 */

#include "utils/common.h"
#include <pthread.h>

struct xmonitor_static_routine {
    const char *name;
    const char *config_name;
    pthread_t * thread;

    void (*init_routine)();
    void *(*start_routine)(void *);

    volatile sig_atomic_t exit_flag;

    struct xmonitor_static_routine *next;
};

typedef struct {
    struct xmonitor_static_routine *root;
    struct xmonitor_static_routine *last;
} xmonitor_static_routine_list_t;

static xmonitor_static_routine_list_t __xmonitor_static_routine_list = { NULL,
                                                                     NULL };

static void
register_xmonitor_static_routine(struct xmonitor_static_routine *routine)
{
    if (__xmonitor_static_routine_list.root == NULL) {
        __xmonitor_static_routine_list.root = routine;
        __xmonitor_static_routine_list.last = routine;
    } else {
        __xmonitor_static_routine_list.last->next = routine;
        __xmonitor_static_routine_list.last       = routine;
    }
}

static const char *routine_1_name = "routine_1_name";
static const char *routine_2_name = "routine_2_name";

__attribute__((constructor)) static void register_routine_1()
{
    fprintf(stderr, "register_routine_1\n");
    struct xmonitor_static_routine *routine_1 =
        (struct xmonitor_static_routine *)calloc(
            1, sizeof(struct xmonitor_static_routine));
    routine_1->name = routine_1_name;
    register_xmonitor_static_routine(routine_1);
}

__attribute__((constructor)) static void register_routine_2()
{
    fprintf(stderr, "register_routine_2\n");
    struct xmonitor_static_routine *routine_2 =
        (struct xmonitor_static_routine *)calloc(
            1, sizeof(struct xmonitor_static_routine));
    routine_2->name = routine_2_name;
    register_xmonitor_static_routine(routine_2);
}

int32_t main(int argc, char const *argv[])
{
    fprintf(stderr, "main start\n");

    struct xmonitor_static_routine *routine = __xmonitor_static_routine_list.root;
    struct xmonitor_static_routine *next    = NULL;
    while (routine) {
        next = routine->next;
        fprintf(stderr, "Cleaning up routine '%s'\n", routine->name);
        free(routine);
        routine = next;
    }
    fprintf(stderr, "main exit\n");
    return 0;
}
