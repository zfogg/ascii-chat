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

#include "../common.h" // For error handling

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
#define VALIDATE_NOTNULL_DATA(client, data, packet_name)                                                               \
  do {                                                                                                                 \
    if (!(data)) {                                                                                                     \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...);                     \
      disconnect_client_for_bad_data((client), "%s payload missing", (packet_name));                                   \
      return;                                                                                                          \
    }                                                                                                                  \
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
#define VALIDATE_MIN_SIZE(client, len, min_size, packet_name)                                                          \
  do {                                                                                                                 \
    if ((len) < (min_size)) {                                                                                          \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...);                     \
      disconnect_client_for_bad_data((client), "%s payload too small (len=%zu, min=%zu)", (packet_name), (len),        \
                                     (min_size));                                                                      \
      return;                                                                                                          \
    }                                                                                                                  \
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
#define VALIDATE_EXACT_SIZE(client, len, expected_size, packet_name)                                                   \
  do {                                                                                                                 \
    if ((len) != (expected_size)) {                                                                                    \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...);                     \
      disconnect_client_for_bad_data((client), "%s payload size mismatch (len=%zu, expected=%zu)", (packet_name),      \
                                     (len), (expected_size));                                                          \
      return;                                                                                                          \
    }                                                                                                                  \
  } while (0)

/**
 * Validate that audio stream is enabled for this client.
 * Disconnects client and returns if audio stream is not enabled.
 *
 * @param client Client info pointer
 * @param packet_name Name of packet type for error message
 */
#define VALIDATE_AUDIO_STREAM_ENABLED(client, packet_name)                                                             \
  do {                                                                                                                 \
    if (!atomic_load(&(client)->is_sending_audio)) {                                                                   \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...);                     \
      disconnect_client_for_bad_data((client), "%s received before audio stream enabled", (packet_name));              \
      return;                                                                                                          \
    }                                                                                                                  \
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
#define VALIDATE_AUDIO_SAMPLE_COUNT(client, num_samples, max_samples, packet_name)                                     \
  do {                                                                                                                 \
    if ((num_samples) <= 0 || (num_samples) > (max_samples)) {                                                         \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...);                     \
      disconnect_client_for_bad_data((client), "%s invalid sample count: %d (max %d)", (packet_name), (num_samples),   \
                                     (max_samples));                                                                   \
      return;                                                                                                          \
    }                                                                                                                  \
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
#define VALIDATE_AUDIO_ALIGNMENT(client, len, sample_size, packet_name)                                                \
  do {                                                                                                                 \
    if ((len) % (sample_size) != 0) {                                                                                  \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...);                     \
      disconnect_client_for_bad_data((client), "%s payload not aligned (len=%zu, sample_size=%zu)", (packet_name),     \
                                     (len), (sample_size));                                                            \
      return;                                                                                                          \
    }                                                                                                                  \
  } while (0)

/**
 * Validate that a required resource/buffer is initialized.
 * Disconnects client and returns if resource is NULL.
 *
 * @param client Client info pointer
 * @param resource Pointer to resource to validate
 * @param resource_name Name of resource for error message
 */
#define VALIDATE_RESOURCE_INITIALIZED(client, resource, resource_name)                                                 \
  do {                                                                                                                 \
    if (!(resource)) {                                                                                                 \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...);                     \
      disconnect_client_for_bad_data((client), "%s not initialized", (resource_name));                                 \
      return;                                                                                                          \
    }                                                                                                                  \
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
#define VALIDATE_PACKET_SIZE(client, data, len, expected_size, packet_name)                                            \
  do {                                                                                                                 \
    if (!(data)) {                                                                                                     \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...);                     \
      disconnect_client_for_bad_data((client), packet_name " payload missing");                                        \
      return;                                                                                                          \
    }                                                                                                                  \
    if ((len) != (expected_size)) {                                                                                    \
      extern void disconnect_client_for_bad_data(client_info_t * client, const char *format, ...);                     \
      disconnect_client_for_bad_data((client), packet_name " payload size %zu (expected %zu)", (len),                  \
                                     (expected_size));                                                                 \
      return;                                                                                                          \
    }                                                                                                                  \
  } while (0)

/**
 * Validate packet payload pointer is not NULL.
 * Used as a conditional to check packet data validity before further processing.
 * Calls the disconnect handler and returns true if validation fails.
 *
 * @param client Client info pointer (required for error handler)
 * @param data Payload pointer to validate
 * @param packet_name Human-readable packet name for error messages
 * @param disconnect_handler Error handler function to call on failure
 * @return Non-zero (true) if validation failed (data is NULL), 0 (false) if valid (data is not NULL)
 *
 * Usage:
 * @code
 * void handle_audio(client_info_t *client, const void *data, size_t len) {
 *   if (VALIDATE_PACKET_NOT_NULL(client, data, "AUDIO_OPUS", disconnect_client_for_bad_data)) {
 *     return;  // Handler already called with error message
 *   }
 *   // ... process packet ...
 * }
 * @endcode
 */
#define VALIDATE_PACKET_NOT_NULL(client, data, packet_name, disconnect_handler)                                        \
  ({                                                                                                                   \
    int _validation_failed = 0;                                                                                        \
    if (!(data)) {                                                                                                     \
      extern void disconnect_handler(client_info_t * client, const char *format, ...);                                 \
      disconnect_handler((client), packet_name " payload missing");                                                    \
      _validation_failed = 1;                                                                                          \
    }                                                                                                                  \
    _validation_failed;                                                                                                \
  })

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
