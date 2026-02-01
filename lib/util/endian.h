/**
 * @file util/endian.h
 * @brief 🔄 Network byte order conversion helpers
 * @ingroup util
 *
 * Provides convenient macros for converting between host and network byte order.
 * Reduces repetitive htonl/ntohl calls and makes intent clearer.
 *
 * Common Pattern:
 * Throughout the protocol handlers, we convert numeric values between host and
 * network byte order for transmission. This creates repetitive patterns like:
 *
 * Bad (repetitive):
 * @code
 * uint32_t width_net = htonl(img_width);
 * uint32_t height_net = htonl(img_height);
 * uint32_t channels_net = htonl(img_channels);
 * @endcode
 *
 * Good (using these helpers):
 * @code
 * uint32_t width_net = HOST_TO_NET_U32(img_width);
 * uint32_t height_net = HOST_TO_NET_U32(img_height);
 * uint32_t channels_net = HOST_TO_NET_U32(img_channels);
 * @endcode
 *
 * Usage:
 * @code
 * // Sending frame header
 * uint32_t width = frame->width;
 * uint32_t height = frame->height;
 * uint32_t size = frame->data_size;
 *
 * frame_header_t hdr = {
 *     .width = HOST_TO_NET_U32(width),
 *     .height = HOST_TO_NET_U32(height),
 *     .data_size = HOST_TO_NET_U32(size),
 * };
 *
 * // Receiving and unpacking
 * const frame_header_t *hdr = (const frame_header_t *)packet_data;
 * uint32_t width = NET_TO_HOST_U32(hdr->width);
 * uint32_t height = NET_TO_HOST_U32(hdr->height);
 * uint32_t size = NET_TO_HOST_U32(hdr->data_size);
 * @endcode
 */

#pragma once

#include <stdint.h>
#include "platform/network.h"

/**
 * Convert a 32-bit value from host byte order to network byte order.
 * Typically used when preparing data for transmission.
 *
 * @param val 32-bit unsigned integer in host byte order
 * @return Same value in network byte order (big-endian)
 *
 * Usage:
 * @code
 * uint32_t count = 42;
 * packet.count = HOST_TO_NET_U32(count);
 * @endcode
 */
#define HOST_TO_NET_U32(val) htonl((val))

/**
 * Convert a 32-bit value from network byte order to host byte order.
 * Typically used when receiving data from the network.
 *
 * @param val 32-bit unsigned integer in network byte order
 * @return Same value in host byte order
 *
 * Usage:
 * @code
 * const packet_t *pkt = (const packet_t *)data;
 * uint32_t count = NET_TO_HOST_U32(pkt->count);
 * @endcode
 */
#define NET_TO_HOST_U32(val) ntohl((val))

/**
 * Convert a 16-bit value from host byte order to network byte order.
 * Typically used for port numbers and other 16-bit fields.
 *
 * @param val 16-bit unsigned integer in host byte order
 * @return Same value in network byte order (big-endian)
 *
 * Usage:
 * @code
 * uint16_t port = 27224;
 * addr.sin_port = HOST_TO_NET_U16(port);
 * @endcode
 */
#define HOST_TO_NET_U16(val) htons((val))

/**
 * Convert a 16-bit value from network byte order to host byte order.
 * Typically used for port numbers and other 16-bit fields.
 *
 * @param val 16-bit unsigned integer in network byte order
 * @return Same value in host byte order
 *
 * Usage:
 * @code
 * const addr_t *addr = ...;
 * uint16_t port = NET_TO_HOST_U16(addr->sin_port);
 * @endcode
 */
#define NET_TO_HOST_U16(val) ntohs((val))

/**
 * Convert an array of 32-bit values from host to network byte order in-place.
 * Modifies the array directly.
 *
 * @param arr Pointer to array of uint32_t values
 * @param count Number of elements to convert
 *
 * Usage:
 * @code
 * uint32_t data[10] = {...};
 * CONVERT_ARRAY_HOST_TO_NET_U32(data, 10);
 * // Now all values in 'data' are in network byte order
 * @endcode
 */
