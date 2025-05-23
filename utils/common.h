/*
 * @Author: CALM.WU
 * @Date: 2021-10-15 10:52:05
 * @Last Modified by: CALM.WUU
 * @Last Modified time: 2022-09-13 14:18:11
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <libgen.h>
#include <limits.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/sched.h>
#include <locale.h>
#include <math.h>
#include <net/if.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
#include <spawn.h>
#include <sched.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/eventfd.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <argp.h>
#include <malloc.h>

// C11 新增的 concurrency 相关的 header，gcc4.9 以上才支持，-std=c11
#if GCC_VERSION >= 40900
#include <stdatomic.h>
#endif

#ifdef __cplusplus
}
#endif