
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
#include <ascii-chat/log/log.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/video/ascii/palette.h>
#include <ascii-chat/video/ascii/common.h>  // For simd_caches_destroy_all()
#include <ascii-chat/options/colorscheme.h> // For colorscheme_destroy()
#include <ascii-chat/util/time.h>           // For timer_system_destroy()
#include <ascii-chat/util/pcre2.h>          // For asciichat_pcre2_cleanup_all()
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/crypto/known_hosts.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>       // For RCU-based options access
#include <ascii-chat/discovery/strings.h> // For RCU-based options access
#include <ascii-chat/debug/sync.h>        // For debug_sync_final_cleanup, debug_sync_cleanup_thread, debug_sync_destroy
#include <ascii-chat/debug/mutex.h>       // For mutex_stack_cleanup
#include <ascii-chat/debug/named.h>       // For named_destroy()
#include <ascii-chat/debug/atomic.h>      // For debug_atomic_shutdown()
#include <ascii-chat/debug/memory.h>      // For debug_memory_thread_cleanup in debug builds
#include <ascii-chat/platform/symbols.h>  // For symbols cache init
#include <ascii-chat/platform/terminal.h> // For terminal_screen_cleanup
#include <ascii-chat/platform/keyboard.h> // For keyboard_destroy()
#include <ascii-chat/ui/terminal_screen.h> // For terminal_screen_cleanup
#include <string.h>
#include <ascii-chat/atomic.h>
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
// ASCIICHAT_API is required for proper DLL export on Windows
ASCIICHAT_API int g_argc = 0;
ASCIICHAT_API char **g_argv = NULL;

// Global flags for --color detection (set during options_init before RCU is ready)
ASCIICHAT_API bool g_color_flag_passed = false; // Was --color explicitly passed in argv?
ASCIICHAT_API bool g_color_flag_value = false;  // What was the value of --color?

/* ============================================================================
 * Shutdown Check System Implementation
 * ============================================================================
 */

// Use atomic for thread-safe access to shutdown callback
#include <ascii-chat/atomic.h>
static atomic_ptr_t g_shutdown_callback = {0};

void shutdown_register_callback(shutdown_check_fn callback) {
  atomic_ptr_store(&g_shutdown_callback, (void *)callback);
}

bool shutdown_is_requested(void) {
  shutdown_check_fn callback = (shutdown_check_fn)atomic_ptr_load(&g_shutdown_callback);
  if (callback == NULL) {
    return false; // No callback registered, assume not shutting down
  }
  return callback();
}

/* ============================================================================
 * Shared Initialization Implementation
 * ============================================================================
 */

