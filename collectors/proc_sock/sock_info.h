/*
 * @Author: CALM.WU
 * @Date: 2022-11-15 15:07:39
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-11-18 16:38:33
 */

#pragma once

#include <stdint.h>

#include "urcu/urcu-memb.h"
#include "urcu/rculfhash.h"

#include "utils/atomic.h"

// SOCK 类型
enum SOCK_TYPE {
	ST_UNKNOWN,
	ST_TCP,
	ST_TCP6,
	ST_UDP,
	ST_UDP6,
	ST_UNIX,
	ST_MAX
};

// https://github.com/torvalds/linux/blob/master/include/uapi/linux/net.h#L48
// SOCK 状态
enum SOCK_STATE {
	SS_UNKNOWN = 0,
	SS_ESTABLISHED = 1, //
	SS_SYN_SENT,
	SS_SYN_RECV,
	SS_FIN_WAIT1,
	SS_FIN_WAIT2,
	SS_TIME_WAIT, // 不归属某个进程
	SS_CLOSE,
	SS_CLOSE_WAIT,
	SS_LAST_ACK,
	SS_LISTEN, // 0A
	SS_CLOSING, /* Now a valid state */
	SS_MAX_STATES /* Leave at the end! */
};

struct sock_info {
	uint32_t ino; // sock inode
	enum SOCK_TYPE sock_type;
	enum SOCK_STATE sock_state;
};