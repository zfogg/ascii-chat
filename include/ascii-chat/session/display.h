/**
 * @file session/display.h
 * @brief 🖥️ Unified terminal display abstraction for session-based rendering
 * @ingroup session
 * @addtogroup session
 * @{
 *
 * This header provides a unified interface for terminal display that abstracts
 * TTY detection, palette initialization, and frame rendering. It is designed
 * to be reusable across different modes (client, mirror, and discovery mode).
 *
 * CORE FEATURES:
 * ==============
 * - TTY detection and management
 * - Terminal capability detection (color, UTF-8)
 * - Palette initialization and luminance mapping
 * - Frame rendering with RLE expansion support
 * - Snapshot mode support for single-frame capture
 *
 * USAGE:
 * ======
 * @code{.c}
 * // Create display context with color support
 * session_display_config_t config = {
 *     .snapshot_mode = false,
 *     .palette_type = PALETTE_STANDARD,
 *     .custom_palette = NULL,
 *     .color_mode = TERM_COLOR_AUTO
 * };
 * session_display_ctx_t *ctx = session_display_create(&config);
 *
 * // Render frames
 * const char *frame_data = ...; // ASCII frame from server
 * session_display_render_frame(ctx, frame_data, false);
 *
 * // Cleanup
 * session_display_destroy(ctx);
 * @endcode
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/video/palette.h>
#include <ascii-chat/video/image.h>

/* ============================================================================
 * Session Display Configuration
 * ============================================================================ */

/**
 * @brief Callback type to check if initialization should be cancelled
 *
 * Called periodically during initialization to allow graceful cancellation.
 * Should return true if initialization should stop immediately.
 *
 * @param user_data Opaque pointer provided by caller
 * @return true to cancel initialization, false to continue
 */
typedef bool (*session_display_should_exit_fn)(void *user_data);

/**
 * @brief Configuration for session display context
 *
 * Specifies display parameters including snapshot mode, palette, and color mode.
 *
 * @ingroup session
 */
typedef struct {
  /** @brief Enable snapshot mode (single frame capture) */
  bool snapshot_mode;

  /** @brief Palette type for ASCII rendering */
  palette_type_t palette_type;

  /** @brief Custom palette characters (required if palette_type == PALETTE_CUSTOM) */
  const char *custom_palette;

  /** @brief Color mode override (TERM_COLOR_AUTO for auto-detection) */
  terminal_color_mode_t color_mode;

  /** @brief Enable audio playback (mirror mode) */
  bool enable_audio_playback;

  /** @brief Audio context for playback (borrowed, not owned) */
  void *audio_ctx;

  /** @brief Optional: callback to check if initialization should be cancelled (e.g., shutdown signal) */
  session_display_should_exit_fn should_exit_callback;

  /** @brief Opaque data passed to should_exit_callback */
  void *callback_data;
} session_display_config_t;

/* ============================================================================
 * Session Display Context
 * ============================================================================ */

/**
 * @brief Opaque session display context handle
 *
 * Manages TTY state, terminal capabilities, palette, and rendering state.
 * Created via session_display_create(), destroyed via session_display_destroy().
 *
 * @ingroup session
 */
typedef struct session_display_ctx session_display_ctx_t;

/* ============================================================================
 * Session Display Lifecycle Functions
 * @{
 */

/**
 * @brief Create a new session display context
 * @param config Display configuration (must not be NULL)
 * @return Pointer to display context, or NULL on failure
 *
 * Creates and initializes a session display context with the specified
 * configuration. Detects terminal capabilities and initializes palette.
 *
 * @note Call session_display_destroy() to free resources when done.
 * @note On failure, sets asciichat_errno with error details.
 *
 * @ingroup session
 */
session_display_ctx_t *session_display_create(const session_display_config_t *config);

/**
 * @brief Destroy session display context and free resources
 * @param ctx Display context to destroy (can be NULL)
 *
 * Cleans up the display context, restores terminal state, and releases
 * all resources. Safe to call with NULL.
 *
 * @ingroup session
 */
void session_display_destroy(session_display_ctx_t *ctx);

/** @} */

/* ============================================================================
 * Session Display Query Functions
 * @{
 */

/**
 * @brief Check if display has a TTY (terminal) available
 * @param ctx Display context (must not be NULL)
 * @return true if TTY is available, false otherwise
 *
 * Returns whether the display has detected and opened a TTY.
 * When no TTY is available, output goes to stdout.
 *
 * @ingroup session
 */
bool session_display_has_tty(session_display_ctx_t *ctx);

/**
 * @brief Get detected terminal capabilities
 * @param ctx Display context (must not be NULL)
 * @return Pointer to terminal capabilities structure
 *
 * Returns the detected terminal capabilities including color level,
 * UTF-8 support, and render mode preferences.
 *
 * @note Returns NULL if context is NULL or not initialized.
 *
 * @ingroup session
 */
const terminal_capabilities_t *session_display_get_caps(session_display_ctx_t *ctx);

