cmake_minimum_required (VERSION 3.18)
project (x-monitor)

set(CMAKE_C_COMPILER "/usr/bin/gcc")
set(CMAKE_CXX_COMPILER "/usr/bin/g++")

# 编译输出目录
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

set(STATIC_LINKING OFF CACHE BOOL "Build ipftrace2 as a statically linked executable")
set(STATIC_LIBC OFF CACHE BOOL "Attempt to embed libc, only known to work with musl. Has issues with dlopen.")
set(USE_ADDRSANITIZER OFF CACHE BOOL "AddressSanitizer (detects addressability issues) and LeakSanitizer (detects memory leaks).")

set(APP_METRIC_TAG "" CACHE STRING "Tag for AppMetrics.")
set(APP_METRIC_LABEL "app" CACHE STRING "Label for AppMetrics.")

if(APP_METRIC_TAG)
  message(STATUS "set APP_METRIC_LABEL = ${APP_METRIC_TAG}")
  set(APP_METRIC_LABEL ${APP_METRIC_TAG} CACHE STRING "Label for AppMetrics." FORCE)
endif()

message(STATUS "APP_METRIC_LABEL: ${APP_METRIC_LABEL}")
message(STATUS "APP_METRIC_TAG: ${APP_METRIC_TAG}")

if(STATIC_LINKING)
  if(STATIC_LIBC)
    set(CMAKE_EXE_LINKER_FLAGS "-static")
  endif(STATIC_LIBC)
  set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
  set(CMAKE_LINK_SEARCH_START_STATIC TRUE)
  set(CMAKE_LINK_SEARCH_END_STATIC TRUE)
endif(STATIC_LINKING)

# debug 、 release版本
set(CMAKE_C_FLAGS_DEBUG "-Wall -g -ggdb3")
set(CMAKE_C_FLAGS_RELEASE "-O3 -Wall")

# 设置c编译参数
#set(CMAKE_C_FLAGS "-fPIC -Wno-unknown-pragmas -Wextra -Wpedantic")
set(CMAKE_C_FLAGS "-fPIC -Wno-unknown-pragmas -Wextra -fno-omit-frame-pointer")

if(USE_ADDRSANITIZER)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fsanitize-recover=address")
  message(STATUS "CMAKE_C_FLAGS: ${CMAKE_C_FLAGS}")
endif(USE_ADDRSANITIZER)


# 设置c标准 cmake3.18只支持c11
if (CMAKE_VERSION VERSION_LESS "3.1")
  if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set (CMAKE_C_FLAGS "-std=gnu99 ${CMAKE_C_FLAGS}")
  endif ()
else ()
  set (CMAKE_C_STANDARD 11)
endif ()
#set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# 这个是支持c11和extensions -std=gnu11
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 版本号.
set (XMonitor_VERSION_MAJOR 1)
set (XMonitor_VERSION_MINOR 0)

# 依赖项
set(EXTRA_DIR "${PROJECT_SOURCE_DIR}/extra")
set(KERNEL_DIR "/usr/src/linux-5.12.9")

# 配置一个头文件来传递一些CMake设置到源代码
configure_file (
  "${PROJECT_SOURCE_DIR}/config.h.in"
  "${PROJECT_SOURCE_DIR}/config.h"
  )

# ebpf user include路径
set(EBPF_USER_INCLUDE_DIRS "${KERNEL_DIR}/usr/include"
                           "${KERNEL_DIR}/tools/include"
                           "${KERNEL_DIR}/tools/perf"
                           "${KERNEL_DIR}/tools/lib"
                           "${KERNEL_DIR}/tools/testing/selftests/bpf")

# 头文件include目录
include_directories("${PROJECT_SOURCE_DIR}"
                    "${EXTRA_DIR}/include"
                    "${EXTRA_DIR}/include/microhttpd")

# 第三方静态库链接路径
link_directories("${PROJECT_SOURCE_DIR}/extra/libs")

# 第三方库
set(EXTRA_LIBS "libcurl.a"
              "libconfig.a"
              "libzlog.a"
              "libcollectc.a"
              "libbcc.a"
              "libpcre2-8.a"
              "libpcre2-posix.a"
              "libpromhttp.a"
              "libprom.a"
              "libmicrohttpd.a"
              "libgnutls.a"
              "libidn2.a"
              "libhogweed.a"
              "libnettle.a"
              "libp11-kit-internal.a"
              "libp11-library.a"
              "libp11-common.a"
              "libffi.a"
              "libtasn1.a"
              "libunistring.a"
              "libgmp.a")
              #"libpagemap.a")

