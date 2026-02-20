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
#include <stdatomic.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

// ============================================================================
// WASM-Specific Implementations
// ============================================================================

const char *platform_getenv(const char *name) {
  (void)name;
  // Environment variables not supported in WASM browser context
  // Calling getenv() causes "memory access out of bounds" errors
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
// Include Generic Implementations
// ============================================================================
// Generic implementations (safe_snprintf, safe_fprintf, platform_print_backtrace_symbols, etc.)
// are provided by the generic system.c which is included here
#include "../system.c"
