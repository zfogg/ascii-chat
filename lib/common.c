
/**
 * @file common.c
 * @ingroup common
 * @brief 🔧 Core utilities: memory management, safe macros, and cross-platform helpers
 *
 */

// Platform abstraction includes memory sizing functions
#include "common.h"
#include "platform/system.h"
#include "platform/init.h"
#include "platform/memory.h"
#include "log/logging.h"
#include "buffer_pool.h"
#include "video/palette.h"
#include "video/simd/common.h"   // For simd_caches_destroy_all()
#include "video/webcam/webcam.h" // For webcam_cleanup()
#include "util/time.h"           // For timer_system_cleanup()
#include "asciichat_errno.h"
#include "crypto/known_hosts.h"
#include "options/options.h"
#include "options/rcu.h" // For RCU-based options access
#include <string.h>
#include <stdatomic.h>
#include <limits.h>
#include <stdlib.h>

#ifndef _WIN32
#include <pthread.h> // For PTHREAD_MUTEX_INITIALIZER on POSIX
#endif

/* ============================================================================
 * Global Variables for Early Argv Inspection
 * ============================================================================
 */

// These globals are used by lib/platform/terminal.c to detect --color flag
// during early execution phases (e.g., when --help is processed before
// options are fully parsed and published to RCU)
int g_argc = 0;
char **g_argv = NULL;

// Global flags for --color detection (set during options_init before RCU is ready)
bool g_color_flag_passed = false; // Was --color explicitly passed in argv?
bool g_color_flag_value = false;  // What was the value of --color?

/* ============================================================================
 * Shutdown Check System Implementation
 * ============================================================================
 */

// Use atomic for thread-safe access to shutdown callback
#include <stdatomic.h>
static _Atomic(shutdown_check_fn) g_shutdown_callback = NULL;

void shutdown_register_callback(shutdown_check_fn callback) {
  atomic_store(&g_shutdown_callback, callback);
}

bool shutdown_is_requested(void) {
  shutdown_check_fn callback = atomic_load(&g_shutdown_callback);
  if (callback == NULL) {
    return false; // No callback registered, assume not shutting down
  }
  return callback();
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

// Guard to prevent multiple atexit handler registration
// (can happen if options_init() is called multiple times during startup)
// But allow subsystem reinitialization for tests and other use cases
static bool g_atexit_handlers_registered = false;

asciichat_error_t asciichat_shared_init(bool is_client) {
  // Reconfigure logging with final settings (log level and is_client routing)
  // Log file was already set in options_init after mode detection
  // Client mode: route ALL logs to stderr to keep stdout clean for ASCII art output
  const options_t *opts = options_get();
  const char *log_file = opts && opts->log_file[0] != '\0' ? opts->log_file : "ascii-chat.log";
  // Use log_level from parsed options (set by options_init)
  // Default levels (when no --log-level arg or LOG_LEVEL env var):
  //   Debug/Dev builds: LOG_DEBUG
  //   Release/RelWithDebInfo builds: LOG_INFO
  // Precedence: LOG_LEVEL env var > --log-level CLI arg > build type default
  // use_mmap=false: Regular file logging (default for developers, allows tail -f for log monitoring)
  // Note: log_init is safe to call multiple times; it will update routing (is_client) if needed
  log_init(log_file, GET_OPTION(log_level), is_client, false /* don't use_mmap */);

  // Register memory debugging stats FIRST so it runs LAST at exit
  // (atexit callbacks run in LIFO order - last registered runs first)
  // This ensures all cleanup handlers run before the memory report is printed
  // Only register atexit handlers once
  if (!g_atexit_handlers_registered) {
#if defined(DEBUG_MEMORY) && !defined(USE_MIMALLOC_DEBUG) && !defined(NDEBUG)
    (void)atexit(debug_memory_report);
#elif defined(USE_MIMALLOC_DEBUG) && !defined(NDEBUG)
    (void)atexit(print_mimalloc_stats);
    UNUSED(print_mimalloc_stats);
#endif

    // Initialize platform-specific functionality (Winsock, etc)
    if (platform_init() != ASCIICHAT_OK) {
      FATAL(ERROR_PLATFORM_INIT, "Failed to initialize platform");
    }
    (void)atexit(platform_cleanup);

    // Initialize global shared buffer pool
    buffer_pool_init_global();
    (void)atexit(buffer_pool_cleanup_global);

    // Register errno cleanup
    (void)atexit(asciichat_errno_cleanup);

    // Register options state cleanup
    (void)atexit(options_state_shutdown);

    // Register known_hosts cleanup
    (void)atexit(known_hosts_cleanup);

    // Register SIMD caches cleanup (for all modes: server, client, mirror)
    (void)atexit(simd_caches_destroy_all);

    // Register webcam cleanup (frees cached test pattern and webcam resources)
    (void)atexit(webcam_cleanup);

    // Register timer system cleanup AFTER memory report registration
    // This ensures timers are freed BEFORE memory_report runs (due to atexit LIFO order)
    (void)atexit(timer_system_cleanup);

    g_atexit_handlers_registered = true;
  }

  // Apply quiet mode setting BEFORE log_init so initialization messages are suppressed
  if (GET_OPTION(quiet)) {
    log_set_terminal_output(false);
  }

  // Initialize palette based on command line options
  const char *custom_chars = opts && opts->palette_custom_set ? opts->palette_custom : NULL;
  if (apply_palette_config(GET_OPTION(palette_type), custom_chars) != 0) {
    FATAL(ERROR_CONFIG, "Failed to apply palette configuration");
  }

  // Truncate log if it's already too large
  log_truncate_if_large();

  // Set quiet mode for memory debugging (registration done at function start)
#if defined(DEBUG_MEMORY) && !defined(USE_MIMALLOC_DEBUG) && !defined(NDEBUG)
  debug_memory_set_quiet_mode(GET_OPTION(quiet));
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
