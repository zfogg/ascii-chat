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

int thread_create_or_fail(asciithread_t *thread, void *(*func)(void *), void *arg, const char *thread_name) {
  if (!thread || !func || !thread_name) {
    return -1;
  }

  int result = ascii_thread_create(thread, func, arg);
  if (result != 0) {
    log_error("Failed to create %s thread (result=%d)", thread_name, result);
    return result;
  }

  log_debug("Created %s thread successfully", thread_name);
  return 0;
}
