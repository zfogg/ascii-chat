
/**
 * @file platform/abstraction.c
 * @ingroup platform
 * @brief 🏗️ Common platform abstraction stubs (OS-specific code in posix/ and windows/ subdirectories)
 */

// ============================================================================
// Common Platform Functions
// ============================================================================
// This file is reserved for common platform functions that don't need
// OS-specific implementations. Currently all functions are OS-specific.
//
// The OS-specific implementations are in:
// - platform_windows.c (Windows)
// - platform_posix.c (POSIX/Unix/Linux/macOS)
//
// Socket-specific implementations are in:
// - platform/socket.c (Common socket utilities like socket_optimize_for_streaming)
// ============================================================================

// ============================================================================
// Thread Creation Helpers
// ============================================================================

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
