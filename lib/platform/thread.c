/**
 * @file platform/thread.c
 * @ingroup platform
 * @brief 🧵 Thread utilities and helpers
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#include "thread.h"
#include "core/common.h"
#include "log/logging.h"

asciichat_error_t thread_create_or_fail(asciithread_t *thread, void *(*func)(void *), void *arg,
                                        const char *thread_name, uint32_t client_id) {
  if (!thread || !func || !thread_name) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for thread creation");
  }

  int result = ascii_thread_create(thread, func, arg);
  if (result != 0) {
    return SET_ERRNO(ERROR_PLATFORM_INIT, "Failed to create %s thread for client %u (result=%d)", thread_name,
                     client_id, result);
  }

  log_debug("Created %s thread for client %u successfully", thread_name, client_id);
  return ASCIICHAT_OK;
}
