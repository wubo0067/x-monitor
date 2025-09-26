/*
 * @Author: CALM.WU
 * @Date: 2025-09-26 15:25:40
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2025-09-26 16:30:39
 */

#pragma once

#include "xm_bpf_helpers_maps.h"

struct sock_key {
	uint32_t sip4; // network byte order
	uint32_t dip4; // network byte order
	uint32_t sport; // host byte order
	uint32_t dport; // network byte order
	uint32_t family;
};

BPF_SOCK_HASH(xm_sock_redir_hash, struct sock_key, int32_t, 65535);