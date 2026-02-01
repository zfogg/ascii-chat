#pragma once

/**
 * @file platform/network.h
 * @brief Cross-platform network headers and socket definitions
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * Provides unified network header includes across Windows and POSIX platforms.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <stdint.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

/**
 * @brief 64-bit host to network byte order conversion
 *
 * Not all platforms provide htonll/ntohll, so we define portable versions.
 * Uses compiler builtins when available for optimal performance.
 */
#ifndef htonll
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define htonll(x) (x)
#define ntohll(x) (x)
#elif defined(__GNUC__) || defined(__clang__)
#define htonll(x) __builtin_bswap64(x)
#define ntohll(x) __builtin_bswap64(x)
#else
static inline uint64_t htonll(uint64_t val) {
  return ((val & 0x00000000000000FFULL) << 56) |
         ((val & 0x000000000000FF00ULL) << 40) |
         ((val & 0x0000000000FF0000ULL) << 24) |
         ((val & 0x00000000FF000000ULL) << 8) |
         ((val & 0x000000FF00000000ULL) >> 8) |
         ((val & 0x0000FF0000000000ULL) >> 24) |
         ((val & 0x00FF000000000000ULL) >> 40) |
         ((val & 0xFF00000000000000ULL) >> 56);
}
#define ntohll(x) htonll(x)
#endif
#endif

/** @} */
