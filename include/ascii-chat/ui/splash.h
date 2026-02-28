/**
 * @file ui/splash.h
 * @brief Intro and status screen display for ascii-chat with animated rainbow effects
 * @ingroup session
 *
 * Provides splash screen functionality for displaying:
 * - Intro screen with animated rainbow ASCII art (client/mirror/discovery modes)
 * - Status screen with server/discovery-service information
 *
 * The splash screen displays while initialization happens in the background:
 * - For clients/mirror: displays during network connection & frame preparation
 * - For servers: displays during binding and initialization
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#pragma once

#include <stdbool.h>

// Forward declare to avoid circular includes
struct session_display_ctx;
typedef struct session_display_ctx session_display_ctx_t;

/**
 * @brief Start animated intro screen with rainbow ASCII art (non-blocking)
 *
 * Starts the splash screen animation that runs while initialization happens
 * in the background. The animation continues until splash_intro_done() is called.
 *
 * **Display Requirements:**
 * - stdin AND stdout must be TTY (platform_isatty())
 * - --no-intro-screen option must not be set
 * - Not in snapshot mode (--snapshot)
 * - Terminal must be large enough (min 50 cols x 20 rows)
 *
 * **Behavior:**
 * - Starts animated rainbow border with "ascii-chat" logo
 * - Runs continuously until splash_intro_done() is called
 * - Animation loops with rainbow color cycling
 * - When splash_intro_done() called, animation stops on next frame
 * - Next real frame render clears screen and shows ASCII art
 *
 * @param ctx Display context (can be NULL for minimal stdout-only mode)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note Non-blocking - returns immediately after starting animation
 * @note Call splash_intro_done() when first frame is ready to render
 * @note Uses lock-free option access via GET_OPTION() macro
 *
 * @ingroup session
 */
int splash_intro_start(session_display_ctx_t *ctx);

/**
 * @brief Signal that intro splash should end (first frame ready to render)
 *
 * Tells the splash screen animation to stop displaying. On the next animation
 * frame attempt, it will clear the screen and exit cleanly.
 *
 * @return ASCIICHAT_OK on success
 *
 * @note Safe to call multiple times
 * @note After calling this, next frame render should show real ASCII art
 * @note The animation thread checks this flag and exits gracefully
 *
 * @ingroup session
 */
int splash_intro_done(void);

/**
 * @brief Display status screen for server/discovery-service modes
 *
 * Shows server or discovery-service status information with:
 * - Server mode: bind addresses, ports, connection info, encryption status
 * - Discovery-service mode: server fingerprint, bind address/port, database path
 *
 * **Display Requirements:**
 * - stdin AND stdout must be TTY (platform_isatty())
 * - --no-status-screen option must not be set
 * - Terminal must be large enough (min 50 cols x 15 rows)
 *
 * **Behavior:**
 * - Displays formatted status box with colored borders
 * - Stays on screen (requires manual interrupt via Ctrl+C)
 * - Uses rainbow gradient borders matching intro screen style
 *
 * @param mode Server mode type (MODE_SERVER=0 or MODE_DISCOVERY_SERVICE=3)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note Uses lock-free option access via GET_OPTION() macro
 * @note Thread-safe for use from main thread only
 *
 * @ingroup session
 */
int splash_display_status(int mode);

/**
 * @brief Check if splash screen should display
 *
 * Determines if TTY checks pass and options allow splash display.
 * Respects both TTY detection and option flags.
 *
 * @param is_intro true for intro screen checks, false for status screen
 * @return true if splash should display, false if should skip
 *
 * @ingroup session
 */
bool splash_should_display(bool is_intro);

/**
 * @brief Restore stderr after splash animation completes
 *
 * Restores the original stderr file descriptor that was saved when
 * splash_intro_start() was called. This must be called after splash animation
 * completes to allow debug logging to appear on screen again.
 *
 * @note Safe to call multiple times (idempotent)
 * @note Must only be called after splash_intro_start() was called
 *
 * @ingroup session
 */
void splash_restore_stderr(void);

/**
 * @brief Set update notification to display on splash/status screens
 *
 * Sets a notification message that will be displayed on splash screens
 * (intro and status) to inform users about available updates.
 *
 * @param notification Update notification message (can be NULL or empty to clear)
 *
 * @note This should be called before splash_intro_start() or splash_display_status()
 * @note Thread-safe via internal mutex
 *
 * @ingroup session
 */
void splash_set_update_notification(const char *notification);
