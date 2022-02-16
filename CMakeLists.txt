cmake_minimum_required (VERSION 3.18)
project (x-monitor)

set(CMAKE_C_COMPILER "/usr/bin/gcc")
set(CMAKE_CXX_COMPILER "/usr/bin/g++")

# 编译输出目录
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)

set(STATIC_LINKING OFF CACHE BOOL "Build ipftrace2 as a statically linked executable")
set(STATIC_LIBC OFF CACHE BOOL "Attempt to embed libc, only known to work with musl. Has issues with dlopen.")

if(STATIC_LINKING)
  if(STATIC_LIBC)
    set(CMAKE_EXE_LINKER_FLAGS "-static")
  endif(STATIC_LIBC)
  set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
  set(CMAKE_LINK_SEARCH_START_STATIC TRUE)
  set(CMAKE_LINK_SEARCH_END_STATIC TRUE)
endif(STATIC_LINKING)

# debug 、 release版本
set(CMAKE_C_FLAGS_DEBUG "-O0 -Wall -g -ggdb3")
set(CMAKE_C_FLAGS_RELEASE "-O3 -Wall")

# 设置c编译参数
set(CMAKE_C_FLAGS "-fPIC -Wno-unknown-pragmas -Wextra")

# 设置c标准 cmake3.18只支持c11
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

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
set(EXTRA_LIBS "libjson-c.a"
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

# 自定义宏
# add_definitions(
#         -DCONFIG_FILENAME="/etc/x-monitor/x-monitor.cfg"
#         -DLOG_DIR="/usr/log/x-monitor"
# )

# 项目子目录
add_subdirectory(utils)
add_subdirectory(cli/construct_init)
#add_subdirectory(cli/cachestat_cli)
#add_subdirectory(cli/perf_event_stack_cli)
add_subdirectory(cli/procfile_cli)
#add_subdirectory(cli/proto_statistics_cli)
add_subdirectory(cli/simplepattern_test)
add_subdirectory(cli/mempool_test)
add_subdirectory(cli/arl_test)
add_subdirectory(cli/xdp_libbpf_test)

# 子目录源代码
set(PLUGINSD_FILES
        plugins.d/plugins_d.c
        plugins.d/plugins_d.h
        )

set(APPCONFIG_FILES
        appconfig/appconfig.h
        appconfig/appconfig.c
        )

set(PROC_PLUGIN_FILES
        collectors/proc/plugin_proc.h
        collectors/proc/plugin_proc.c
        collectors/proc/proc_diskstats.c
        collectors/proc/proc_loadavg.c
        collectors/proc/proc_stat.c
        collectors/proc/proc_pressure.c
        collectors/proc/proc_vmstat.c
        collectors/proc/proc_meminfo.c
        collectors/diskspace/plugin_diskspace.c
        collectors/diskspace/plugin_diskspace.h
        )

# 编译的执行程序
add_executable(x-monitor main.c
                        ${PLUGINSD_FILES}
                        ${APPCONFIG_FILES}
                        ${PROC_PLUGIN_FILES}
                        )
target_compile_definitions(x-monitor PRIVATE -DDEBUG -DCONFIG_FILENAME="/etc/x-monitor/x-monitor.cfg" -DLOG_DIR="/usr/log/x-monitor")

target_link_libraries(x-monitor xmonitor-utils ${EXTRA_LIBS} pthread dl z m)