/**
 * @file protocol/packet_helpers.h
 * @ingroup protocol_helpers
 * @brief 🔧 Shared packet validation macros for protocol handlers
 *
 * Provides reusable validation macros to reduce code duplication
 * in packet handler implementations.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#pragma once

#include "common.h"

/**
 * @defgroup packet_helpers Packet Validation Helpers
 * @ingroup module_core
 * @brief Macros for packet validation and error handling
 * @{
 */

/**
 * @brief Validate packet payload is not NULL
 *
 * Checks that packet data pointer is not NULL. If NULL, calls the
 * disconnect function with an error message.
 *
 * @param data Packet payload pointer
 * @param packet_type String name of packet type (e.g., "CLIENT_JOIN")
 * @param disconnect_fn Function to call on validation failure, takes (client_t*, const char*)
 *
 * @return true if validation fails (client will be disconnected)
 *
 * @par Example:
 * @code{.c}
 * void handle_client_join_packet(client_info_t *client, const void *data, size_t len) {
 *   if (VALIDATE_PACKET_NOT_NULL(data, "CLIENT_JOIN", disconnect_client_for_bad_data)) {
 *     return;
 *   }
 *   // ... rest of handler
 * }
 * @endcode
 */
#define VALIDATE_PACKET_NOT_NULL(data, packet_type, disconnect_fn) \
  ((data) == NULL ? (disconnect_fn(client, packet_type " payload missing"), true) : false)

/**
 * @brief Validate packet payload size matches expected size
 *
 * Checks that the packet data length matches the expected structure size.
 * If mismatch, calls the disconnect function with a detailed error message.
 *
 * @param len Actual packet payload length
 * @param expected_type Type whose size() should match the payload
 * @param packet_type String name of packet type (e.g., "CLIENT_JOIN")
 * @param disconnect_fn Function to call on validation failure
 *
 * @return true if validation fails (client will be disconnected)
 *
 * @par Example:
 * @code{.c}
 * if (VALIDATE_PACKET_SIZE(len, client_info_packet_t, "CLIENT_JOIN", disconnect_client_for_bad_data)) {
 *   return;
 * }
 * @endcode
 */
#define VALIDATE_PACKET_SIZE(len, expected_type, packet_type, disconnect_fn)           \
  ((len) != sizeof(expected_type)                                                      \
       ? (disconnect_fn(client, packet_type " payload size %zu (expected %zu)", (len), \
                        sizeof(expected_type)),                                        \
          true)                                                                        \
       : false)

/**
 * @brief Validate both payload and size in one macro
 *
 * Combines VALIDATE_PACKET_NOT_NULL and VALIDATE_PACKET_SIZE checks.
 * If either check fails, returns early.
 *
 * @param data Packet payload pointer
 * @param len Actual packet payload length
 * @param expected_type Type whose size() should match the payload
 * @param packet_type String name of packet type (e.g., "CLIENT_JOIN")
 * @param disconnect_fn Function to call on validation failure
 *
 * @par Example:
 * @code{.c}
 * void handle_client_join_packet(client_info_t *client, const void *data, size_t len) {
 *   VALIDATE_PACKET(data, len, client_info_packet_t, "CLIENT_JOIN", disconnect_client_for_bad_data);
 *   const client_info_packet_t *info = (const client_info_packet_t *)data;
 *   // ... rest of handler
 * }
 * @endcode
 */
#define VALIDATE_PACKET(data, len, expected_type, packet_type, disconnect_fn) \
  do {                                                                        \
    if (VALIDATE_PACKET_NOT_NULL((data), (packet_type), (disconnect_fn))) {  \
      return;                                                                \
    }                                                                         \
    if (VALIDATE_PACKET_SIZE((len), expected_type, (packet_type), (disconnect_fn))) { \
      return;                                                                \
    }                                                                         \
  } while (0)

/** @} */

#pragma once