# 自定义宏
# add_definitions(
#         -DCONFIG_FILENAME="/etc/x-monitor/x-monitor.cfg"
#         -DLOG_DIR="/usr/log/x-monitor"
# )

# 项目子目录，让cmake去识别子目录下的CMakeLists.txt文件
add_subdirectory(utils)
add_subdirectory(collectors/process)

add_subdirectory(cli/construct_init)
add_subdirectory(cli/procfile_cli)
#add_subdirectory(cli/proto_statistics_cli)
add_subdirectory(cli/simplepattern_test)
add_subdirectory(cli/mempool_test)
add_subdirectory(cli/arl_test)
add_subdirectory(cli/prom_client_test)
add_subdirectory(cli/http_test/prom_reg_test)
add_subdirectory(cli/http_test/http_client_test)
add_subdirectory(cli/process_stat_test)
add_subdirectory(cli/regex_cli)
add_subdirectory(cli/config_test)
add_subdirectory(cli/pdpids_test)

add_subdirectory(cli/ebpf/cachestat_cli)
#add_subdirectory(cli/ebpf/perf_event_stack_cli)
add_subdirectory(cli/ebpf/xdp_libbpf_test)
add_subdirectory(cli/ebpf/bootstrap_cli)
add_subdirectory(cli/ebpf/readbtf_cli)

# 子目录源代码
#set(PLUGINSD_FILES
#        plugins.d/plugins_d.c
#        plugins.d/plugins_d.h
#        )
#
#set(PROXY_FILES
#        plugin_proxy/envoy_mgr.c
#        plugin_proxy/envoy_mgr.h
#        )
#
#set(APPCONFIG_FILES
#        app_config/app_config.h
#        app_config/app_config.c
#        )
#
#set(PROC_PLUGIN_FILES
#        collectors/proc/plugin_proc.h
#        collectors/proc/plugin_proc.c
#        collectors/proc/proc_diskstats.c
#        collectors/proc/proc_loadavg.c
#        collectors/proc/proc_stat.c
#        collectors/proc/proc_pressure.c
#        collectors/proc/proc_vmstat.c
#        collectors/proc/proc_meminfo.c
#        collectors/proc/proc_net_netstat.c
#        collectors/proc/proc_net_dev.c
#        collectors/proc/proc_net_snmp.c
#        collectors/proc/proc_net_sockstat.c
#        collectors/proc/proc_net_stat_conntrack.c
#        collectors/proc/proc_cgroups.c
#        collectors/proc/proc_schedstat.c
#        collectors/diskspace/plugin_diskspace.c
#        collectors/diskspace/plugin_diskspace.h
#        collectors/application/apps_status.h
#        collectors/application/apps_status.c
#        collectors/application/apps_filter_rule.c
#        collectors/application/apps_filter_rule.h
#        collectors/application/plugin_apps.c
#        collectors/application/plugin_apps.h
#        collectors/application/app_metrics.c
#        collectors/application/app_metrics.h
#        )

# x-monitor的所有源代码
file(GLOB_RECURSE X-MONITOR_SRCS 
        "collectors/proc/*.c" 
        "collectors/proc/*.h" 
        "collectors/application/*.c" 
        "collectors/application/*.h" 
        "collectors/diskspace/*.c" 
        "collectors/diskspace/*.h"                 
        "app_config/*.c" 
        "app_config/*.h" 
        "app_register/*.c" 
        "app_register/*.h"         
        "plugins.d/*.c" 
        "plugins.d/*.h" 
        "plugin_proxy/*.c" 
        "plugin_proxy/*.h")

message(STATUS "X-MONITOR_SRCS = ${X-MONITOR_SRCS}")        

# 编译的执行程序
add_executable(x-monitor main.c ${X-MONITOR_SRCS})
#                        ${PLUGINSD_FILES}
#                        ${APPCONFIG_FILES}
#                        ${PROC_PLUGIN_FILES}
#                        ${PROXY_FILES}
#                        )
target_compile_definitions(x-monitor PRIVATE 
  -DDEBUG 
  -DCONFIG_FILENAME="/etc/x-monitor/x-monitor.cfg" 
  -DLOG_DIR="/usr/log/x-monitor"
  -DAPP_METRIC_TAG=${APP_METRIC_TAG}
  -DAPP_METRIC_LABEL=${APP_METRIC_LABEL})

target_link_libraries(x-monitor xmonitor-process-stat xmonitor-utils ${EXTRA_LIBS} pthread ssl crypto uuid dl zstd z m)