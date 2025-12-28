/**
 * @file packet_utils.h
 * @brief Packet serialization and deserialization utilities
 *
 * This module consolidates repeated byte-order conversion patterns for packet
 * headers and payloads. It eliminates duplicated memcpy + htonl/ntohl patterns
 * that appear in multiple protocol handlers.
 *
 * Instead of:
 * @code
 *   memcpy(&header, data, sizeof(packet_header_t));
 *   header.width = ntohl(header.width);
 *   header.height = ntohl(header.height);
 *   header.size = ntohl(header.size);
 * @endcode
 *
 * Use:
 * @code
 *   packet_header_t header;
 *   packet_deserialize_frame_header(&header, data);
 * @endcode
 *
 * @ingroup network
 */

#ifndef LIB_NETWORK_PACKET_UTILS_H
#define LIB_NETWORK_PACKET_UTILS_H

#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>

/**
 * @brief ASCII frame packet header structure
 *
 * Represents the serialized format of an ASCII frame packet header
 * with all fields in network byte order.
 */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t original_size;
    uint32_t compressed_size;
    uint32_t checksum;
    uint32_t flags;
} __attribute__((packed)) packet_ascii_frame_header_t;

/**
 * @brief Deserialize ASCII frame header from network format
 *
 * Extracts ASCII frame header from packet data and converts all fields
 * from network byte order (big-endian) to host byte order.
 *
 * @param[out] header Pointer to header structure to populate
 * @param[in] data Packet data buffer
 * @param[in] len Length of packet data (must be >= sizeof header)
 *
 * @return 0 on success, -1 if buffer too small
 *
 * @note Assumes data contains at least sizeof(packet_ascii_frame_header_t) bytes
 *
 * @ingroup network
 */
int packet_deserialize_ascii_frame_header(packet_ascii_frame_header_t *header, const void *data, size_t len);

/**
 * @brief Serialize ASCII frame header to network format
 *
 * Converts ASCII frame header fields from host byte order to network byte order
 * and writes to packet data buffer.
 *
 * @param[out] data Packet data buffer to write to
 * @param[in] len Length of buffer (must be >= sizeof header)
 * @param[in] header Pointer to header with host byte order fields
 *
 * @return 0 on success, -1 if buffer too small
 *
 * @ingroup network
 */
int packet_serialize_ascii_frame_header(void *data, size_t len, const packet_ascii_frame_header_t *header);

/**
 * @brief Deserialize image dimensions from network format
 *
 * Extracts width and height fields from packet header and converts from
 * network byte order. Useful for image frame packets.
 *
 * @param[out] width Pointer to width value
 * @param[out] height Pointer to height value
 * @param[in] data Packet data buffer (must contain at least 8 bytes)
 * @param[in] len Length of buffer
 *
 * @return 0 on success, -1 if buffer too small
 *
 * @ingroup network
 */
int packet_deserialize_dimensions(uint32_t *width, uint32_t *height, const void *data, size_t len);

/**
 * @brief Serialize image dimensions to network format
 *
 * Writes width and height fields to packet header in network byte order.
 * Useful for image frame packets.
 *
 * @param[out] data Packet data buffer
 * @param[in] len Length of buffer (must be >= 8 bytes)
 * @param[in] width Width value in host byte order
 * @param[in] height Height value in host byte order
 *
 * @return 0 on success, -1 if buffer too small
 *
 * @ingroup network
 */
int packet_serialize_dimensions(void *data, size_t len, uint32_t width, uint32_t height);

/**
 * @brief Deserialize a uint32_t field from network byte order
 *
 * Convenience function for extracting a single uint32_t field from
 * packet data at the given offset.
 *
 * @param[out] value Pointer to store deserialized value
 * @param[in] data Packet data buffer
 * @param[in] len Buffer length
 * @param[in] offset Offset within buffer
 *
 * @return 0 on success, -1 if out of bounds
 *
 * @ingroup network
 */
static inline int packet_deserialize_uint32(uint32_t *value, const void *data, size_t len, size_t offset) {
    if (!value || !data || offset + sizeof(uint32_t) > len) {
        return -1;
    }
    const uint8_t *ptr = (const uint8_t *)data + offset;
    uint32_t network_value;
    memcpy(&network_value, ptr, sizeof(uint32_t));
    *value = ntohl(network_value);
    return 0;
}

/**
 * @brief Serialize a uint32_t field to network byte order
 *
 * Convenience function for writing a single uint32_t field to packet
 * data at the given offset in network byte order.
 *
 * @param[out] data Packet data buffer
 * @param[in] len Buffer length
 * @param[in] offset Offset within buffer
 * @param[in] value Value to serialize (host byte order)
 *
 * @return 0 on success, -1 if out of bounds
 *
 * @ingroup network
 */
static inline int packet_serialize_uint32(void *data, size_t len, size_t offset, uint32_t value) {
    if (!data || offset + sizeof(uint32_t) > len) {
        return -1;
    }
    uint8_t *ptr = (uint8_t *)data + offset;
    uint32_t network_value = htonl(value);
    memcpy(ptr, &network_value, sizeof(uint32_t));
    return 0;
}

#endif // LIB_NETWORK_PACKET_UTILS_H
