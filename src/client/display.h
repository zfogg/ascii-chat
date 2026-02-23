/**
 * @file client/display.h
 * @ingroup client_display
 * @brief ascii-chat Client Display Management Interface
 *
 * Defines the display subsystem interface for terminal rendering,
 * TTY management, frame output operations, and terminal capability detection.
 *
 * The display subsystem handles all interactions with the terminal:
 * - Terminal capability detection (color depth, Unicode, half-blocks)
 * - TTY control (raw mode, terminal reset)
 * - ASCII frame rendering to stdout
 * - Terminal logging suppression during rendering
 * - Clean terminal restoration on shutdown
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 *
 * @see topic_client_display "Terminal Display Architecture"
 */

#pragma once

#include <stdbool.h>
#include <ascii-chat/platform/terminal.h>

/* Forward declarations */
typedef struct session_display_ctx session_display_ctx_t;

/**
 * @brief Set the display context (framework integration)
 *
 * Called by client_run() to pass the display context created by
 * session_client_like_run() to the display module so protocol threads
 * can render frames.
 *
 * @param display_ctx Display context from framework (may be NULL)
 * @ingroup client_display
 */
void display_set_context(session_display_ctx_t *display_ctx);

/**
 * @brief Initialize display subsystem
 *
 * Sets up terminal for ASCII rendering and detects terminal capabilities.
 *
 * Initialization steps:
 * 1. Checks if stdout is a TTY (error if not, unless snapshot mode or test pattern)
 * 2. Detects terminal color depth (24-bit true color, 256-color, 16-color, or ANSI)
 * 3. Detects Unicode/UTF-8 support
 * 4. Detects half-block character support (for 2x vertical resolution)
 * 5. Sets raw terminal mode (disables echo, canonical mode)
 * 6. Saves original terminal settings for restoration
 *
 * **Options affecting initialization**:
 * - `--color`: Force enable/disable color
 * - `--palette`: Select ASCII rendering palette
 * - `--width`, `--height`: Terminal dimensions (auto-detected if not set)
 * - `--half-block`: Enable 2x vertical resolution using half-block chars
 *
 * @return 0 on success, negative on error:
 *         - ERROR_TERMINAL: TTY error or terminal control failed
 *         - ERROR_DISPLAY: Terminal dimensions invalid
 *
 * @note Called during client initialization before connection attempts.
 *       Must be paired with display_cleanup() for proper terminal restoration.
 *
 * @ingroup client_display
 *
 * @see display_cleanup "Cleanup display and restore terminal"
 */
int display_init();

/**
 * @brief Check if display has TTY capability
 *
 * Checks if stdout is connected to a terminal (TTY) or redirected to a file/pipe.
 * The client requires TTY for interactive rendering unless in snapshot or test mode.
 *
 * @return true if TTY available (interactive terminal), false if piped/redirected
 *
 * @note Test pattern and snapshot modes can proceed without TTY, but real webcam
 *       streaming requires a terminal for rendering.
 *
 * @ingroup client_display
 */
bool display_has_tty();

/**
 * @brief Perform full display reset
 *
 * Clears the terminal and resets display state. Called by server clear commands
 * or on SIGWINCH terminal resize events.
 *
 * The reset:
 * 1. Clears the terminal screen
 * 2. Resets cursor to home position
 * 3. Resets text attributes (color, bold, etc.)
 * 4. Resets first-frame tracking (re-suppresses logging on next frame)
 *
 * @ingroup client_display
 *
 * @see display_reset_for_new_connection "Reset for new connection"
 */
void display_full_reset();

/**
 * @brief Reset display state for new connection
 *
 * Call this when starting a new connection after reconnection to reset
 * first-frame tracking and clear any residual state from the previous connection.
 *
 * The reset:
 * 1. Clears the terminal screen (like display_full_reset)
 * 2. Resets first-frame flag to suppress logging on first frame of new connection
 * 3. Prepares for fresh frame rendering
 *
 * @ingroup client_display
 *
 * @see display_reset_for_new_connection "Reset for new connection"
 */
void display_reset_for_new_connection();

/**
 * @brief Disable terminal logging for first frame
 *
 * Temporarily disables terminal logging output to prevent debug/info messages
 * from corrupting the first rendered ASCII frame.
 *
 * Call sequence:
 * 1. display_disable_logging_for_first_frame() - before rendering frame
 * 2. display_render_frame() - renders frame with logging disabled
 * 3. Logging re-enabled automatically after first frame
 *
 * **Why**: The first frame render clears the terminal and draws the ASCII grid.
 * If logging is active, log messages would overwrite or corrupt the grid.
 *
 * @ingroup client_display
 *
 * @see display_render_frame "Render ASCII frame"
 */
void display_disable_logging_for_first_frame();

/**
 * @brief Render ASCII frame to display
 *
 * Outputs the ASCII frame data to the terminal. This is called by the protocol
 * thread when receiving PACKET_TYPE_ASCII_FRAME from the server.
 *
 * The function:
 * 1. Verifies TTY is still available
 * 2. If first frame: disables logging, clears terminal
 * 3. Outputs frame data to stdout
 * 4. Flushes output to ensure visible immediately
 * 5. Re-enables logging after first frame
 *
 * In snapshot mode, exits immediately after rendering.
 *
 * @param frame_data ASCII frame data to render (null-terminated string or binary data)
 *
 * @note Called by protocol thread - must be thread-safe with respect to
 *       terminal state (stdout writes are atomic for single call).
 *
 * @ingroup client_display
 *
 * @see display_init "Initialize display subsystem"
 */
void display_render_frame(const char *frame_data);

/**
 * @brief Cleanup display subsystem
 *
 * Restores terminal to original state and releases display resources.
 * Must be called before process exit to ensure terminal is left in a usable state.
 *
 * The cleanup process:
 * 1. Re-enables terminal echo (if it was disabled)
 * 2. Re-enables canonical mode (line buffering)
 * 3. Moves cursor to clean location (below last frame)
 * 4. Restores original terminal color (if color was used)
 * 5. Restores original terminal settings
 * 6. Releases terminal state structures
 *
 * **Critical**: Must be called even if display_init() failed - partial init may need cleanup.
 *
 * @ingroup client_display
 *
 * @see display_init "Initialize display subsystem"
 */
void display_cleanup();

extern tty_info_t g_tty_info;
