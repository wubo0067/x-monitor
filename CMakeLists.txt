cmake_minimum_required(VERSION 3.18)

# 一定要写在 project 之前
set(CMAKE_C_COMPILER "/usr/bin/clang")
set(CMAKE_CXX_COMPILER "/usr/bin/clang++")

project(x-monitor)

message(STATUS "C++ compiler id: ${CMAKE_CXX_COMPILER_ID}")

execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpfullversion -dumpversion
                OUTPUT_VARIABLE GCC_VERSION)
#string(REGEX MATCHALL "[0-9]+" GCC_VERSION_COMPONENTS ${GCC_VERSION})
list(GET GCC_VERSION_COMPONENTS 0 GCC_MAJOR)
list(GET GCC_VERSION_COMPONENTS 1 GCC_MINOR)
set(GCC_VERSION, "${GCC_MAJOR}.${GCC_MINOR}")
message(STATUS "GCC_VERSION: ${GCC_VERSION}")
message(STATUS "cmake version: ${CMAKE_VERSION}")

SET(COMMIT_HASH "")
SET(BRANCH_NAME "")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} log -1 --pretty=format:%h
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} symbolic-ref --short -q HEAD
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        OUTPUT_VARIABLE BRANCH_NAME
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
message(STATUS "Git version is ${BRANCH_NAME}:${COMMIT_HASH}")
# 版本号.
set(XMonitor_VERSION_MAJOR 1)
set(XMonitor_VERSION_MINOR 0)

string(TIMESTAMP BUILD_TIME "%Y-%m-%d %H:%M:%S" UTC)

# 编译输出目录
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

set(STATIC_LINKING OFF CACHE BOOL "Build ipftrace2 as a statically linked executable")
set(STATIC_LIBC OFF CACHE BOOL "Attempt to embed libc, only known to work with musl. Has issues with dlopen.")
set(USE_ADDRSANITIZER ON CACHE BOOL "AddressSanitizer (detects addressability issues) and LeakSanitizer (detects memory leaks).")

set(APP_METRIC_PREFIX "app" CACHE STRING "Prefix for AppMetrics.")
set(APP_METRIC_LABEL_NAME "app" CACHE STRING "Label for AppMetrics.")
set(COMMON_METRIC_PREFIX "" CACHE STRING "Prefix for CommonMetrics.")

# if(APP_METRIC_PREFIX)
# message(STATUS "set APP_METRIC_LABEL_NAME = ${APP_METRIC_PREFIX}")
# set(APP_METRIC_LABEL_NAME ${APP_METRIC_PREFIX} CACHE STRING "Label for AppMetrics." FORCE)
# endif()
message(STATUS "APP_METRIC_LABEL_NAME: ${APP_METRIC_LABEL_NAME}")
message(STATUS "APP_METRIC_PREFIX: ${APP_METRIC_PREFIX}")
message(STATUS "COMMON_METRIC_PREFIX: ${COMMON_METRIC_PREFIX}")

if(STATIC_LINKING)
  if(STATIC_LIBC)
    set(CMAKE_EXE_LINKER_FLAGS "-static")
  endif(STATIC_LIBC)

  set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
  set(CMAKE_LINK_SEARCH_START_STATIC TRUE)
  set(CMAKE_LINK_SEARCH_END_STATIC TRUE)
endif(STATIC_LINKING)

# debug、release 版本
set(CMAKE_C_FLAGS_DEBUG "-Wall -Wformat=2 -g -ggdb3 -Wno-format-security -Wno-unused-but-set-variable -Wno-unused-const-variable")
set(CMAKE_C_FLAGS_RELEASE "-O3 -Wall -Wformat=2")

if(${CMAKE_BUILD_TYPE} MATCHES "Release")
  set(BuildType "Release")
  message(STATUS "Release Version: ${CMAKE_C_FLAGS_RELEASE}")
else()
  set(BuildType "Debug")
  message(STATUS "Debug Version: ${CMAKE_C_FLAGS_DEBUG}")
endif()

# 设置 c 编译参数
# Position-Independent Code
# set(CMAKE_C_FLAGS "-fPIE -Wno-unknown-pragmas -Wextra -Wpedantic")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unknown-pragmas -Wextra -Wpedantic -fno-omit-frame-pointer -fstack-protector -fno-delete-null-pointer-checks")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wpointer-arith -Werror-implicit-function-declaration -Wno-trigraphs -Wno-format-security")

# -Wredundant-decls 如果在同一个可见域某定义多次声明，编译器就发出警告，即使这些重复声明有效并且毫无差别.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wnested-externs")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wconversion -Wimplicit-fallthrough")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_FORTIFY_SOURCE=2")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-clash-protection -fstack-protector-strong")

# -Wcast-qual 一旦某个指针强制类型转换以便移除类型修饰符时，编译器就发出警告。例如，如果把 const char * 强制转换为普通的 char *时，警告就会出现。
# -Wshadow 一旦某个局部变量屏蔽了另一个局部变量，编译器就发出警
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wshadow -Wcast-qual -Wcast-align -Wwrite-strings")
# generate BTF file
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fverbose-asm -save-temps")

