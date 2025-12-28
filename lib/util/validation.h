/**
 * @defgroup validation Validation Helpers
 * @ingroup module_core
 * @brief Reusable validation macros for protocol handlers
 *
 * @file validation.h
 * @brief Common validation macros to reduce duplication in protocol handlers
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

#include "../common.h"
#include <stdint.h>
#include <stddef.h>

/* Forward declarations to avoid circular dependencies */
typedef struct client_info client_info_t;

/* Function declaration - defined in src/server/protocol.c */
void disconnect_client_for_bad_data(client_info_t *client, const char *format, ...);

/**
 * Validate that payload data pointer is not NULL.
 * Disconnects client and returns if data is NULL.
 */
#define VALIDATE_NOTNULL_DATA(client, data, packet_name)                                                               \
  do {                                                                                                                 \
    if (!(data)) {                                                                                                     \
      disconnect_client_for_bad_data((client), "%s payload missing", (packet_name));                                   \
      return;                                                                                                          \
    }                                                                                                                  \
  } while (0)

/**
 * Validate that payload size is at least the minimum required.
 * Disconnects client and returns if len < min_size.
 */
#define VALIDATE_MIN_SIZE(client, len, min_size, packet_name)                                                          \
  do {                                                                                                                 \
    if ((len) < (min_size)) {                                                                                          \
      disconnect_client_for_bad_data((client), "%s payload too small (len=%zu, min=%zu)", (packet_name), (len),        \
                                     (min_size));                                                                      \
      return;                                                                                                          \
    }                                                                                                                  \
  } while (0)

/**
 * Validate that payload size is exactly the expected size.
 * Disconnects client and returns if len != expected_size.
 */
#define VALIDATE_EXACT_SIZE(client, len, expected_size, packet_name)                                                   \
  do {                                                                                                                 \
    if ((len) != (expected_size)) {                                                                                    \
      disconnect_client_for_bad_data((client), "%s payload size mismatch (len=%zu, expected=%zu)", (packet_name),      \
                                     (len), (expected_size));                                                          \
      return;                                                                                                          \
    }                                                                                                                  \
  } while (0)

/**
 * Validate that audio stream is enabled for this client.
 * Disconnects client and returns if audio stream is not enabled.
 */
#define VALIDATE_AUDIO_STREAM_ENABLED(client, packet_name)                                                             \
  do {                                                                                                                 \
    if (!atomic_load(&(client)->is_sending_audio)) {                                                                   \
      disconnect_client_for_bad_data((client), "%s received before audio stream enabled", (packet_name));              \
      return;                                                                                                          \
    }                                                                                                                  \
  } while (0)

/**
 * Validate that audio sample count is within acceptable bounds.
 * Disconnects client and returns if sample count is invalid.
 */
#define VALIDATE_AUDIO_SAMPLE_COUNT(client, num_samples, max_samples, packet_name)                                     \
  do {                                                                                                                 \
    if ((num_samples) <= 0 || (num_samples) > (max_samples)) {                                                         \
      disconnect_client_for_bad_data((client), "%s invalid sample count: %d (max %d)", (packet_name), (num_samples),   \
                                     (max_samples));                                                                   \
      return;                                                                                                          \
    }                                                                                                                  \
  } while (0)

/**
 * Validate that audio sample alignment is correct (must be multiple of sample size).
 * Disconnects client and returns if alignment is incorrect.
 */
#define VALIDATE_AUDIO_ALIGNMENT(client, len, sample_size, packet_name)                                                \
  do {                                                                                                                 \
    if ((len) % (sample_size) != 0) {                                                                                  \
      disconnect_client_for_bad_data((client), "%s payload not aligned (len=%zu, sample_size=%zu)", (packet_name),     \
                                     (len), (sample_size));                                                            \
      return;                                                                                                          \
    }                                                                                                                  \
  } while (0)

/**
 * Validate that a required resource/buffer is initialized.
 * Disconnects client and returns if resource is NULL.
 */
