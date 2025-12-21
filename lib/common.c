
/**
 * @file common.c
 * @ingroup common
 * @brief 🔧 Core utilities: memory management, safe macros, and cross-platform helpers
 */

// Platform-specific malloc size headers - MUST come before common.h
// to avoid conflicts with debug memory macros
#ifdef _WIN32
#if defined(_MSC_VER)
#include <excpt.h>
#endif
#include <malloc.h> // For _msize
#elif defined(__APPLE__)
#include <malloc/malloc.h> // For malloc_size on macOS
#elif defined(__linux__)
// For Linux systems - need GNU extensions for malloc_usable_size
#include <features.h>
#include <malloc.h>
// If _GNU_SOURCE is not defined or we don't have glibc, declare it ourselves
#if !defined(_GNU_SOURCE) || !defined(__GLIBC__)
extern size_t malloc_usable_size(void *ptr);
#endif
#endif

#include "common.h"
#include "platform/system.h"
#include "platform/init.h"
#include "logging.h"
#include "buffer_pool.h"
#include "palette.h"
#include "asciichat_errno.h"
#include "crypto/known_hosts.h"
#include "options.h"
#include <string.h>
#include <stdatomic.h>
#include <limits.h>
#include <stdlib.h>

#ifndef _WIN32
#include <pthread.h> // For PTHREAD_MUTEX_INITIALIZER on POSIX
#endif

// Global frame rate variable - can be set via command line
int g_max_fps = 0; // 0 means use default

/* ============================================================================
 * Shutdown Check System Implementation
 * ============================================================================
 */

static shutdown_check_fn g_shutdown_callback = NULL;

void shutdown_register_callback(shutdown_check_fn callback) {
  g_shutdown_callback = callback;
}

bool shutdown_is_requested(void) {
  if (g_shutdown_callback == NULL) {
    return false; // No callback registered, assume not shutting down
  }
  return g_shutdown_callback();
}

/* ============================================================================
 * Shared Initialization Implementation
 * ============================================================================
 */

#if defined(DEBUG_MEMORY) && !defined(USE_MIMALLOC_DEBUG) && !defined(NDEBUG)
#include "debug/memory.h"
#elif defined(USE_MIMALLOC_DEBUG) && !defined(NDEBUG)
static void print_mimalloc_stats(void);
#endif

asciichat_error_t asciichat_shared_init(const char *default_log_filename, bool is_client) {
  // Initialize platform-specific functionality (Winsock, etc)
  if (platform_init() != ASCIICHAT_OK) {
    FATAL(ERROR_PLATFORM_INIT, "Failed to initialize platform");
  }
  (void)atexit(platform_cleanup);

  // Initialize logging with default filename
  // Client mode: route ALL logs to stderr to keep stdout clean for ASCII art output
  const char *log_filename = (strlen(opt_log_file) > 0) ? opt_log_file : default_log_filename;
  // Use opt_log_level from command-line argument (set by options_init in main.c)
  // Default levels (when no --log-level arg or LOG_LEVEL env var):
  //   Debug/Dev builds: LOG_DEBUG
  //   Release/RelWithDebInfo builds: LOG_INFO
  // Precedence: LOG_LEVEL env var > --log-level CLI arg > build type default
  log_init(log_filename, opt_log_level, is_client);

  // Initialize palette based on command line options
  const char *custom_chars = opt_palette_custom_set ? opt_palette_custom : NULL;
  if (apply_palette_config(opt_palette_type, custom_chars) != 0) {
    FATAL(ERROR_CONFIG, "Failed to apply palette configuration");
  }

  // Initialize global shared buffer pool
  data_buffer_pool_init_global();
  (void)atexit(data_buffer_pool_cleanup_global);

  // Register errno cleanup
  (void)atexit(asciichat_errno_cleanup);

  // Register known_hosts cleanup
  (void)atexit(known_hosts_cleanup);

  // Truncate log if it's already too large
  log_truncate_if_large();

  // Print memory debugging stats at exit (only in debug builds)
#if defined(DEBUG_MEMORY) && !defined(USE_MIMALLOC_DEBUG) && !defined(NDEBUG)
  debug_memory_set_quiet_mode(opt_quiet);
  (void)atexit(debug_memory_report);
#elif defined(USE_MIMALLOC_DEBUG) && !defined(NDEBUG)
  // Register mimalloc stats printer at exit
  (void)atexit(print_mimalloc_stats);
  UNUSED(print_mimalloc_stats);
#endif

  return ASCIICHAT_OK;
}

#if defined(USE_MIMALLOC_DEBUG) && !defined(NDEBUG)
// Wrapper function for mi_stats_print to use with atexit()
// mi_stats_print takes a parameter, but atexit requires void(void)
static void print_mimalloc_stats(void) {
  mi_stats_print(NULL); // NULL = print to stderr
}
#endif
