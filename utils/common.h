/*
 * @Author: CALM.WU
 * @Date: 2021-10-15 10:52:05
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-01-24 17:57:18
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
#include <locale.h>
#include <math.h>
#include <net/if.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdatomic.h>
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
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <uuid/uuid.h>
//#include <uv.h>

#define __new(T) (typeof(T)) calloc(1, sizeof(T))
#define __delete(P) free((void *)(P))
#define __zero(P) memset((void *)(P), 0, sizeof(*(P)))

#ifdef __cplusplus
}
#endif