#define VALIDATE_RESOURCE_INITIALIZED(client, resource, resource_name)                                                 \
  do {                                                                                                                 \
    if (!(resource)) {                                                                                                 \
      disconnect_client_for_bad_data((client), "%s not initialized", (resource_name));                                 \
      return;                                                                                                          \
    }                                                                                                                  \
  } while (0)

/**
 * Validate packet payload size and presence.
 * Checks if data pointer is non-NULL and payload matches expected size.
 */
#define VALIDATE_PACKET_SIZE(client, data, len, expected_size, packet_name)                                            \
  do {                                                                                                                 \
    if (!(data)) {                                                                                                     \
      disconnect_client_for_bad_data((client), packet_name " payload missing");                                        \
      return;                                                                                                          \
    }                                                                                                                  \
    if ((len) != (expected_size)) {                                                                                    \
      disconnect_client_for_bad_data((client), packet_name " payload size %zu (expected %zu)", (len),                  \
                                     (expected_size));                                                                 \
      return;                                                                                                          \
    }                                                                                                                  \
  } while (0)

/**
 * Validate that a numeric value is non-zero (for dimensions, counts, etc).
 * Disconnects client and returns if value is zero.
 */
#define VALIDATE_NONZERO(client, value, value_name, packet_name)                                                       \
  do {                                                                                                                 \
    if ((value) == 0) {                                                                                                \
      disconnect_client_for_bad_data((client), "%s %s cannot be zero", (packet_name), (value_name));                   \
      return;                                                                                                          \
    }                                                                                                                  \
  } while (0)

/**
 * Validate that a numeric value is within a specified range (inclusive).
 * Disconnects client and returns if value is outside range.
 */
#define VALIDATE_RANGE(client, value, min_val, max_val, value_name, packet_name)                                       \
  do {                                                                                                                 \
    if ((value) < (min_val) || (value) > (max_val)) {                                                                  \
      disconnect_client_for_bad_data((client), "%s %s out of range: %u (valid: %u-%u)", (packet_name), (value_name),   \
                                     (unsigned)(value), (unsigned)(min_val), (unsigned)(max_val));                     \
      return;                                                                                                          \
    }                                                                                                                  \
  } while (0)

/**
 * Validate that at least one capability flag is set.
 * Disconnects client and returns if all flags are zero or invalid.
 */
#define VALIDATE_CAPABILITY_FLAGS(client, flags, valid_mask, packet_name)                                              \
  do {                                                                                                                 \
    if (((flags) & (valid_mask)) == 0) {                                                                               \
      disconnect_client_for_bad_data((client), "%s no valid capability flags set (flags=0x%x, valid=0x%x)",            \
                                     (packet_name), (unsigned)(flags), (unsigned)(valid_mask));                        \
      return;                                                                                                          \
    }                                                                                                                  \
  } while (0)

/**
 * Validate that only known/valid flags are set.
 * Disconnects client and returns if unknown flags are present.
 */
#define VALIDATE_FLAGS_MASK(client, flags, valid_mask, packet_name)                                                    \
  do {                                                                                                                 \
    if (((flags) & ~(valid_mask)) != 0) {                                                                              \
      disconnect_client_for_bad_data((client), "%s unknown flags set (flags=0x%x, valid=0x%x)", (packet_name),         \
                                     (unsigned)(flags), (unsigned)(valid_mask));                                       \
      return;                                                                                                          \
    }                                                                                                                  \
  } while (0)

/**
 * Validate packet payload pointer is not NULL.
 * Returns true if validation failed (data is NULL), false if valid.
 */
#define VALIDATE_PACKET_NOT_NULL(client, data, packet_name)                                                            \
  ({                                                                                                                   \
    int _validation_failed = 0;                                                                                        \
    if (!(data)) {                                                                                                     \
      disconnect_client_for_bad_data((client), packet_name " payload missing");                                        \
      _validation_failed = 1;                                                                                          \
    }                                                                                                                  \
    _validation_failed;                                                                                                \
  })

/** @} */

/* ============================================================================
 * Image Dimension Validation (Function Implementation)
 * ============================================================================ */

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
 */
asciichat_error_t image_validate_dimensions(uint32_t width, uint32_t height, size_t *out_rgb_size);

/** @} */
