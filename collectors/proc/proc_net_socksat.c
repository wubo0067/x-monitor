/*
 * @Author: CALM.WU
 * @Date: 2022-02-24 11:17:30
 * @Last Modified by: calmwu
 * @Last Modified time: 2022-02-28 19:59:33
 */

#include "prometheus-client-c/prom.h"

#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/strings.h"
#include "utils/clocks.h"

#include "appconfig/appconfig.h"

static const char       *__proc_net_socksat_filename = "/proc/net/sockstat";
static struct proc_file *__pf_net_socksat = NULL;

/*
两种情况会出发 “Out of socket memory” 的信息：
1.有很多的孤儿套接字(orphan sockets)
2.tcp socket 用尽了给他分配的内存
*/

static struct proc_net_sockstat {
    uint64_t sockets_used;   // 已使用的所有协议套接字总量

    uint64_t tcp_inuse;   // 正在使用（正在侦听）的TCP套接字数量 netstat –lnt | grep ^tcp | wc –l
    uint64_t tcp_orphan;   // 无主（不属于任何进程）的TCP连接数（无用、待销毁的TCP socket数）
    uint64_t tcp_tw;   // 等待关闭的TCP连接数。其值等于netstat –ant | grep TIME_WAIT | wc –l
    uint64_t tcp_alloc;   // 已分配（已建立、已申请到sk_buff）的TCP套接字数量。其值等于netstat –ant
                          // | grep ^tcp | wc –l
    uint64_t tcp_mem;   // 套接字缓冲区使用量 套接字缓冲区使用量（单位页） the socket descriptors
                        // in-kernel send queues (stuff waiting to be sent out by the NIC) in-kernel
                        // receive queues (stuff that's been received, but hasn't yet been read by
                        // user space yet).

    uint64_t udp_inuse;   // 正在使用的UDP套接字数量
    uint64_t udp_mem;

    uint64_t raw_inuse;

    uint64_t frag_mem;
    uint64_t frag_memory;
} __proc_net_sockstat = { 0 };

int32_t init_collector_proc_net_socksat() {
    int32_t ret = 0;
    return ret;
}

int32_t collector_proc_net_socksat(int32_t update_every, usec_t dt, const char *config_path) {
    int32_t ret = 0;
    return ret;
}

void fini_collector_proc_net_socksat() {
}