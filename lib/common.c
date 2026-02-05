
/**
 * @file common.c
 * @ingroup common
 * @brief 🔧 Core utilities: memory management, safe macros, and cross-platform helpers
 *
 */

// Platform abstraction includes memory sizing functions
#include <ascii-chat/common.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/video/palette.h>
#include <ascii-chat/video/simd/common.h>   // For simd_caches_destroy_all()
#include <ascii-chat/video/webcam/webcam.h> // For webcam_cleanup()
#include <ascii-chat/options/colorscheme.h> // For colorscheme_shutdown()
#include <ascii-chat/util/time.h>           // For timer_system_cleanup()
#include <ascii-chat/util/pcre2.h>          // For asciichat_pcre2_cleanup_all()
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/crypto/known_hosts.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>       // For RCU-based options access
#include <ascii-chat/discovery/strings.h> // For RCU-based options access
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
#include <ascii-chat/debug/memory.h>
#elif defined(USE_MIMALLOC_DEBUG) && !defined(NDEBUG)
static void print_mimalloc_stats(void);
#endif

// Guard to prevent multiple initialization of shared subsystems
// (can happen if asciichat_shared_init() is called multiple times during startup)
// But allow subsystem reinitialization for tests and other use cases
static bool g_shared_initialized = false;

#ifndef _WIN32
/**
 * @brief Fork handler for child process
 *
 * Reset the initialization flag so the child process will reinitialize
 * all subsystems that were already initialized in the parent.
 */
static void asciichat_common_atfork_child(void) {
  // Reset the shared initialization flag so asciichat_shared_init() will reinitialize subsystems
  g_shared_initialized = false;
}

/**
 * @brief Register fork handlers for common module
 */
__attribute__((constructor)) static void register_common_fork_handlers(void) {
  pthread_atfork(NULL, NULL, asciichat_common_atfork_child);
}
#endif

asciichat_error_t asciichat_shared_init(const char *log_file, bool is_client) {
  // Initialize shared subsystems BEFORE options_init()
  // This allows options_init() to use properly configured logging with colors
  //
  // NOTE: This function is called BEFORE options_init(), so we can't use GET_OPTION()
  // Palette config and quiet mode will be applied after options_init() is called

  // Only register initialization tracking once
  if (!g_shared_initialized) {
    // 1. TIMER SYSTEM - Initialize first so timing is available for everything else
    if (!timer_system_init()) {
      FATAL(ERROR_PLATFORM_INIT, "Failed to initialize timer system");
    }

    // 2. LOGGING - Initialize with provided log file
    // Force stderr for client-like modes when stdout is piped to keep stdout clean
    bool force_stderr = is_client && !platform_isatty(STDOUT_FILENO);
    // Use LOG_DEBUG by default; will be reconfigured after options_init()
    log_init(log_file, LOG_DEBUG, force_stderr, false /* don't use_mmap */);

    // 3. PLATFORM - Initialize platform-specific functionality (Winsock, etc)
    if (platform_init() != ASCIICHAT_OK) {
      FATAL(ERROR_PLATFORM_INIT, "Failed to initialize platform");
    }

    // 4. BUFFER POOL - Initialize global shared buffer pool
    buffer_pool_init_global();

    // Mark that we've initialized (prevents re-registration of subsystems)
    g_shared_initialized = true;
  }

  // NOTE: This library function does NOT register atexit() handlers.
  // Library code should not own the process lifecycle. The application
  // is responsible for calling atexit(asciichat_shared_shutdown) if it
  // wants automatic cleanup on normal exit.

  return ASCIICHAT_OK;
}

/**
 * @brief Clean up all shared library subsystems
 *
 * Performs comprehensive cleanup of all subsystems initialized by
 * asciichat_shared_init(). All cleanup functions are idempotent
 * (safe to call multiple times), so this can be called explicitly
 * and also via atexit() without issues.
 *
 * Cleanup order is carefully chosen to be the reverse of initialization,
 * ensuring dependencies are respected (e.g., timer cleanup before
 * memory reporting, memory reporting before errno cleanup).
 *
 * @note This function is safe to call even if init failed or wasn't called.
 * @note All subsystem cleanup functions must remain idempotent.
 */
void asciichat_shared_shutdown(void) {
  // Cleanup in reverse order of initialization (LIFO)
  // This ensures dependencies are properly handled

  // 1. Webcam - cleanup resources
  webcam_cleanup();

  // 2. SIMD caches - cleanup CPU-specific caches
  simd_caches_destroy_all();

  // 3. Discovery service strings cache - cleanup session string cache
  acds_strings_cleanup();

  // 4. Logging - in correct order (end first, then begin)
  log_shutdown_end();
  log_shutdown_begin();

  // 5. Known hosts - cleanup authentication state
  known_hosts_cleanup();

  // 6. Options state - cleanup RCU-based options
  options_state_shutdown();

  // 7. Buffer pool - cleanup global buffer pool
  buffer_pool_cleanup_global();

  // 8. Platform cleanup - restores terminal, cleans up platform resources
  // (includes symbol cache cleanup on Windows)
  platform_cleanup();

  // 9. Keyboard - restore terminal settings (redundant with platform_cleanup but safe)
  extern void keyboard_cleanup(void);
  keyboard_cleanup();

  // 10. Timer system - cleanup timers (may still log!)
  timer_system_cleanup();

  // 11. Error context cleanup
  asciichat_errno_cleanup();

  // 12. Memory stats (debug builds only) - runs with colors still available
  //     Note: PCRE2 singletons are ignored in the report (expected system allocations)
#if defined(USE_MIMALLOC_DEBUG) && !defined(NDEBUG)
  print_mimalloc_stats();
#elif defined(DEBUG_MEMORY) && !defined(NDEBUG)
  debug_memory_report();
#endif

  // 13. Color cleanup - free compiled ANSI strings (AFTER memory report)
  log_cleanup_colors();
  colorscheme_shutdown();

  // 14. PCRE2 - cleanup all regex singletons together
  asciichat_pcre2_cleanup_all();
}

#if defined(USE_MIMALLOC_DEBUG) && !defined(NDEBUG)
// Wrapper function for mi_stats_print to use with atexit()
// mi_stats_print takes a parameter, but atexit requires void(void)
static void print_mimalloc_stats(void) {
  // Skip memory report if an action flag was passed (for clean action output)
  // unless explicitly forced via ASCII_CHAT_MEMORY_DEBUG environment variable
  if (has_action_flag() && !SAFE_GETENV("ASCII_CHAT_MEMORY_DEBUG")) {
    return;
  }
  mi_stats_print(NULL); // NULL = print to stderr
}
#endif
