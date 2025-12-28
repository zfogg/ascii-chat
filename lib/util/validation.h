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

#include "../common.h"  // For error handling

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
 * Validate packet payload size and presence.
 * Helper macro to standardize packet validation. Checks if data pointer is
 * non-NULL and payload matches expected size. Disconnects client on failure.
 *
 * @param client Client being validated (must not be NULL)
 * @param data Payload pointer
 * @param len Payload size
 * @param expected_size Expected payload size
 * @param packet_name Human-readable packet name for error messages
 *
 * @note This macro uses 'return;' statement - only use inside void functions
 * @note Automatically calls disconnect_client_for_bad_data() on failure
 */
#define VALIDATE_PACKET_SIZE(client, data, len, expected_size, packet_name) \
  do { \
    if (!(data)) { \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...); \
      disconnect_client_for_bad_data((client), packet_name " payload missing"); \
      return; \
    } \
    if ((len) != (expected_size)) { \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...); \
      disconnect_client_for_bad_data((client), packet_name " payload size %zu (expected %zu)", (len), \
                                     (expected_size)); \
      return; \
    } \
  } while (0)

/**
 * Validate that a numeric value is non-zero (for dimensions, counts, etc).
 * Disconnects client and returns if value is zero.
 *
 * @param client Client info pointer
 * @param value Value to validate
 * @param value_name Name of value for error message
 * @param packet_name Name of packet type for error message
 */
#define VALIDATE_NONZERO(client, value, value_name, packet_name) \
  do { \
    if ((value) == 0) { \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...); \
      disconnect_client_for_bad_data((client), "%s %s cannot be zero", (packet_name), (value_name)); \
      return; \
    } \
  } while (0)

/**
 * Validate that a numeric value is within a specified range (inclusive).
 * Disconnects client and returns if value is outside range.
 *
 * @param client Client info pointer
 * @param value Value to validate
 * @param min_val Minimum allowed value (inclusive)
 * @param max_val Maximum allowed value (inclusive)
 * @param value_name Name of value for error message
 * @param packet_name Name of packet type for error message
 */
#define VALIDATE_RANGE(client, value, min_val, max_val, value_name, packet_name) \
  do { \
    if ((value) < (min_val) || (value) > (max_val)) { \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...); \
      disconnect_client_for_bad_data((client), "%s %s out of range: %u (valid: %u-%u)", (packet_name), \
                                     (value_name), (unsigned)(value), (unsigned)(min_val), (unsigned)(max_val)); \
      return; \
    } \
  } while (0)

/**
 * Validate that at least one capability flag is set.
 * Disconnects client and returns if all flags are zero or invalid.
 *
 * @param client Client info pointer
 * @param flags Capability flags value
 * @param valid_mask Bitmask of valid flag bits
 * @param packet_name Name of packet type for error message
 */
#define VALIDATE_CAPABILITY_FLAGS(client, flags, valid_mask, packet_name) \
  do { \
    if (((flags) & (valid_mask)) == 0) { \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...); \
      disconnect_client_for_bad_data((client), "%s no valid capability flags set (flags=0x%x, valid=0x%x)", \
                                     (packet_name), (unsigned)(flags), (unsigned)(valid_mask)); \
      return; \
    } \
  } while (0)

/**
 * Validate that only known/valid flags are set.
 * Disconnects client and returns if unknown flags are present.
 *
 * @param client Client info pointer
 * @param flags Flags value to validate
 * @param valid_mask Bitmask of valid flag bits
 * @param packet_name Name of packet type for error message
 */
#define VALIDATE_FLAGS_MASK(client, flags, valid_mask, packet_name) \
  do { \
    if (((flags) & ~(valid_mask)) != 0) { \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...); \
      disconnect_client_for_bad_data((client), "%s unknown flags set (flags=0x%x, valid=0x%x)", (packet_name), \
                                     (unsigned)(flags), (unsigned)(valid_mask)); \
      return; \
    } \
  } while (0)

/** @} */

/* ============================================================================
 * Image Dimension Validation (Function Implementation)
 * ============================================================================ */

#include <stdint.h>
#include <stddef.h>

/**
 * @defgroup image_validation Image Validation Functions
 * @ingroup validation
 * @{
 */

/**
 * @brief Validate and compute RGB image buffer size safely
 * @param width Image width in pixels (must be > 0)
 * @param height Image height in pixels (must be > 0)
 * @param out_rgb_size Pointer to store computed RGB buffer size (must not be NULL)
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM on overflow or invalid dimensions
 *
 * Safely validates image dimensions and computes the total RGB buffer size needed.
 * Prevents integer overflow attacks by checking multiplication at each step.
 *
 * VALIDATION STEPS:
 * 1. Check width > 0 and height > 0
 * 2. Check pixel_count = width * height won't overflow (max 4K = 3840x2160)
 * 3. Check rgb_size = pixel_count * sizeof(rgb_t) won't overflow
 *
 * @note Maximum supported resolution is 4K (3840x2160) = 8,294,400 pixels
 * @note This function prevents DoS attacks via integer overflow in dimension checks
 * @note Returns error code via asciichat_errno, not just return value
 *
 * @ingroup image_validation
 */
asciichat_error_t image_validate_dimensions(uint32_t width, uint32_t height, size_t *out_rgb_size);

/** @} */
