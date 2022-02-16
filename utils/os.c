/*
 * @Author: CALM.WU
 * @Date: 2022-01-18 11:38:31
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-01-24 17:26:50
 */

#include "common.h"
#include "compiler.h"
#include "os.h"

static char __hostname[HOST_NAME_MAX + 1] = { 0 };

static const char *__def_ipaddr   = "0.0.0.0";
static const char *__def_macaddr  = "00:00:00:00:00:00";
static const char *__def_hostname = "unknown";

const char *get_hostname() {
    if (unlikely(0 == __hostname[0])) {
        if (unlikely(0 != gethostname(__hostname, HOST_NAME_MAX))) {
            strncpy(__hostname, __def_hostname, 7);
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
    strncpy(ip_buf, ip_addr, ip_buf_size - 1);
    ip_buf[ip_buf_size - 1] = '\0';

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

    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ioctl(fd, SIOCGIFHWADDR, &ifr);

    close(fd);

    uint8_t *mac = (uint8_t *)ifr.ifr_hwaddr.sa_data;

    snprintf(mac_buf, mac_buf_size - 1, "%.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n", mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
    mac_buf[mac_buf_size - 1] = '\0';

    return mac_buf;
}