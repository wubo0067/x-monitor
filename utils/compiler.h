/*
 * @Author: CALM.WU
 * @Date: 2021-10-14 16:33:29
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-01-24 15:32:20
 */

#pragma once

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

// linux kernel的ARRAY_SIZE
// macro為了預防傳入的變數是pointer而不是真正的array，背後其實花了很多心力來防範這件事，使得如果發生問題就會在編譯時期就失敗，避免了因為誤用而在run-time才發生問題的狀況。
#define __same_type(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))
#define BUILD_BUG_ON_ZERO(e) (sizeof(struct { int : -!!(e); }))
#define __must_be_array(a) BUILD_BUG_ON_ZERO(__same_type((a), &(a)[0]))
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))

// 定义纯函数
#ifndef __pure
#define __pure __attribute__((pure))
#endif

// 强制内嵌函数
#ifndef __always_inline
#define __always_inline inline __attribute__((always_inline))
#endif

#ifdef __GNUC__
#define UNUSED(x) UNUSED_##x __attribute__((__unused__))
#else
#define UNUSED(x) UNUSED_##x
#endif

#ifdef __GNUC__
#define UNUSED_FUNCTION(x) __attribute__((__unused__)) UNUSED_##x
#else
#define UNUSED_FUNCTION(x) UNUSED_##x
#endif

#ifndef __concat
#define __concat(a, b) a##b
#endif

#if !defined ROUNDUP
#define ROUNDUP(X, Y) (((X) + ((Y)-1)) & ~((Y)-1))
#endif