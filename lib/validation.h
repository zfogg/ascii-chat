/**
 * @defgroup validation Validation Helpers
 * @ingroup module_core
 * @brief 🛡️ Reusable validation macros for protocol handlers
 *
 * @file validation.h
 * @brief 🛡️ Common validation macros to reduce duplication in protocol handlers
 * @ingroup validation
 * @addtogroup validation
 * @{
 *
 * Provides standardized validation macros for protocol handlers to:
 * - Check for NULL pointers
 * - Validate payload sizes
 * - Verify client state flags
 * - Reduce code duplication across handlers
 *
 * All macros call disconnect_client_for_bad_data() on validation failure
 * and return from the calling function immediately.
 *
 * Usage:
 * @code
 * void handle_packet(client_info_t *client, const void *data, size_t len) {
 *   VALIDATE_NOTNULL_DATA(client, data, "PACKET_TYPE");
 *   VALIDATE_MIN_SIZE(client, len, 16, "PACKET_TYPE");
 *   VALIDATE_AUDIO_STREAM_ENABLED(client, "PACKET_TYPE");
 *   // ... continue processing ...
 * }
 * @endcode
 */

#pragma once

#include "common.h"  // For error handling

/* Forward declarations to avoid circular dependencies */
typedef struct client_info client_info_t;

/**
 * Validate that payload data pointer is not NULL.
 * Disconnects client and returns if data is NULL.
 *
 * @param client Client info pointer
 * @param data Payload data pointer to validate
 * @param packet_name Name of packet type for error message
 */
#define VALIDATE_NOTNULL_DATA(client, data, packet_name) \
  do { \
    if (!(data)) { \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...); \
      disconnect_client_for_bad_data((client), "%s payload missing", (packet_name)); \
      return; \
    } \
  } while (0)

/**
 * Validate that payload size is at least the minimum required.
 * Disconnects client and returns if len < min_size.
 *
 * @param client Client info pointer
 * @param len Actual payload length
 * @param min_size Minimum required length
 * @param packet_name Name of packet type for error message
 */
#define VALIDATE_MIN_SIZE(client, len, min_size, packet_name) \
  do { \
    if ((len) < (min_size)) { \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...); \
      disconnect_client_for_bad_data((client), "%s payload too small (len=%zu, min=%zu)", (packet_name), \
                                     (len), (min_size)); \
      return; \
    } \
  } while (0)

/**
 * Validate that payload size is exactly the expected size.
 * Disconnects client and returns if len != expected_size.
 *
 * @param client Client info pointer
 * @param len Actual payload length
 * @param expected_size Expected exact length
 * @param packet_name Name of packet type for error message
 */
#define VALIDATE_EXACT_SIZE(client, len, expected_size, packet_name) \
  do { \
    if ((len) != (expected_size)) { \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...); \
      disconnect_client_for_bad_data((client), "%s payload size mismatch (len=%zu, expected=%zu)", \
                                     (packet_name), (len), (expected_size)); \
      return; \
    } \
  } while (0)

/**
 * Validate that audio stream is enabled for this client.
 * Disconnects client and returns if audio stream is not enabled.
 *
 * @param client Client info pointer
 * @param packet_name Name of packet type for error message
 */
#define VALIDATE_AUDIO_STREAM_ENABLED(client, packet_name) \
  do { \
    if (!atomic_load(&(client)->is_sending_audio)) { \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...); \
      disconnect_client_for_bad_data((client), "%s received before audio stream enabled", (packet_name)); \
      return; \
    } \
  } while (0)

/**
 * Validate that audio sample count is within acceptable bounds.
 * Disconnects client and returns if sample count is invalid.
 *
 * @param client Client info pointer
 * @param num_samples Number of audio samples to validate
 * @param max_samples Maximum allowed samples
 * @param packet_name Name of packet type for error message
 */
#define VALIDATE_AUDIO_SAMPLE_COUNT(client, num_samples, max_samples, packet_name) \
  do { \
    if ((num_samples) <= 0 || (num_samples) > (max_samples)) { \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...); \
      disconnect_client_for_bad_data((client), "%s invalid sample count: %d (max %d)", (packet_name), \
                                     (num_samples), (max_samples)); \
      return; \
    } \
  } while (0)

