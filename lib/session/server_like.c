/**
 * @file server_like.c
 * @ingroup session
 * @brief Shared initialization and lifecycle management for server-like modes
 */

#include <ascii-chat/session/server_like.h>
#include <ascii-chat/session/session_log_buffer.h>

#include <ascii-chat/log/logging.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/terminal.h>

/* External exit function from src/main.c */
extern bool should_exit(void);

asciichat_error_t session_server_like_run(const session_server_like_config_t *config) {
  if (!config || !config->init_fn || !config->run_fn) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "config, init_fn, or run_fn is NULL");
  }

  asciichat_error_t result = ASCIICHAT_OK;

  /* ============================================================================
   * SETUP: Terminal and Logging
   * ============================================================================ */

  log_debug("session_server_like_run(): Setting up terminal and logging");

  /* Force stderr when stdout is piped (prevents ASCII corruption in output stream) */
  bool should_force_stderr = terminal_should_force_stderr();
  log_debug("terminal_should_force_stderr()=%d", should_force_stderr);

  if (should_force_stderr) {
    log_set_force_stderr(true);
  }

  /* Disable terminal logging during status screen rendering (mode will re-enable as needed) */
  log_set_terminal_output(false);

  /* ============================================================================
   * SETUP: Keepawake System
   * ============================================================================ */

  log_debug("session_server_like_run(): Validating keepawake options");

  bool enable_ka = GET_OPTION(enable_keepawake);
  bool disable_ka = GET_OPTION(disable_keepawake);
  log_debug("enable_keepawake=%d, disable_keepawake=%d", enable_ka, disable_ka);

  if (enable_ka && disable_ka) {
    result = SET_ERRNO(ERROR_INVALID_PARAM, "--keepawake and --no-keepawake are mutually exclusive");
    goto cleanup;
  }

  log_debug("session_server_like_run(): Enabling keepawake if needed");

  if (!disable_ka) {
    log_debug("Calling platform_enable_keepawake()");
    if (platform_enable_keepawake() != ASCIICHAT_OK) {
      log_warn("Failed to enable keepawake, continuing anyway");
    }
  }

  /* ============================================================================
   * SETUP: Session Log Buffer
   * ============================================================================ */

  log_debug("session_server_like_run(): Initializing session log buffer");
  if (session_log_buffer_init() != ASCIICHAT_OK) {
    log_warn("Failed to initialize session log buffer, continuing without log capture");
  }

  /* ============================================================================
   * SETUP: Mode-Specific Initialization
   * ============================================================================ */

  log_debug("session_server_like_run(): Calling mode init_fn");
  result = config->init_fn(config->init_user_data);
  if (result != ASCIICHAT_OK) {
    log_error("Mode initialization failed");
    goto cleanup;
  }

  /* ============================================================================
   * RUN: Mode-Specific Server Loop
   * ============================================================================ */

  log_debug("session_server_like_run(): Calling config->run_fn()");
  result = config->run_fn(config->run_user_data);
  log_debug("config->run_fn() returned with result=%d", result);

  /* ============================================================================
   * CLEANUP (always runs, even on error)
   * ============================================================================ */

cleanup:
  /* Re-enable terminal output for shutdown logs */
  log_set_terminal_output(true);

  /* Cleanup session log buffer */
  log_debug("Cleaning up session log buffer");
  session_log_buffer_destroy();

  /* Disable keepawake (re-allow OS to sleep) */
  log_debug("Disabling keepawake");
  platform_disable_keepawake();

  log_set_terminal_output(false);

  return result;
}
