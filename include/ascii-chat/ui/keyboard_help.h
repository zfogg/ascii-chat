/**
 * @file ui/keyboard_help.h
 * @brief 🆘 Interactive keyboard help overlay for session keyboard shortcuts
 * @ingroup session
 * @addtogroup session
 * @{
 *
 * Provides an interactive keyboard help overlay that displays:
 * - Available keyboard shortcuts
 * - Current program state (volume, color mode, webcam flip, audio, mute status)
 *
 * The keyboard help is toggled with '?' key and suppresses frame rendering
 * while network reception continues in the background.
 *
 * **Display State:**
 * - Horizontally and vertically centered in terminal
 * - Box-drawing border with UTF-8 fallback to ASCII
 * - Real-time updates of option values (no network latency)
 *
 * **Threading:**
 * - Keyboard help state: Atomic bool for lock-free access
 * - Terminal writes: Serialized via session_display_write_raw()
 * - Option reads: Lock-free via RCU (GET_OPTION macro)
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#pragma once

#include "session/display.h"
#include <stdbool.h>

/* ============================================================================
 * Keyboard Help Control Functions
 * @{
 */

/**
 * @brief Toggle keyboard help on/off
 * @param ctx Display context (must not be NULL)
 *
 * Atomically toggles the keyboard help active state. When active, the keyboard
 * help is displayed and frame rendering is suppressed. Network reception
 * continues in the background.
 *
 * @note Thread-safe: Uses atomic bool for lock-free toggle
 * @ingroup session
 */
void keyboard_help_toggle(session_display_ctx_t *ctx);

/**
 * @brief Check if keyboard help is currently active
 * @param ctx Display context (must not be NULL)
 * @return true if keyboard help is active, false otherwise
 *
 * Non-blocking check of keyboard help state.
 *
 * @note Thread-safe: Uses atomic load
 * @ingroup session
 */
bool keyboard_help_is_active(session_display_ctx_t *ctx);

/**
 * @brief Render keyboard help TUI overlay
 * @param ctx Display context (must not be NULL)
 *
 * Renders a centered keyboard help screen showing:
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
void keyboard_help_render(session_display_ctx_t *ctx);

/**
 * @brief Set the global display context (for signal handlers like Ctrl+C)
 * @param ctx Display context (NULL to clear)
 *
 * Called by the render loop to register the current display context
 * so that signal handlers can access it. This enables Ctrl+C to close
 * the help screen instead of quitting when help is active.
 *
 * @note Must be called from the render loop startup/shutdown
 * @ingroup session
 */
void session_display_set_global_context(session_display_ctx_t *ctx);

/**
 * @brief Check if keyboard help is active (global accessor for signal handlers)
 * @return true if keyboard help is active, false otherwise
 *
 * Signal-handler-safe version of keyboard_help_is_active() that doesn't
 * require passing a context. Used by Ctrl+C handler to check if help is
 * active before deciding to exit.
 *
 * @note Safe to call from signal handlers
 * @ingroup session
 */
bool keyboard_help_is_active_global(void);

/**
 * @brief Toggle keyboard help (global accessor for signal handlers)
 *
 * Signal-handler-safe version of keyboard_help_toggle() that doesn't
 * require passing a context. Used by Ctrl+C handler to close the help
 * screen instead of exiting the app.
 *
 * @note Safe to call from signal handlers
 * @ingroup session
 */
void keyboard_help_toggle_global(void);

/**
 * @brief Request help screen closure from signal handler (Ctrl+C)
 *
 * Signal-handler-safe way to request that the help screen be closed.
 * Sets an atomic flag that the render loop checks. The render loop then
 * toggles off the help screen and continues running.
 *
 * @note Safe to call from signal handlers
 * @ingroup session
 */
void keyboard_help_signal_cancel(void);

/**
 * @brief Check if help screen closure was requested by signal handler
 * @return true if signal requested help closure (and clears the flag), false otherwise
 *
 * Called by render loop to detect and handle Ctrl+C while help is active.
 * Uses atomic compare-and-swap to clear flag while returning the previous value.
 *
 * @note Thread-safe using atomic operations
 * @ingroup session
 */
bool keyboard_help_check_signal_cancel(void);

/** @} */

/** @} */