/**
 * @brief Get the palette characters string
 * @param ctx Display context (must not be NULL)
 * @return Pointer to palette character string
 *
 * Returns the initialized palette character string used for
 * luminance-to-character mapping.
 *
 * @ingroup session
 */
const char *session_display_get_palette_chars(session_display_ctx_t *ctx);

/**
 * @brief Get the palette character count
 * @param ctx Display context (must not be NULL)
 * @return Number of characters in the palette
 *
 * @ingroup session
 */
size_t session_display_get_palette_len(session_display_ctx_t *ctx);

/**
 * @brief Get the luminance mapping palette
 * @param ctx Display context (must not be NULL)
 * @return Pointer to 256-entry luminance mapping array
 *
 * Returns the luminance mapping palette for direct brightness-to-character
 * lookup during rendering.
 *
 * @ingroup session
 */
const char *session_display_get_luminance_palette(session_display_ctx_t *ctx);

/**
 * @brief Get the TTY file descriptor
 * @param ctx Display context (must not be NULL)
 * @return TTY file descriptor, or -1 if no TTY available
 *
 * @ingroup session
 */
int session_display_get_tty_fd(session_display_ctx_t *ctx);

/** @} */

/* ============================================================================
 * Session Display ASCII Conversion Functions
 * @{
 */

/**
 * @brief Convert an image to ASCII art using display context and command-line options
 * @param ctx Display context (must not be NULL)
 * @param image Image to convert (must not be NULL)
 * @return Dynamically allocated ASCII string, or NULL on error
 *
 * Converts the given image to ASCII art using:
 * - Palette and terminal capabilities from display context
 * - Width, height, stretch, and aspect ratio settings from GET_OPTION()
 *
 * This completely encapsulates ASCII conversion complexity so callers
 * don't need to manage palette, terminal capabilities, or conversion options.
 *
 * The returned string must be freed by caller with SAFE_FREE().
 *
 * @ingroup session
 */
char *session_display_convert_to_ascii(session_display_ctx_t *ctx, const image_t *image);

/** @} */

/* ============================================================================
 * Session Display Rendering Functions
 * @{
 */

/**
 * @brief Render an ASCII frame to the terminal
 * @param ctx Display context (must not be NULL)
 * @param frame_data ASCII frame data to render (must not be NULL)
 * @param is_final true if this is the final frame (for snapshot mode)
 *
 * Renders the ASCII frame to the terminal. Handles cursor positioning,
 * RLE expansion if needed, and snapshot mode behavior.
 *
 * In snapshot mode, renders all frames during the snapshot window.
 *
 * @ingroup session
 */
void session_display_render_frame(session_display_ctx_t *ctx, const char *frame_data);

/**
 * @brief Render raw bytes to the terminal without frame processing
 * @param ctx Display context (must not be NULL)
 * @param data Raw byte data to write (must not be NULL)
 * @param len Length of data in bytes
 *
 * Directly writes raw bytes to the terminal without any frame processing.
 * Useful for RLE-expanded frames or pre-formatted output.
 *
 * @ingroup session
 */
void session_display_write_raw(session_display_ctx_t *ctx, const char *data, size_t len);

/**
 * @brief Reset terminal to default state
 * @param ctx Display context (must not be NULL)
 *
 * Resets terminal attributes (colors, cursor visibility, etc.) to defaults.
 * Useful for cleanup or error recovery.
 *
 * @ingroup session
 */
void session_display_reset(session_display_ctx_t *ctx);

/**
 * @brief Clear the terminal screen
 * @param ctx Display context (must not be NULL)
 *
 * Clears the terminal screen and moves cursor to home position.
 *
 * @ingroup session
 */
void session_display_clear(session_display_ctx_t *ctx);

/**
 * @brief Move cursor to home position (top-left)
 * @param ctx Display context (must not be NULL)
 *
 * Moves the cursor to the top-left corner (1,1) of the terminal.
 *
 * @ingroup session
 */
void session_display_cursor_home(session_display_ctx_t *ctx);

/**
 * @brief Show or hide the cursor
 * @param ctx Display context (must not be NULL)
 * @param visible true to show cursor, false to hide
 *
 * @ingroup session
 */
void session_display_set_cursor_visible(session_display_ctx_t *ctx, bool visible);

/** @} */

/* ============================================================================
 * Session Display Audio Functions
 * @{
 */

/**
 * @brief Check if display has audio playback configured
 * @param ctx Display context (must not be NULL)
 * @return true if audio playback is available, false otherwise
 *
 * @ingroup session
 */
bool session_display_has_audio_playback(session_display_ctx_t *ctx);

/**
 * @brief Write audio samples to playback buffer
 * @param ctx Display context (must not be NULL)
 * @param buffer Audio samples to write (must not be NULL)
 * @param num_samples Number of samples to write
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Writes audio samples to the playback ring buffer for playback through speakers.
 * Used in mirror mode to play file audio or other audio sources.
 *
 * @ingroup session
 */
asciichat_error_t session_display_write_audio(session_display_ctx_t *ctx, const float *buffer, size_t num_samples);

/** @} */

/** @} */
