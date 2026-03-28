/**
 * @file platform/wasm/system.c
 * @brief System utilities for WASM/Emscripten - includes generic implementations
 * @ingroup platform
 *
 * This file is structured like posix/system.c and windows/system.c:
 * 1. Platform-specific implementations go first
 * 2. Generic implementations from ../system.c are included at the end
 */

#ifndef __EMSCRIPTEN__
#error "This file is for WASM/Emscripten builds only"
#endif

#include <ascii-chat/platform/api.h>
#include <ascii-chat/platform/internal.h>
#include <ascii-chat/common.h>
#include <ascii-chat/common/buffer_sizes.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/platform/symbols.h>
#include <ascii-chat/options/options.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ascii-chat/atomic.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

// ============================================================================
// WASM-Specific Implementations
// ============================================================================

const char *platform_getenv(const char *name) {
  write(STDERR_FILENO, "[WASM-GETENV] ENTRY\n", 20);
  (void)name;
  // Environment variables not supported in WASM browser context
  // Calling getenv() causes "memory access out of bounds" errors
  write(STDERR_FILENO, "[WASM-GETENV] returning NULL\n", 29);
  return NULL;
}

const char *platform_strerror(int errnum) {
  return strerror(errnum);
}

int platform_setenv(const char *name, const char *value) {
  (void)name;
  (void)value;
  return -1; // Not supported in WASM
}

int platform_unsetenv(const char *name) {
  (void)name;
  return -1; // Not supported in WASM
}

pid_t platform_get_pid(void) {
  return 1; // WASM runs in browser - no process ID concept
}

// ============================================================================
// I/O Redirection for WASM Terminal
// ============================================================================

// Forward declare the EM_JS bridge from terminal.c
extern void js_terminal_write(const char *data, int len);

/**
 * Override platform_write to route STDOUT to xterm.js
 * This allows the native C render pipeline (session_display_write_ascii) to
 * output ASCII frames directly to the browser's xterm.js terminal.
 */
ssize_t platform_write(int fd, const void *buf, size_t count) {
  if (!buf || count == 0) {
    // Don't warn about these - it's a valid case (empty write)
    return 0;
  }

  if (fd == STDOUT_FILENO) {
    // Route STDOUT to xterm.js
    // Cast to avoid alignment warnings on pointer conversion
    js_terminal_write((const char *)buf, (int)count);
    return (ssize_t)count;
  }

  // For stderr and other fds, use the default write()
  return write(fd, buf, count);
}

// ============================================================================
// Include Generic Implementations
// ============================================================================
// Generic implementations (safe_snprintf, safe_fprintf, platform_print_backtrace_symbols, etc.)
// are provided by the generic system.c which is included here
#include "../system.c"
