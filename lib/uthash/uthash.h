/**
 * @file uthash/uthash.h
 * @brief #️⃣ Wrapper for uthash.h with ascii-chat customizations
 * @ingroup util
 *
 * This header wraps the upstream uthash.h with ascii-chat specific customizations:
 * - Custom allocators (ALLOC_MALLOC/ALLOC_FREE) for memory debugging
 * - UBSan-safe hash functions to avoid undefined behavior
 * - FNV-1a hash function for better distribution
 *
 * @note Always include this instead of directly including upstream uthash.h
 *
 * Installation layout:
 *   include/ascii-chat/uthash/uthash.h      (this wrapper)
 *   include/ascii-chat/uthash/upstream/     (raw uthash headers)
 */

#pragma once

// Define custom allocators BEFORE including uthash.h
// These integrate with ascii-chat's memory debugging system
#define uthash_malloc(sz) ALLOC_MALLOC(sz)
#define uthash_free(ptr, sz) ALLOC_FREE(ptr)

// Include the upstream uthash header
#include "upstream/uthash.h"

// Include ascii-chat headers for allocators and hash function
#include "../common.h"
#include "../util/fnv1a.h"

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
