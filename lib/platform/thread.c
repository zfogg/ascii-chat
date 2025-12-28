/**
 * @file platform/thread.c
 * @ingroup platform
 * @brief 🧵 Thread utilities and helpers
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#include "thread.h"
#include "common.h"
#include "logging.h"

asciichat_error_t thread_create_or_fail(asciithread_t *thread, void *(*func)(void *), void *arg, const char *thread_name) {
  if (!thread || !func || !thread_name) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for thread creation");
  }

  int result = ascii_thread_create(thread, func, arg);
  if (result != 0) {
    return SET_ERRNO(ERROR_PLATFORM_INIT, "Failed to create %s thread (result=%d)", thread_name, result);
  }

  log_debug("Created %s thread successfully", thread_name);
  return ASCIICHAT_OK;
}
