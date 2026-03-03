/**
 * @file server_like.c
 * @brief Implementation of server_like abstraction for unified server mode lifecycle
 * @ingroup session
 */

#include "server_like.h"
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/abstraction.h>
#include <signal.h>

/* ============================================================================
 * Static State for Signal Handler
 * ============================================================================ */

/** Stored interrupt callback for signal handler context */
static session_server_like_interrupt_fn g_server_like_interrupt_fn = NULL;

/**
 * Signal handler wrapper that calls the stored interrupt_fn.
 * Registered via set_interrupt_callback().
 *
 * Must be async-signal-safe (only uses the stored function pointer).
 */
static void server_like_signal_wrapper(void) {
  if (g_server_like_interrupt_fn) {
    g_server_like_interrupt_fn();
  }
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

asciichat_error_t session_server_like_run(const session_server_like_config_t *config) {
  if (!config) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "config is NULL");
  }

  if (!config->init_fn) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "init_fn is NULL");
  }

  if (!config->run_fn) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "run_fn is NULL");
  }

  if (!config->interrupt_fn) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "interrupt_fn is NULL");
  }

  asciichat_error_t result = ASCIICHAT_OK;

  /* === INITIALIZATION === */

  /* 1. Validate keepawake mutual exclusivity */
  bool keepawake_enabled = GET_OPTION(enable_keepawake);
  bool keepawake_disabled = GET_OPTION(disable_keepawake);
  if (keepawake_enabled && keepawake_disabled) {
    return SET_ERRNO(ERROR_INVALID_PARAM,
                     "Cannot specify both --keepawake and --no-keepawake");
  }

  /* 2. Enable keepawake if requested (default is off for servers) */
  if (keepawake_enabled) {
    platform_enable_keepawake();
  }

  /* 3. Store interrupt callback for signal handlers */
  g_server_like_interrupt_fn = config->interrupt_fn;

  /* 4. Register signal handlers - note: signal handlers call interrupt_fn */
  platform_signal(SIGINT, (signal_handler_t)server_like_signal_wrapper);
  platform_signal(SIGTERM, (signal_handler_t)server_like_signal_wrapper);
  platform_signal(SIGPIPE, SIG_IGN);

  /* 5. Call mode-specific initialization */
  result = config->init_fn(config->init_user_data);
  if (result != ASCIICHAT_OK) {
    log_error("Mode initialization failed");
    goto cleanup;
  }

  /* === RUN === */

  /* 6. Call mode-specific blocking run loop */
  result = config->run_fn(config->run_user_data);

cleanup:
  /* === CLEANUP === */

  /* 7. Disable keepawake */
  if (keepawake_enabled) {
    platform_disable_keepawake();
  }

  return result;
}
