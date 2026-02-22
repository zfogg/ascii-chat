/**
 * @file session/settings.h
 * @brief ⚙️ Session settings serialization and synchronization
 * @ingroup session
 * @addtogroup session
 * @{
 *
 * This header provides settings structures and serialization functions for
 * session configuration that can be transmitted between peers in discovery mode.
 *
 * CORE FEATURES:
 * ==============
 * - Compact binary serialization for network transmission
 * - Version-based conflict detection
 * - Bidirectional conversion with options system
 * - Future-proof extensible structure with reserved bytes
 *
 * USAGE:
 * ======
 * @code{.c}
 * // Create settings from current options
 * session_settings_t settings;
 * session_settings_from_options(&settings);
 *
 * // Serialize for transmission
 * uint8_t buffer[SESSION_SETTINGS_SERIALIZED_SIZE];
 * size_t len;
 * session_settings_serialize(&settings, buffer, &len);
 *
 * // Deserialize received settings
 * session_settings_t received;
 * session_settings_deserialize(buffer, len, &received);
 *
 * // Check if update needed
 * if (session_settings_needs_update(local_version, received.version)) {
 *     session_settings_apply_to_options(&received);
 * }
 * @endcode
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <ascii-chat/asciichat_errno.h>

/* ============================================================================
 * Session Settings Constants
 * ============================================================================ */

/** @brief Current session settings structure version */
#define SESSION_SETTINGS_VERSION 1

/** @brief Size of serialized session settings in bytes */
#define SESSION_SETTINGS_SERIALIZED_SIZE 64

/* ============================================================================
 * Session Settings Structure
 * ============================================================================ */

/**
 * @brief Session settings for transmission between peers
 *
 * Contains display and rendering configuration that can be synchronized
 * between session participants. Uses fixed-size fields for deterministic
 * serialization.
 *
 * @note Struct is designed for binary serialization with network byte order.
 * @note Reserved bytes allow future extension without breaking compatibility.
 *
 * @ingroup session
 */
typedef struct {
  /** @brief Settings version for conflict detection (monotonically increasing) */
  uint32_t version;

  /** @brief Terminal width in characters (0 = auto-detect) */
  int16_t width;

  /** @brief Terminal height in characters (0 = auto-detect) */
  int16_t height;

  /** @brief Color mode (terminal_color_mode_t value) */
  uint8_t color_mode;

  /** @brief Render mode (render_mode_t value) */
  uint8_t render_mode;

  /** @brief Palette type (palette_type_t value) */
  uint8_t palette_type;

  /** @brief Custom palette characters (if palette_type == PALETTE_CUSTOM) */
  char palette_custom[32];

  /** @brief Audio enabled flag */
  uint8_t audio_enabled;

  /** @brief Encryption required flag */
  uint8_t encryption_required;

  /** @brief Reserved bytes for future expansion */
  uint8_t reserved[16];
} session_settings_t;

/* ============================================================================
 * Session Settings Functions
 * @{
 */

/**
 * @brief Initialize session settings to defaults
 * @param settings Settings structure to initialize (must not be NULL)
 *
 * Initializes settings structure with sensible defaults:
 * - Width/height: 0 (auto-detect)
 * - Color mode: TERM_COLOR_AUTO
 * - Render mode: RENDER_MODE_FOREGROUND
 * - Palette: PALETTE_STANDARD
 * - Audio: disabled
 * - Encryption: required
 *
 * @ingroup session
 */
void session_settings_init(session_settings_t *settings);

/**
 * @brief Serialize session settings to binary buffer
 * @param settings Settings to serialize (must not be NULL)
 * @param buffer Output buffer (must be at least SESSION_SETTINGS_SERIALIZED_SIZE bytes)
 * @param len Output parameter for actual serialized length (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Serializes settings to a compact binary format suitable for network
 * transmission. Uses network byte order for multi-byte integers.
 *
 * @ingroup session
 */
asciichat_error_t session_settings_serialize(const session_settings_t *settings, uint8_t *buffer, size_t *len);

/**
 * @brief Deserialize session settings from binary buffer
 * @param buffer Input buffer containing serialized settings
 * @param len Length of input buffer in bytes
 * @param settings Output settings structure (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Deserializes settings from binary format. Validates buffer length
 * and extracts fields in network byte order.
 *
 * @note Returns ERROR_INVALID_PARAM if buffer is too small.
 *
 * @ingroup session
 */
asciichat_error_t session_settings_deserialize(const uint8_t *buffer, size_t len, session_settings_t *settings);

/**
 * @brief Populate settings from current global options
 * @param settings Output settings structure (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Reads current values from the global options system and populates
 * the settings structure. Version is set to current timestamp.
 *
 * @ingroup session
 */
asciichat_error_t session_settings_from_options(session_settings_t *settings);

/**
 * @brief Apply settings to global options
 * @param settings Settings to apply (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Updates the global options system with values from the settings
 * structure. Only modifies options that are represented in settings.
 *
 * @ingroup session
 */
asciichat_error_t session_settings_apply_to_options(const session_settings_t *settings);

/**
 * @brief Check if settings need update based on versions
 * @param local_version Current local settings version
 * @param remote_version Received remote settings version
 * @return true if local should update to remote, false otherwise
 *
 * Determines if local settings should be updated based on version comparison.
 * Higher version number wins (newer settings).
 *
 * @ingroup session
 */
bool session_settings_needs_update(uint32_t local_version, uint32_t remote_version);

/**
 * @brief Compare two settings structures for equality
 * @param a First settings structure (must not be NULL)
 * @param b Second settings structure (must not be NULL)
 * @return true if settings are equal (ignoring version), false otherwise
 *
 * @ingroup session
 */
bool session_settings_equal(const session_settings_t *a, const session_settings_t *b);

/** @} */

/** @} */
