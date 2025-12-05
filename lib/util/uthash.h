/**
 * @file util/uthash.h
 * @brief #️⃣ Wrapper for uthash.h that ensures common.h is included first
 * @ingroup util
 *
 * This header ensures that common.h is included before uthash.h throughout
 * the codebase. This is necessary because common.h defines HASH_FUNCTION
 * which must be set before uthash.h is included.
 *
 * @note Always include this instead of directly including uthash.h
 */

#pragma once

// Define these BEFORE including uthash.h
#define uthash_malloc(sz) ALLOC_MALLOC(sz)
#define uthash_free(ptr, sz) ALLOC_FREE(ptr)
// when common.h transitively includes lock_debug.h via platform/abstraction.h
#include <uthash.h>

#include "../common.h"
#include "util/fnv1a.h"

// UBSan-safe hash wrapper for uthash (fnv1a uses 64-bit arithmetic, no overflow)
// Note: uthash expects HASH_FUNCTION(keyptr, keylen, hashv) where hashv is an output parameter
#undef HASH_FUNCTION
#define HASH_FUNCTION(keyptr, keylen, hashv)                                                                           \
  do {                                                                                                                 \
    if (!(keyptr) || (keylen) == 0) {                                                                                  \
      (hashv) = 1; /* Non-zero constant for safety */                                                                  \
    } else {                                                                                                           \
      (hashv) = fnv1a_hash_bytes((keyptr), (keylen));                                                                  \
    }                                                                                                                  \
  } while (0)

// Redefine HASH_JEN_MIX to use safe arithmetic that doesn't trigger UBSan
// This prevents undefined behavior from left shifts that exceed uint32_t range
// The original uthash uses shifts that can overflow, but we fix it by using
// 64-bit arithmetic for intermediate calculations, then casting back to 32-bit
// Must be defined AFTER including uthash.h since uthash.h unconditionally defines it
#undef HASH_JEN_MIX
#define HASH_JEN_MIX(a, b, c)                                                                                          \
  do {                                                                                                                 \
    /* Use 64-bit arithmetic for shifts to avoid undefined behavior, then mask back to 32-bit */                       \
    uint64_t _a = (uint64_t)(a), _b = (uint64_t)(b), _c = (uint64_t)(c);                                               \
    _a -= _b;                                                                                                          \
    _a -= _c;                                                                                                          \
    _a ^= (_c >> 13);                                                                                                  \
    a = (unsigned)(_a & 0xFFFFFFFFU);                                                                                  \
                                                                                                                       \
    _b -= _c;                                                                                                          \
    _b -= (uint64_t)a;                                                                                                 \
    _b ^= (((uint64_t)a << 8) & 0xFFFFFFFFU);                                                                          \
    b = (unsigned)(_b & 0xFFFFFFFFU);                                                                                  \
                                                                                                                       \
    _c -= (uint64_t)a;                                                                                                 \
    _c -= (uint64_t)b;                                                                                                 \
    _c ^= ((uint64_t)b >> 13);                                                                                         \
    c = (unsigned)(_c & 0xFFFFFFFFU);                                                                                  \
                                                                                                                       \
    _a = (uint64_t)a;                                                                                                  \
    _a -= (uint64_t)b;                                                                                                 \
    _a -= (uint64_t)c;                                                                                                 \
    _a ^= ((uint64_t)c >> 12);                                                                                         \
    a = (unsigned)(_a & 0xFFFFFFFFU);                                                                                  \
                                                                                                                       \
    _b = (uint64_t)b;                                                                                                  \
    _b -= (uint64_t)c;                                                                                                 \
    _b -= (uint64_t)a;                                                                                                 \
    _b ^= (((uint64_t)a << 16) & 0xFFFFFFFFU);                                                                         \
    b = (unsigned)(_b & 0xFFFFFFFFU);                                                                                  \
                                                                                                                       \
    _c = (uint64_t)c;                                                                                                  \
    _c -= (uint64_t)a;                                                                                                 \
    _c -= (uint64_t)b;                                                                                                 \
    _c ^= ((uint64_t)b >> 5);                                                                                          \
    c = (unsigned)(_c & 0xFFFFFFFFU);                                                                                  \
                                                                                                                       \
    _a = (uint64_t)a;                                                                                                  \
    _a -= (uint64_t)b;                                                                                                 \
    _a -= (uint64_t)c;                                                                                                 \
    _a ^= ((uint64_t)c >> 3);                                                                                          \
    a = (unsigned)(_a & 0xFFFFFFFFU);                                                                                  \
                                                                                                                       \
    _b = (uint64_t)b;                                                                                                  \
    _b -= (uint64_t)c;                                                                                                 \
    _b -= (uint64_t)a;                                                                                                 \
    _b ^= (((uint64_t)a << 10) & 0xFFFFFFFFU);                                                                         \
    b = (unsigned)(_b & 0xFFFFFFFFU);                                                                                  \
                                                                                                                       \
    _c = (uint64_t)c;                                                                                                  \
    _c -= (uint64_t)a;                                                                                                 \
    _c -= (uint64_t)b;                                                                                                 \
    _c ^= ((uint64_t)b >> 15);                                                                                         \
    c = (unsigned)(_c & 0xFFFFFFFFU);                                                                                  \
  } while (0)