#if defined(USE_MIMALLOC_DEBUG) && !defined(NDEBUG)
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

  fprintf(stderr, "[SHARED_INIT] Starting, g_shared_initialized=%d\n", g_shared_initialized);
  fflush(stderr);

  // Only register initialization tracking once
  if (!g_shared_initialized) {
    // 1. TIMER SYSTEM - Initialize first so timing is available for everything else
    fprintf(stderr, "[SHARED_INIT] Calling timer_system_init()\n");
    fflush(stderr);
    if (!timer_system_init()) {
      fprintf(stderr, "[SHARED_INIT] ERROR: timer_system_init() failed\n");
      fflush(stderr);
      FATAL(ERROR_PLATFORM_INIT, "Failed to initialize timer system");
    }
    fprintf(stderr, "[SHARED_INIT] timer_system_init() OK\n");
    fflush(stderr);

    // KEYBOARD SYSTEM must be initialized BEFORE logging since keyboard_init() calls log_info()
    // But we need to initialize logging AFTER timer_system_init() and BEFORE keyboard_init()
    // So we move keyboard_init() to AFTER log_init()

#ifndef NDEBUG
    fprintf(stderr, "[SHARED_INIT] Calling named_init()\n");
    fflush(stderr);
    if (named_init() != ASCIICHAT_OK) {
      fprintf(stderr, "[SHARED_INIT] ERROR: named_init() failed\n");
      fflush(stderr);
      return SET_ERRNO(ERROR_PLATFORM_INIT, "Failed to initialize named.");
    }
    fprintf(stderr, "[SHARED_INIT] named_init() OK\n");
    fflush(stderr);
    debug_memory_ensure_init();
    symbol_cache_init();
#endif

    // 2. LOGGING - Initialize system state first, then user-level logging
    // Initialize logging system internal state (terminal caps, colors)
    fprintf(stderr, "[SHARED_INIT] Calling log_system_init()\n");
    fflush(stderr);
    log_system_init();
    fprintf(stderr, "[SHARED_INIT] log_system_init() done\n");
    fflush(stderr);

    // Initialize with provided log file
    // Force stderr for client-like modes when stdout is piped to keep stdout clean
    fprintf(stderr, "[SHARED_INIT] Calling terminal_is_piped_output()\n");
    fflush(stderr);
    bool force_stderr = is_client && terminal_is_piped_output();
    fprintf(stderr, "[SHARED_INIT] terminal_is_piped_output() returned, force_stderr=%d\n", force_stderr);
    fflush(stderr);

    // Use LOG_DEBUG by default; will be reconfigured after options_init()
    fprintf(stderr, "[SHARED_INIT] Calling log_init(log_file=%p, force_stderr=%d)\n", (void *)log_file, force_stderr);
    fflush(stderr);
    log_init(log_file, LOG_DEBUG, force_stderr, false /* don't use_mmap */);
    fprintf(stderr, "[SHARED_INIT] log_init() done\n");
    fflush(stderr);

    // 2. KEYBOARD SYSTEM - Initialize keyboard input AFTER logging (may fail if stdin/tty not available)
    // Failure is not fatal - keyboard_read_nonblocking() safely handles uninitialized state
    fprintf(stderr, "[SHARED_INIT] Calling keyboard_init()\n");
    fflush(stderr);
    keyboard_init();
    fprintf(stderr, "[SHARED_INIT] keyboard_init() done\n");
    fflush(stderr);

    // 3. PLATFORM - Initialize platform-specific functionality (Winsock, etc)
    fprintf(stderr, "[SHARED_INIT] Calling platform_init()\n");
    fflush(stderr);
    if (platform_init() != ASCIICHAT_OK) {
      fprintf(stderr, "[SHARED_INIT] ERROR: platform_init() failed\n");
      fflush(stderr);
      FATAL(ERROR_PLATFORM_INIT, "Failed to initialize platform");
    }
    fprintf(stderr, "[SHARED_INIT] platform_init() OK\n");
    fflush(stderr);

    // 4. BUFFER POOL - Initialize global shared buffer pool
    fprintf(stderr, "[SHARED_INIT] Calling buffer_pool_init_global()\n");
    fflush(stderr);
    buffer_pool_init_global();
    fprintf(stderr, "[SHARED_INIT] buffer_pool_init_global() done\n");
    fflush(stderr);

    // Mark that we've initialized (prevents re-registration of subsystems)
    fprintf(stderr, "[SHARED_INIT] Marking g_shared_initialized=true\n");
    fflush(stderr);
    g_shared_initialized = true;
  }

  fprintf(stderr, "[SHARED_INIT] Returning ASCIICHAT_OK\n");
  fflush(stderr);

  // NOTE: This library function does NOT register atexit() handlers.
  // Library code should not own the process lifecycle. The application
  // is responsible for calling atexit(asciichat_shared_destroy) if it
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
void asciichat_shared_destroy(void) {
  // Guard against double cleanup (can be called explicitly + via atexit)
  static bool shutdown_done = false;
  if (shutdown_done) {
    return;
  }
  shutdown_done = true;

  symbol_cache_destroy();

  // Cleanup in reverse order of initialization (LIFO)
  // This ensures dependencies are properly handled

  // 0. Stop background threads first (before any cleanup that might use them)
  // Terminal resize detection thread (Windows only, checks if active)
  terminal_stop_resize_detection();

#ifndef NDEBUG
  // Lock debug thread - must join before any lock cleanup
  debug_sync_cleanup_thread();

  // Clean up all remaining mutex stacks before memory report
  mutex_stack_cleanup();

  // Clean up current thread's allocations
  debug_sync_final_cleanup();

  // Memory debug thread - prints memory report (must be last)
#if defined(DEBUG_MEMORY)
  debug_memory_thread_cleanup();
#endif

  // Lock debug system - set initialized=false so mutex_lock uses mutex_lock_impl directly
  // This must happen after thread cleanup but before any subsystem that uses mutex_lock
  debug_sync_destroy();

  // Atomic debug cleanup
  debug_atomic_shutdown();
#endif

  // 1. Terminal screen - cleanup frame buffer
  terminal_screen_cleanup();

  // 2. SIMD caches - cleanup CPU-specific caches
  simd_caches_destroy_all();

  // 3. Discovery service strings cache - cleanup session string cache
  acds_strings_destroy();

  // 4. Logging - in correct order (end first, then begin)
  log_shutdown_end();
  log_shutdown_begin();

  // 5. Known hosts - cleanup authentication state
  known_hosts_destroy();

  // 6. Options state - cleanup RCU-based options
  options_state_destroy();

  // 7. Buffer pool - cleanup global buffer pool
  buffer_pool_cleanup_global();

  // 8. Platform cleanup - restores terminal, cleans up platform resources
  // (includes symbol cache cleanup on Windows)
  platform_destroy();

  // 9. Keyboard - restore terminal settings (redundant with platform_destroy but safe)
  keyboard_destroy();

  // 10. Timer system - cleanup timers (may still log!)
  timer_system_destroy();

  // 11. Error context cleanup
  asciichat_errno_destroy();

  // 12. Logging cleanup - free log format buffers and system state
  log_destroy();
  log_system_destroy();

  // 13. Mutex stack cleanup - must be before memory report so stacks are freed
#ifndef NDEBUG
  mutex_stack_cleanup();
  // Final cleanup: free the main thread's debug allocations
  debug_sync_final_cleanup();
#endif

  // 14. Memory stats (debug builds only) - runs with colors still available
  //     Note: PCRE2 singletons are cleaned up later via atexit handler (runs after this function)
  //     This ensures code can still use PCRE2 during normal program shutdown
  //     Note: debug_memory_report() is also called manually during shutdown in server_main()
  //     The function is safe to call multiple times with polling-based locking
#if defined(USE_MIMALLOC_DEBUG) && !defined(NDEBUG)
  print_mimalloc_stats();
#endif

#if defined(DEBUG_MEMORY) && !defined(NDEBUG)
  debug_memory_report();
#endif

  // 15. Color cleanup - free compiled ANSI strings (AFTER memory report)
  log_cleanup_colors();
  colorscheme_destroy();

  // 16. Clean up binary path cache explicitly
  // Note: This is also called by platform_destroy() via atexit(), but it's idempotent
  platform_cleanup_binary_path_cache();

  // 17. Clean up RCU-based options state
  options_state_destroy();

  // 18. Clean up errno context (allocated strings, backtrace symbols)
  asciichat_errno_destroy();

#ifndef NDEBUG
  // 19. Named registry - cleanup all registered thread names and debug entries
  // Must come BEFORE PCRE2 cleanup since named_destroy() uses path normalization which needs PCRE2
  named_destroy();
#endif

  // 20. PCRE2 - cleanup all regex singletons together (after named_destroy)
  asciichat_pcre2_cleanup_all();

  // 21. timer resolution restore
  platform_restore_timer_resolution(); // Restore timer resolution (no-op on POSIX)

  // 22. Clean up SIMD caches
  simd_caches_destroy_all();

  // 23. Clean up symbol cache
  // This must be called BEFORE log_destroy() as symbol_cache_destroy() uses log_debug()
  // Safe to call even if atexit() runs - it's idempotent (checks g_symbol_cache_initialized)
  // Also called via platform_destroy() atexit handler, but explicit call ensures proper ordering
  symbol_cache_destroy();

  // 24. Clean up global buffer pool (explicitly, as atexit may not run on Ctrl-C)
  // Note: This is also registered with atexit(), but calling it explicitly is safe (idempotent)
  // Safe to call even if atexit() runs - it checks g_global_buffer_pool and sets it to NULL
  buffer_pool_cleanup_global();

#ifndef NDEBUG
  // 25. Symbol cache cleanup
  symbol_cache_destroy();
#endif
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
