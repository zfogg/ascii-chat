#pragma once

/**
 * @file ui/update_banner.h
 * @brief Update available prompt screen shown after splash
 * @ingroup session
 * @addtogroup session
 * @{
 *
 * Provides a blocking prompt screen that shows when a new version of ascii-chat
 * is available. Displayed after the splash screen ends, before video rendering
 * begins. The user can choose to exit and update (Y/Enter) or continue (N/Esc).
 *
 * Thread-safe: update_banner_set_result() can be called from the background
 * update check thread while the splash is still running.
 */

#include <ascii-chat/network/update_checker.h>
#include <stdbool.h>

struct session_display_ctx;

/**
 * @brief Store the update check result (thread-safe)
 *
 * Called from the background update check thread after a successful check
 * that found an available update. The result is stored under mutex protection.
 *
 * @param result Update check result to store (copied internally)
 */
void update_banner_set_result(const update_check_result_t *result);

/**
 * @brief Check if an update is available (thread-safe)
 * @return true if update_banner_set_result() was called with an available update
 */
bool update_banner_has_update(void);

/**
 * @brief Show the update prompt screen and wait for user input
 *
 * Renders a centered box with version info, upgrade command, and release URL.
 * Blocks until the user presses a key:
 * - Y/y/Enter: returns true (user wants to exit and update)
 * - N/n/Esc: returns false (user wants to continue)
 *
 * @param ctx Display context for terminal output
 * @return true if user chose to update, false if user chose to continue
 */
bool update_banner_show_prompt(struct session_display_ctx *ctx);

/**
 * @brief Print upgrade instructions to stdout and prepare for clean exit
 *
 * Called after update_banner_show_prompt() returns true. Prints a clear,
 * copy-pasteable upgrade command to stdout.
 */
void update_banner_print_instructions(void);

/**
 * @brief Start the background update check thread
 *
 * Spawns a background thread that performs the update check (DNS + HTTPS)
 * and stores the result. The splash screen notification is also set if an
 * update is found. Call update_banner_wait_for_check() later to join.
 */
void update_banner_start_check(void);

/**
 * @brief Wait for the background update check thread to finish
 *
 * Blocks up to 5 seconds for the background check to complete. Safe to call
 * even if no check was started.
 */
void update_banner_wait_for_check(void);

/** @} */
