/**
 * @file session/help_screen.h
 * @brief 🆘 Interactive help screen for session keyboard shortcuts
 * @ingroup session
 * @addtogroup session
 * @{
 *
 * Provides an interactive help screen overlay that displays:
 * - Available keyboard shortcuts
 * - Current program state (volume, color mode, webcam flip, audio, mute status)
 *
 * The help screen is toggled with '?' key and suppresses frame rendering
 * while network reception continues in the background.
 *
 * **Display State:**
 * - Horizontally and vertically centered in terminal
 * - Box-drawing border with UTF-8 fallback to ASCII
 * - Real-time updates of option values (no network latency)
 *
 * **Threading:**
 * - Help state: Atomic bool for lock-free access
 * - Terminal writes: Serialized via session_display_write_raw()
 * - Option reads: Lock-free via RCU (GET_OPTION macro)
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#pragma once

#include "display.h"
#include <stdbool.h>

/* ============================================================================
 * Help Screen Control Functions
 * @{
 */

/**
 * @brief Toggle help screen on/off
 * @param ctx Display context (must not be NULL)
 *
 * Atomically toggles the help screen active state. When active, the help
 * screen is displayed and frame rendering is suppressed. Network reception
 * continues in the background.
 *
 * @note Thread-safe: Uses atomic bool for lock-free toggle
 * @ingroup session
 */
void session_display_toggle_help(session_display_ctx_t *ctx);

/**
 * @brief Check if help screen is currently active
 * @param ctx Display context (must not be NULL)
 * @return true if help screen is active, false otherwise
 *
 * Non-blocking check of help screen state.
 *
 * @note Thread-safe: Uses atomic load
 * @ingroup session
 */
bool session_display_is_help_active(session_display_ctx_t *ctx);

/**
 * @brief Render help screen TUI overlay
 * @param ctx Display context (must not be NULL)
 *
 * Renders a centered help screen showing:
 * - Keyboard shortcuts and their functions
 * - Current program state values:
 *   - Volume (displayed as bar graph: ████████░░ 80%)
 *   - Color mode (Mono, 16-color, 256-color, Truecolor)
 *   - Webcam flip state (Normal or Flipped)
 *   - Audio status (Enabled or Disabled)
 *   - Mute status (in volume display)
 *
 * **Layout:**
 * - Centered horizontally: col = (term_width - box_width) / 2
 * - Centered vertically: row = (term_height - box_height) / 2
 * - Border: Box drawing chars (╔═╗║╚╝) with ASCII fallback (+|-) for UTF-8-less terminals
 *
 * **Edge Cases:**
 * - Terminal too small: Shows simplified help or warning
 * - Non-TTY: Skips rendering (no terminal for display)
 *
 * @note Reads live option values via GET_OPTION() - values reflect current state
 * @note Uses session_display_write_raw() for atomic terminal output
 * @ingroup session
 */
void session_display_render_help(session_display_ctx_t *ctx);

/** @} */

/** @} */
