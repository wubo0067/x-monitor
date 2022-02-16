/*
 * @Author: CALM.WU
 * @Date: 2021-10-12 11:15:44
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-01-24 14:31:30
 */
#pragma once

#include <stdint.h>

#include "zlog/zlog.h"

#ifdef HAVE_FUNC_ATTRIBUTE_FORMAT
#define PRINTFLIKE(f, a) __attribute__((format(__printf__, f, a)))
#else
#define PRINTFLIKE(f, a)
#endif

// const char * 是限制指针内容，*后面的const是限定变量的，这样才能赋予内部链接属性
// extern const联合修饰时，extern会压制const内部链接属性

extern zlog_category_t *g_log_cat;

#define debug(args...) zlog_debug(g_log_cat, ##args)
#define info(args...) zlog_info(g_log_cat, ##args)
#define warn(args...) zlog_warn(g_log_cat, ##args)
#define error(args...) zlog_error(g_log_cat, ##args)
#define fatal(args...) zlog_fatal(g_log_cat, ##args)

extern int32_t log_init(const char *log_config_file, const char *log_category_name);
extern void    log_fini();
