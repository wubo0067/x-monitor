/*
 * @Author: CALM.WU
 * @Date: 2022-10-31 15:52:09
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2025-10-11 15:10:06
 */

#pragma once

#include <bpf/bpf_helpers.h>
#include <asm-generic/errno.h>

#define BPF_MAP(_name, _type, _key_type, _value_type, _max_entries)            \
	struct {                                                               \
		__uint(type, _type);                                           \
		__uint(max_entries, _max_entries);                             \
		__type(key, _key_type);                                        \
		__type(value, _value_type);                                    \
	} _name SEC(".maps");

#define BPF_HASH(_name, _key_type, _value_type, _max_entries)                  \
	BPF_MAP(_name, BPF_MAP_TYPE_HASH, _key_type, _value_type, _max_entries)

#define BPF_PERCPU_HASH(_name, _key_type, _value_type, _max_entries)           \
	BPF_MAP(_name, BPF_MAP_TYPE_PERCPU_HASH, _key_type, _value_type,       \
		_max_entries)

#define BPF_LRU_HASH(_name, _key_type, _value_type, _max_entries)              \
	BPF_MAP(_name, BPF_MAP_TYPE_LRU_HASH, _key_type, _value_type,          \
		_max_entries)

#define BPF_ARRAY(_name, _value_type, _max_entries)                            \
	BPF_MAP(_name, BPF_MAP_TYPE_ARRAY, __u32, _value_type, _max_entries)

#define BPF_PERCPU_ARRAY(_name, _value_type, _max_entries)                     \
	BPF_MAP(_name, BPF_MAP_TYPE_PERCPU_ARRAY, __u32, _value_type,          \
		_max_entries)

#define BPF_PROG_ARRAY(_name, _max_entries)                                    \
	BPF_MAP(_name, BPF_MAP_TYPE_PROG_ARRAY, __u32, __u32, _max_entries)

#define BPF_PERF_OUTPUT(_name, _max_entries)                                   \
	BPF_MAP(_name, BPF_MAP_TYPE_PERF_EVENT_ARRAY, int, __u32, _max_entries)

#define BPF_SOCK_HASH(_name, _key_type, _value_type, _max_entries)             \
	BPF_MAP(_name, BPF_MAP_TYPE_SOCKHASH, _key_type, _value_type,          \
		_max_entries)

#define BPF_CGROUP_STORAGE(_name, _value_type)                                 \
	struct {                                                               \
		__uint(type, BPF_MAP_TYPE_CGROUP_STORAGE);                     \
		__type(key, struct bpf_cgroup_storage_key);                    \
		__type(value, _value_type);                                    \
	} _name SEC(".maps");

#ifndef PERF_MAX_STACK_DEPTH
#define PERF_MAX_STACK_DEPTH 127
#endif

typedef __u64 stack_trace_type[PERF_MAX_STACK_DEPTH];
#define BPF_STACK_TRACE(_name, _max_entries)                                   \
	BPF_MAP(_name, BPF_MAP_TYPE_STACK_TRACE, __u32, stack_trace_type,      \
		_max_entries);

/**
 * Looks up a value in a map and initializes it if it doesn't exist.
 *
 * @param map The map to look up the value in.
 * @param key The key to look up the value with.
 * @param init_val The value to initialize the map with if it doesn't exist.
 *
 * @returns The value in the map, or the value initialized by init_val if the
 *          map didn't exist.
 */
static void *__xm_bpf_map_lookup_or_try_init(void *map, const void *key,
					     const void *init_val)
{
	void *val = bpf_map_lookup_elem(map, key);
	if (val) {
		return val;
	}

	__s32 err_no = bpf_map_update_elem(map, key, init_val, BPF_NOEXIST);
	if (err_no && err_no != -EEXIST) {
		return 0;
	}
	return bpf_map_lookup_elem(map, key);
}

// This code increments a counter in a BPF map.  The map is indexed by key and
// the counter is incremented by inc. If the map does not contain a counter for
// the given key, a new counter is created and initialized to zero. The return
// value is the new counter value. The map is assumed to be of type
// BPF_MAP_TYPE_ARRAY.
static __s64 __xm_bpf_map_increment(void *map, const void *key, __u64 inc)
{
	__u64 zero = 0;
	__u64 *count = (__u64 *)bpf_map_lookup_elem(map, key);
	if (!count) {
		bpf_map_update_elem(map, key, &zero, BPF_NOEXIST);
		count = (__u64 *)bpf_map_lookup_elem(map, key);
		if (!count) {
			return 0;
		}
	}
	__sync_fetch_and_add(count, inc);
	return *((__s64 *)count);
}