/**
 * Validate that audio sample alignment is correct (must be multiple of sample size).
 * Disconnects client and returns if alignment is incorrect.
 *
 * @param client Client info pointer
 * @param len Payload length
 * @param sample_size Size of each sample (typically sizeof(float))
 * @param packet_name Name of packet type for error message
 */
#define VALIDATE_AUDIO_ALIGNMENT(client, len, sample_size, packet_name) \
  do { \
    if ((len) % (sample_size) != 0) { \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...); \
      disconnect_client_for_bad_data((client), "%s payload not aligned (len=%zu, sample_size=%zu)", \
                                     (packet_name), (len), (sample_size)); \
      return; \
    } \
  } while (0)

/**
 * Validate that a required resource/buffer is initialized.
 * Disconnects client and returns if resource is NULL.
 *
 * @param client Client info pointer
 * @param resource Pointer to resource to validate
 * @param resource_name Name of resource for error message
 */
#define VALIDATE_RESOURCE_INITIALIZED(client, resource, resource_name) \
  do { \
    if (!(resource)) { \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...); \
      disconnect_client_for_bad_data((client), "%s not initialized", (resource_name)); \
      return; \
    } \
  } while (0)

/**
 * Validate that an unsigned integer value is within acceptable range.
 * Disconnects client and returns if value is outside [min_val, max_val].
 *
 * @param client Client info pointer
 * @param value Value to validate
 * @param min_val Minimum acceptable value (inclusive)
 * @param max_val Maximum acceptable value (inclusive)
 * @param value_name Name of value for error message
 * @param packet_name Name of packet type for error message
 */
#define VALIDATE_RANGE_UNSIGNED(client, value, min_val, max_val, value_name, packet_name) \
  do { \
    if ((value) < (min_val) || (value) > (max_val)) { \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...); \
      disconnect_client_for_bad_data((client), \
                                     "%s invalid %s: %u (valid range: %u-%u)", (packet_name), \
                                     (value_name), (value), (min_val), (max_val)); \
      return; \
    } \
  } while (0)

/**
 * Validate terminal dimensions are within acceptable bounds.
 * Disconnects client and returns if width or height is invalid.
 * Prevents DoS attacks and rendering issues from extreme dimensions.
 *
 * @param client Client info pointer
 * @param width Terminal width in characters
 * @param height Terminal height in characters
 * @param packet_name Name of packet type for error message
 */
#define VALIDATE_TERMINAL_DIMENSIONS(client, width, height, packet_name) \
  do { \
    if ((width) < 8 || (width) > 512 || (height) < 4 || (height) > 256) { \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...); \
      disconnect_client_for_bad_data((client), \
                                     "%s invalid terminal dimensions: %ux%u (valid: 8-512 x 4-256)", \
                                     (packet_name), (width), (height)); \
      return; \
    } \
  } while (0)

/**
 * Validate that an integer enum value is one of the expected values.
 * Disconnects client and returns if value doesn't match any expected value.
 * Used for color_level, render_mode, palette_type, etc.
 *
 * @param client Client info pointer
 * @param value Enum value to validate
 * @param expected1 First valid enum value
 * @param expected2 Second valid enum value
 * @param expected3 Third valid enum value (or -1 if not needed)
 * @param field_name Name of enum field for error message
 * @param packet_name Name of packet type for error message
 */
#define VALIDATE_ENUM_VALUE(client, value, expected1, expected2, expected3, field_name, packet_name) \
  do { \
    int v = (int)(value); \
    int e1 = (int)(expected1); \
    int e2 = (int)(expected2); \
    int e3 = (int)(expected3); \
    if (v != e1 && v != e2 && (e3 < 0 || v != e3)) { \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...); \
      disconnect_client_for_bad_data((client), \
                                     "%s invalid %s enum value: %d", (packet_name), (field_name), v); \
      return; \
    } \
  } while (0)

/** @} */
