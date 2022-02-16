/*
 * @Author: CALM.WU
 * @Date: 2022-01-18 11:31:35
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-01-18 12:00:28
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

extern const char *get_hostname();

extern const char *get_ipaddr_by_iface(const char *iface, char *ip_buf, size_t ip_buf_size);

extern const char *get_macaddr_by_iface(const char *iface, char *mac_buf, size_t mac_buf_size);

#ifdef __cplusplus
}
#endif