#define CONVERT_ARRAY_HOST_TO_NET_U32(arr, count)                                                                      \
  do {                                                                                                                 \
    for (size_t i = 0; i < (count); i++) {                                                                             \
      (arr)[i] = htonl((arr)[i]);                                                                                      \
    }                                                                                                                  \
  } while (0)

/**
 * Convert an array of 32-bit values from network to host byte order in-place.
 * Modifies the array directly.
 *
 * @param arr Pointer to array of uint32_t values
 * @param count Number of elements to convert
 *
 * Usage:
 * @code
 * uint32_t data[10];
 * memcpy(data, packet_data, sizeof(data));
 * CONVERT_ARRAY_NET_TO_HOST_U32(data, 10);
 * // Now all values in 'data' are in host byte order
 * @endcode
 */
#define CONVERT_ARRAY_NET_TO_HOST_U32(arr, count)                                                                      \
  do {                                                                                                                 \
    for (size_t i = 0; i < (count); i++) {                                                                             \
      (arr)[i] = ntohl((arr)[i]);                                                                                      \
    }                                                                                                                  \
  } while (0)

/**
 * Unpack a 32-bit value from network byte order.
 * Takes a uint32_t in network order and returns it in host order.
 *
 * @param val 32-bit unsigned integer in network byte order
 * @return Same value in host byte order
 */
static inline uint32_t endian_unpack_u32(uint32_t val) {
  return ntohl(val);
}

/**
 * Unpack a 16-bit value from network byte order.
 * Takes a uint16_t in network order and returns it in host order.
 *
 * @param val 16-bit unsigned integer in network byte order
 * @return Same value in host byte order
 */
static inline uint16_t endian_unpack_u16(uint16_t val) {
  return ntohs(val);
}

/**
 * Pack a 32-bit value to network byte order.
 * Takes a uint32_t in host order and returns it in network order.
 *
 * @param val 32-bit unsigned integer in host byte order
 * @return Same value in network byte order
 */
static inline uint32_t endian_pack_u32(uint32_t val) {
  return htonl(val);
}

/**
 * Pack a 16-bit value to network byte order.
 * Takes a uint16_t in host order and returns it in network order.
 *
 * @param val 16-bit unsigned integer in host byte order
 * @return Same value in network byte order
 */
static inline uint16_t endian_pack_u16(uint16_t val) {
  return htons(val);
}

/**
 * Convert a 64-bit value from host byte order to network byte order.
 * Typically used when preparing data for transmission.
 *
 * @param val 64-bit unsigned integer in host byte order
 * @return Same value in network byte order (big-endian)
 *
 * Usage:
 * @code
 * uint64_t magic = 0xA5C11C4A1;
 * packet.magic = HOST_TO_NET_U64(magic);
 * @endcode
 */
#define HOST_TO_NET_U64(val) htonll((val))

/**
 * Convert a 64-bit value from network byte order to host byte order.
 * Typically used when receiving data from the network.
 *
 * @param val 64-bit unsigned integer in network byte order
 * @return Same value in host byte order
 *
 * Usage:
 * @code
 * const packet_t *pkt = (const packet_t *)data;
 * uint64_t magic = NET_TO_HOST_U64(pkt->magic);
 * @endcode
 */
#define NET_TO_HOST_U64(val) ntohll((val))

/**
 * Unpack a 64-bit value from network byte order.
 * Takes a uint64_t in network order and returns it in host order.
 *
 * @param val 64-bit unsigned integer in network byte order
 * @return Same value in host byte order
 */
static inline uint64_t endian_unpack_u64(uint64_t val) {
  return ntohll(val);
}

/**
 * Pack a 64-bit value to network byte order.
 * Takes a uint64_t in host order and returns it in network order.
 *
 * @param val 64-bit unsigned integer in host byte order
 * @return Same value in network byte order
 */
static inline uint64_t endian_pack_u64(uint64_t val) {
  return htonll(val);
}
