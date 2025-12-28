/**
 * @file thread_helpers.c
 * @ingroup thread_helpers
 * @brief 🧵 Thread creation and management helper implementations
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#include "thread_helpers.h"
#include "common.h"
#include "logging.h"

int thread_create_or_fail(ascii_thread_t *thread, void *(*func)(void *), void *arg, const char *thread_name) {
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
