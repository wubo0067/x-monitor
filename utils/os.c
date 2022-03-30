/*
 * @Author: CALM.WU
 * @Date: 2022-01-18 11:38:31
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-01-24 17:26:50
 */

#include "common.h"
#include "compiler.h"
#include "os.h"
#include "log.h"
#include "procfile.h"
#include "files.h"
#include "strings.h"

static _Thread_local char __hostname[HOST_NAME_MAX] = { 0 };

static const char *__def_ipaddr = "0.0.0.0";
static const char *__def_macaddr = "00:00:00:00:00:00";
static const char *__def_hostname = "unknown";

static _Thread_local int32_t __processors = 1;

static const char __no_user[] = "";

const char *get_hostname() {
    if (unlikely(0 == __hostname[0])) {
        if (unlikely(0 != gethostname(__hostname, HOST_NAME_MAX))) {
            strlcpy(__hostname, __def_hostname, HOST_NAME_MAX);
            __hostname[7] = '\0';
        }
    }
    return __hostname;
}

const char *get_ipaddr_by_iface(const char *iface, char *ip_buf, size_t ip_buf_size) {
    if (unlikely(NULL == iface)) {
        return __def_ipaddr;
    }

    struct ifreq ifr;
    ifr.ifr_addr.sa_family = AF_INET;

    int32_t fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (unlikely(fd < 0)) {
        return __def_ipaddr;
    }

    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ioctl(fd, SIOCGIFADDR, &ifr);

    close(fd);

    char *ip_addr = inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr);
    strlcpy(ip_buf, ip_addr, ip_buf_size);
    // ip_buf[ip_buf_size - 1] = '\0';

    return ip_buf;
}

const char *get_macaddr_by_iface(const char *iface, char *mac_buf, size_t mac_buf_size) {
    if (unlikely(NULL == iface)) {
        return __def_ipaddr;
    }

    struct ifreq ifr;
    ifr.ifr_addr.sa_family = AF_INET;

    int32_t fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (unlikely(fd < 0)) {
        return __def_macaddr;
    }

    strlcpy(ifr.ifr_name, iface, IFNAMSIZ);
    ioctl(fd, SIOCGIFHWADDR, &ifr);

    close(fd);

    uint8_t *mac = (uint8_t *)ifr.ifr_hwaddr.sa_data;

    snprintf(mac_buf, mac_buf_size - 1, "%.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n", mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
    mac_buf[mac_buf_size - 1] = '\0';

    return mac_buf;
}

// 锁定内存限制 BCC 已经无条件地将此限制设置为无穷大，但 libbpf 不会自动执行此操作（按照设计）。
int32_t bump_memlock_rlimit(void) {
    struct rlimit rlim_new = {
        .rlim_cur = RLIM_INFINITY,
        .rlim_max = RLIM_INFINITY,
    };

    return setrlimit(RLIMIT_MEMLOCK, &rlim_new);
}

const char *get_username(uid_t uid) {
    struct passwd *pwd = getpwuid(uid);
    if (pwd == NULL) {
        return __no_user;
    }
    return pwd->pw_name;
}

int32_t get_system_cpus() {
    // return sysconf(_SC_NPROCESSORS_ONLN); 加强移植性

    struct proc_file *pf_stat = procfile_open("/proc/stat", NULL, PROCFILE_FLAG_DEFAULT);
    if (unlikely(!pf_stat)) {
        error("Cannot open /proc/stat. Assuming system has %d processors. error: %s", __processors,
              strerror(errno));
        return __processors;
    }

    pf_stat = procfile_readall(pf_stat);
    if (unlikely(!pf_stat)) {
        error("Cannot read /proc/stat. Assuming system has %d __processors.", __processors);
        return __processors;
    }

    __processors = 0;

    for (size_t index = 0; index < procfile_lines(pf_stat); index++) {
        if (!procfile_linewords(pf_stat, index)) {
            continue;
        }

        if (strncmp(procfile_lineword(pf_stat, index, 0), "cpu", 3) == 0) {
            __processors++;
        }
    }

    __processors--;
    if (__processors < 1) {
        __processors = 1;
    }

    procfile_close(pf_stat);

    debug("System has %d __processors.", __processors);

    return __processors;
}

int32_t read_tcp_mem(uint64_t *low, uint64_t *pressure, uint64_t *high) {
    int32_t ret = 0;
    char    rd_buffer[1024] = { 0 };
    char   *start = NULL, *end = NULL;

    ret = read_file("/proc/sys/net/ipv4/tcp_mem", rd_buffer, 1023);
    if (unlikely(ret < 0)) {
        return ret;
    }

    start = rd_buffer;
    // *解析C-stringstr将其内容解释为指定内容的整数base，它以type的值形式返回unsigned long long
    // *int。如果endptr不是空指针，该函数还会设置endptr指向数字后的第一个字符。
    *low = strtoull(start, &end, 10);

    start = end;
    *pressure = strtoull(start, &end, 10);

    start = end;
    *high = strtoull(start, &end, 10);

    debug("TCP MEM low = %lu, pressure = %lu, high = %lu", *low, *pressure, *high);

    return 0;
}

__always_inline int32_t read_tcp_max_orphans(uint64_t *tcp_max_orphans) {
    if (unlikely(read_file_to_uint64("/proc/sys/net/ipv4/tcp_max_orphans", tcp_max_orphans) < 0)) {
        return -1;
    }

    debug("TCP max orphans = %lu", *tcp_max_orphans);
    return 0;
}

/**
 * Get the command line of a process
 *
 * @param pid The process ID of the process you want to get the name of.
 * @param name The name of the process.
 * @param name_size The size of the buffer that will hold the process name.
 *
 * @return The process name.
 */
int32_t get_process_name(pid_t pid, char *name, size_t name_size) {
    char              *filename;
    FILE              *f;
    int32_t            rc = 0;
    static const char *unknown_cmdline = "<unknown>";

    if (asprintf(&filename, "/proc/%d/cmdline", pid) < 0) {
        rc = 1;
        goto exit;
    }

    f = fopen(filename, "r");
    if (f == NULL) {
        rc = 2;
        goto releasefilename;
    }

    if (fgets(name, name_size, f) == NULL) {
        rc = 3;
        goto closefile;
    }

closefile:
    (void)fclose(f);
releasefilename:
    free(filename);
exit:
    if (rc != 0) {
        /*
         * The process went away before we could read its process name. Try
         * to give the user "<unknown>" here, but otherwise they get to look
         * at a blank.
         */
        if (strlcpy(name, unknown_cmdline, name_size) >= name_size) {
            rc = 4;
        }
    }

    return rc;
}
