/*
 * @Author: CALM.WU
 * @Date: 2021-11-29 11:35:19
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-12-23 16:37:49
 */

#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/debug.h"

#include <bpf/btf.h>
#include <bpf/libbpf.h>

int32_t main(int32_t argc, char **argv) {
    int32_t                 ret         = 0;
    struct btf *            vmlinux_btf = NULL;
    const struct btf_type * t, *func_proto;
    const char *            func_name, *st_name;
    const struct btf_param *params;
    uint16_t                params_count = 0;

    debugLevel = 9;
    debugFile  = fdopen(STDOUT_FILENO, "w");

    debug("readbtf /sys/kernel/btf/vmlinux");

    vmlinux_btf = libbpf_find_kernel_btf();
    if (unlikely(NULL == vmlinux_btf)) {
        debug("libbpf_find_kernel_btf failed");
        return -1;
    }

    for (uint32_t id = 0; (t = btf__type_by_id(vmlinux_btf, id)); id++) {
        if (!btf_is_func(t)) {
            continue;
        }

        // 函数名
        func_name = btf__str_by_offset(vmlinux_btf, t->name_off);
        // 获取函数原型
        func_proto   = btf__type_by_id(vmlinux_btf, t->type);
        params_count = btf_vlen(func_proto);
        debug("func: %s, params count: %d", func_name, params_count);
        // 函数参数列表
        params = btf_params(func_proto);

        for (uint16_t pi = 0; pi < params_count; pi++) {
            t = btf__type_by_id(vmlinux_btf, params[pi].type);

            if (!btf_is_ptr(t)) {
                st_name = btf__str_by_offset(vmlinux_btf, t->name_off);
                debug("\tparam[%d] --- %s", pi, st_name);
                continue;
            }
            else {
                t = btf__type_by_id(vmlinux_btf, t->type);
                if (btf_is_struct(t)) {
                    st_name = btf__str_by_offset(vmlinux_btf, t->name_off);
                    debug("\tparam[%d] +++ %s", pi, st_name);
                }
                else {
                    debug("\tparam[%d] *** %s", pi, "unknown");
                }
            }
        }
    }

    debugClose();

    return ret;
}