# -Wstrict-prototypes 如果函数的声明或定义没有指出参数类型，编译器就发出警告。(如果函数的前向引用说明指出了参数类型，则允许后面使用旧式风格的函数定义，而不会产生警告.)
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wold-style-definition -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls")

#  -fsanitize=undefined
if(USE_ADDRSANITIZER)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fsanitize-recover=address")
  message(STATUS "CMAKE_C_FLAGS: ${CMAKE_C_FLAGS}")
endif(USE_ADDRSANITIZER)

# 设置 c 标准 cmake3.18 只支持 c11
if(CMAKE_VERSION VERSION_LESS "3.1")
  if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_C_FLAGS "-std=gnu99 ${CMAKE_C_FLAGS}")
  endif()
else()
set(CMAKE_C_FLAGS "-std=gnu23 ${CMAKE_C_FLAGS}")
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wtrampolines -Wsuggest-attribute=noreturn -Wjump-misses-init -Wduplicated-cond -Wduplicated-branches -Wrestrict -Wno-format-overflow")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-z,nodlopen -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now")
  if (GCC_VERSION GREATER "4.9")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wmisleading-indentation -Wnull-dereference")
  endif()
  if (GCC_VERSION VERSION_GREATER_EQUAL 12.0)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstrict-flex-arrays=3")
  endif()
endif()

# set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 这个是支持 c11 和 extensions -std=gnu11
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 依赖项
set(EXTRA_DIR "${PROJECT_SOURCE_DIR}/extra")
set(KERNEL_DIR "/usr/src/linux-5.12.9")

# 配置一个头文件来传递一些 CMake 设置到源代码
configure_file(
  "${PROJECT_SOURCE_DIR}/config.h.in"
  "${PROJECT_SOURCE_DIR}/config.h"
)

# ebpf user include 路径
set(EBPF_USER_INCLUDE_DIRS "${KERNEL_DIR}/usr/include"
  "${KERNEL_DIR}/tools/include"
  "${KERNEL_DIR}/tools/perf"
  "${KERNEL_DIR}/tools/lib"
  "${KERNEL_DIR}/tools/testing/selftests/bpf")

# 头文件 include 目录
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
  "libgmp.a"
  "libzstd.a"
  "liburcu-cds.a"
  "liburcu.a")

# "libpagemap.a")

# 自定义宏
# add_definitions(
# -DCONFIG_FILENAME="/etc/x-monitor/x-monitor.cfg"
# -DLOG_DIR="/usr/log/x-monitor"
# )

# 项目子目录，让 cmake 去识别子目录下的 CMakeLists.txt 文件
add_subdirectory(utils)
add_subdirectory(collectors/process)
add_subdirectory(collectors/proc_sock)

add_subdirectory(examples/construct_init)
add_subdirectory(examples/procfile_cli)
add_subdirectory(examples/read_smaps)

# add_subdirectory(examples/proto_statistics_cli)
add_subdirectory(examples/simplepattern_test)
add_subdirectory(examples/mempool_test)
add_subdirectory(examples/arl_test)
add_subdirectory(examples/prom_client_test)
add_subdirectory(examples/http_test/prom_reg_test)
add_subdirectory(examples/http_test/http_client_test)
add_subdirectory(examples/process_stat_test)
add_subdirectory(examples/regex_cli)
add_subdirectory(examples/config_test)
add_subdirectory(examples/pdpids_test)
add_subdirectory(examples/proc_sock_test)
add_subdirectory(examples/vm_test)
add_subdirectory(examples/lambda)

add_subdirectory(examples/ebpf/cachestat_top_cli)
# add_subdirectory(examples/ebpf/perf_event_stack_cli)
add_subdirectory(examples/ebpf/xdp_libbpf_test)
add_subdirectory(examples/ebpf/bootstrap_cli)
add_subdirectory(examples/ebpf/readbtf_cli)
add_subdirectory(examples/ebpf/deny_ptrace_cli)
add_subdirectory(examples/ebpf/stack_unwind_cli)

add_subdirectory(examples/kernel_dev/mmap_dev_module/user)
add_subdirectory(examples/kernel_dev/dev_ioctl_test/ioctl_userspace)

# x-monitor 的所有源代码
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
  "plugin_proxy/*.h"
  "plugin_cgroups/*.c"
  "plugin_cgroups/*.h")

message(STATUS "X-MONITOR_SRCS = ${X-MONITOR_SRCS}")

# 编译的执行程序
# D_FORTIFY_SOURCE 还会在运行时添加检查 (检测到缓冲区溢出会终止程序)   -D_FORTIFY_SOURCE=2
add_executable(x-monitor main.c ${X-MONITOR_SRCS})
target_compile_definitions(x-monitor PRIVATE
  -DDEBUG
  -DCONFIG_FILENAME="/etc/x-monitor/x-monitor.cfg"
  -DLOG_DIR="/usr/log/x-monitor"
  -DAPP_METRIC_PREFIX=${APP_METRIC_PREFIX}
  -DAPP_METRIC_LABEL_NAME=${APP_METRIC_LABEL_NAME}
  -DCOMMON_METRIC_PREFIX=${COMMON_METRIC_PREFIX})

target_link_libraries(x-monitor xmonitor-process-stat xmonitor-proc-sock xmonitor-utils ${EXTRA_LIBS} pthread ssl crypto uuid dl z m)