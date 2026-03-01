/**
 * @file sync.c
 * @ingroup debug_sync
 * @brief 🔒 Synchronization primitive debugging with dynamic state inspection
 * @date February 2026
 *
 * Clean architecture: queries named.c registry for current state.
 * No internal collection overhead - inspects lock structs directly.
 */

#include <ascii-chat/debug/sync.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/debug/backtrace.h>
#include <ascii-chat/debug/mutex.h>
#include <ascii-chat/debug/memory.h>
#include <ascii-chat/platform/cond.h>
#include <ascii-chat/platform/mutex.h> // Must come after cond.h since cond.h includes it
#include <ascii-chat/platform/rwlock.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/options/options.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <inttypes.h>
#include <stdatomic.h>

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Format elapsed time string (uses time_pretty for consistent formatting)
 * @param elapsed_ns Elapsed time in nanoseconds
 * @param buffer Output buffer
 * @param size Buffer size
 */
static void format_elapsed(uint64_t elapsed_ns, char *buffer, size_t size) {
  time_pretty(elapsed_ns, -1, buffer, size);
}

/**
 * @brief Extract timing info from a mutex_t as a single line
 * @param mutex Pointer to mutex_t
 * @param buffer Output buffer for formatted info
 * @param size Buffer size
 * @return Number of bytes written
 */
static int format_mutex_timing(const mutex_t *mutex, char *buffer, size_t size) {
  if (!mutex)
    return 0;

  // If mutex was never locked, return empty
  if (mutex->last_lock_time_ns == 0) {
    return 0;
  }

  int offset = 0;
  uint64_t now_ns = time_get_ns();
  char lock_str[64] = "";
  char unlock_str[64] = "";
  char held_str[256] = "";

  if (mutex->last_lock_time_ns > 0 && mutex->last_lock_time_ns <= now_ns) {
    char elapsed_str[64];
    format_elapsed(now_ns - mutex->last_lock_time_ns, elapsed_str, sizeof(elapsed_str));
    snprintf(lock_str, sizeof(lock_str), "lock=%s", elapsed_str);
  }

  if (mutex->last_unlock_time_ns > 0 && mutex->last_unlock_time_ns <= now_ns) {
    char elapsed_str[64];
    format_elapsed(now_ns - mutex->last_unlock_time_ns, elapsed_str, sizeof(elapsed_str));
    snprintf(unlock_str, sizeof(unlock_str), "unlock=%s", elapsed_str);
  }

  if (mutex->currently_held_by_key != 0) {
    snprintf(held_str, sizeof(held_str), "[LOCKED_BY=0x%lx]", (unsigned long)mutex->currently_held_by_key);
  } else {
    snprintf(held_str, sizeof(held_str), "[FREE]");
  }

  offset += snprintf(buffer + offset, size - offset, "%s %s %s", lock_str, unlock_str, held_str);

  return offset;
}

/**
 * @brief Extract timing info from an rwlock_t as a single line
 * @param rwlock Pointer to rwlock_t
 * @param buffer Output buffer for formatted info
 * @param size Buffer size
 * @return Number of bytes written
 */
static int format_rwlock_timing(const rwlock_t *rwlock, char *buffer, size_t size) {
  if (!rwlock)
    return 0;

  // If rwlock was never locked, return empty
  if (rwlock->last_rdlock_time_ns == 0 && rwlock->last_wrlock_time_ns == 0) {
    return 0;
  }

  int offset = 0;
  uint64_t now_ns = time_get_ns();
  char rdlock_str[64] = "";
  char wrlock_str[64] = "";
  char unlock_str[64] = "";
  char write_held_str[256] = "";
  char read_held_str[256] = "";
  char status_str[128] = "";

  if (rwlock->last_rdlock_time_ns > 0 && rwlock->last_rdlock_time_ns <= now_ns) {
    char elapsed_str[64];
    format_elapsed(now_ns - rwlock->last_rdlock_time_ns, elapsed_str, sizeof(elapsed_str));
    snprintf(rdlock_str, sizeof(rdlock_str), "rdlock=%s", elapsed_str);
  }

  if (rwlock->last_wrlock_time_ns > 0 && rwlock->last_wrlock_time_ns <= now_ns) {
    char elapsed_str[64];
    format_elapsed(now_ns - rwlock->last_wrlock_time_ns, elapsed_str, sizeof(elapsed_str));
    snprintf(wrlock_str, sizeof(wrlock_str), "wrlock=%s", elapsed_str);
  }

  if (rwlock->last_unlock_time_ns > 0 && rwlock->last_unlock_time_ns <= now_ns) {
    char elapsed_str[64];
    format_elapsed(now_ns - rwlock->last_unlock_time_ns, elapsed_str, sizeof(elapsed_str));
    snprintf(unlock_str, sizeof(unlock_str), "unlock=%s", elapsed_str);
  }

  if (rwlock->write_held_by_key != 0) {
    snprintf(write_held_str, sizeof(write_held_str), "[WRITE_LOCKED_BY=0x%lx]",
             (unsigned long)rwlock->write_held_by_key);
  }

  if (rwlock->read_lock_count > 0) {
    snprintf(read_held_str, sizeof(read_held_str), "[READ_LOCKED=%" PRIu64 "]", rwlock->read_lock_count);
  }

  if (rwlock->write_held_by_key == 0 && rwlock->read_lock_count == 0) {
    snprintf(status_str, sizeof(status_str), "[FREE]");
  }

  offset += snprintf(buffer + offset, size - offset, "%s %s %s %s %s %s", rdlock_str, wrlock_str, unlock_str,
                     write_held_str, read_held_str, status_str);

  return offset;
}

/**
 * @brief Extract timing info from a cond_t as a single line
 * @param cond Pointer to cond_t
 * @param buffer Output buffer for formatted info
 * @param size Buffer size
 * @return Number of bytes written
 */
static int format_cond_timing(const cond_t *cond, char *buffer, size_t size) {
  if (!cond)
    return 0;

  // If cond was never waited on, return empty
  if (cond->last_wait_time_ns == 0) {
    return 0;
  }

  int offset = 0;
  uint64_t now_ns = time_get_ns();
  char wait_str[64] = "";
  char signal_str[64] = "";
  char broadcast_str[64] = "";
  char waiting_str[256] = "";
  char status_str[128] = "";

  if (cond->last_wait_time_ns > 0 && cond->last_wait_time_ns <= now_ns) {
    char elapsed_str[64];
    format_elapsed(now_ns - cond->last_wait_time_ns, elapsed_str, sizeof(elapsed_str));
    snprintf(wait_str, sizeof(wait_str), "wait=%s", elapsed_str);
  }

  if (cond->last_signal_time_ns > 0 && cond->last_signal_time_ns <= now_ns) {
    char elapsed_str[64];
    format_elapsed(now_ns - cond->last_signal_time_ns, elapsed_str, sizeof(elapsed_str));
    snprintf(signal_str, sizeof(signal_str), "signal=%s", elapsed_str);
  }

  if (cond->last_broadcast_time_ns > 0 && cond->last_broadcast_time_ns <= now_ns) {
    char elapsed_str[64];
    format_elapsed(now_ns - cond->last_broadcast_time_ns, elapsed_str, sizeof(elapsed_str));
    snprintf(broadcast_str, sizeof(broadcast_str), "broadcast=%s", elapsed_str);
  }

  if (cond->waiting_count > 0) {
    snprintf(waiting_str, sizeof(waiting_str), "[WAITING=%" PRIu64 " threads, last=0x%lx]", cond->waiting_count,
             (unsigned long)cond->last_waiting_key);
  } else {
    snprintf(status_str, sizeof(status_str), "[IDLE]");
  }

  offset += snprintf(buffer + offset, size - offset, "%s %s %s %s %s", wait_str, signal_str, broadcast_str, waiting_str,
                     status_str);

  return offset;
}

// ============================================================================
// Iterator Callbacks for named.c
// ============================================================================

typedef struct {
  char *buffer;
  size_t buffer_size;
  size_t offset;
} sync_buffer_t;

static void mutex_iter_callback(uintptr_t key, const char *name, void *user_data) {
  sync_buffer_t *buf = (sync_buffer_t *)user_data;
  if (!buf)
    return;

  const char *type = named_get_type(key);
  if (!type || strcmp(type, "mutex") != 0) {
    return;
  }

  const mutex_t *mutex = (const mutex_t *)key;
  char timing_str[256] = {0};
  format_mutex_timing(mutex, timing_str, sizeof(timing_str));

  // Only append if mutex has been used
  if (timing_str[0]) {
    buf->offset +=
        snprintf(buf->buffer + buf->offset, buf->buffer_size - buf->offset, "  Mutex %s: %s\n", name, timing_str);
  }
}

static void rwlock_iter_callback(uintptr_t key, const char *name, void *user_data) {
  sync_buffer_t *buf = (sync_buffer_t *)user_data;
  if (!buf)
    return;

  const char *type = named_get_type(key);
  if (!type || strcmp(type, "rwlock") != 0) {
    return;
  }

  const rwlock_t *rwlock = (const rwlock_t *)key;
  char timing_str[512] = {0};
  format_rwlock_timing(rwlock, timing_str, sizeof(timing_str));

  // Only append if rwlock has been used
  if (timing_str[0]) {
    buf->offset +=
        snprintf(buf->buffer + buf->offset, buf->buffer_size - buf->offset, "  RWLock %s: %s\n", name, timing_str);
  }
}

static void cond_iter_callback(uintptr_t key, const char *name, void *user_data) {
  sync_buffer_t *buf = (sync_buffer_t *)user_data;
  if (!buf)
    return;

  const char *type = named_get_type(key);
  if (!type || strcmp(type, "cond") != 0) {
    return;
  }

  const cond_t *cond = (const cond_t *)key;
  char timing_str[512] = {0};
  format_cond_timing(cond, timing_str, sizeof(timing_str));

  // Only append if condition variable has been used
  if (timing_str[0]) {
    buf->offset +=
        snprintf(buf->buffer + buf->offset, buf->buffer_size - buf->offset, "  Cond %s: %s\n", name, timing_str);
  }
}

// ============================================================================
// Lock Stack Printing
// ============================================================================

/**
 * @brief Print all thread lock stacks
 */
static void debug_sync_print_lock_stacks(char *buffer, size_t buffer_size, size_t *offset) {
  mutex_stack_entry_t **all_stacks = NULL;
  int *stack_counts = NULL;
  int thread_count = 0;

  if (mutex_stack_get_all_threads(&all_stacks, &stack_counts, &thread_count) != 0) {
    return;
  }

  if (thread_count == 0) {
    mutex_stack_free_all_threads(all_stacks, stack_counts, thread_count);
    return;
  }

  *offset += snprintf(buffer + *offset, buffer_size - *offset, "\nThread Lock Stacks:\n");

  for (int i = 0; i < thread_count; i++) {
    int depth = stack_counts[i];
    if (depth == 0)
      continue;

    *offset += snprintf(buffer + *offset, buffer_size - *offset, "  Thread %d: %d lock(s)\n", i, depth);

    for (int j = 0; j < depth; j++) {
      const mutex_stack_entry_t *entry = &all_stacks[i][j];
      const char *state_str = (entry->state == MUTEX_STACK_STATE_LOCKED) ? "LOCKED" : "PENDING";

      uint64_t elapsed = time_get_ns() - entry->timestamp_ns;
      char elapsed_str[64];
      time_pretty(elapsed, -1, elapsed_str, sizeof(elapsed_str));

      *offset += snprintf(buffer + *offset, buffer_size - *offset, "    [%d] %s @ %p (%s) %s", j, entry->mutex_name,
                          (void *)entry->mutex_key, state_str, elapsed_str);
    }
  }

  mutex_stack_free_all_threads(all_stacks, stack_counts, thread_count);
}

// ============================================================================
// Public API Implementation
// ============================================================================

void debug_sync_print_state(void) {
// Use a single large buffer for all sync state output
#define SYNC_BUFFER_SIZE 8192
  char *buffer = SAFE_MALLOC(SYNC_BUFFER_SIZE, char *);
  if (!buffer)
    return;

  sync_buffer_t buf = {.buffer = buffer, .buffer_size = SYNC_BUFFER_SIZE, .offset = 0};

  // Collect all sync state in one buffer
  buf.offset += snprintf(buf.buffer + buf.offset, buf.buffer_size - buf.offset, "Synchronization Primitive State:\n");

  // Iterate through all registered syncs
  named_registry_for_each(mutex_iter_callback, &buf);
  named_registry_for_each(rwlock_iter_callback, &buf);
  named_registry_for_each(cond_iter_callback, &buf);

  // Print lock stacks for deadlock analysis
  debug_sync_print_lock_stacks(buf.buffer, buf.buffer_size, &buf.offset);

  // Log everything in one call
  if (buf.offset > 0) {
    log_info("%s", buf.buffer);
  }

  SAFE_FREE(buffer);
#undef SYNC_BUFFER_SIZE
}

// ============================================================================
// Condition Variable Deadlock Detection
// ============================================================================

// ============================================================================
// Scheduled Debug State Printing (runs on separate thread)
// ============================================================================

typedef enum {
  DEBUG_REQUEST_STATE,         // Print sync state
  DEBUG_REQUEST_BACKTRACE,     // Print backtrace
  DEBUG_REQUEST_MEMORY_REPORT, // Print memory report
} debug_request_type_t;

typedef struct {
  debug_request_type_t request_type; // What to print
  uint64_t delay_ns;
  _Atomic(bool) should_run;            // Atomic flag set by main thread
  _Atomic(bool) should_exit;           // Atomic flag for shutdown
  _Atomic(bool) signal_triggered;      // Flag set by SIGUSR1 handler
  uint64_t memory_report_interval_ns;  // Interval for periodic memory reports (0 = disabled)
  uint64_t last_memory_report_time_ns; // Timestamp of last memory report
  mutex_t mutex;                       // Protects access to flags during locked operations
  cond_t cond;                         // Wakes thread when signal arrives
  bool initialized;                    // Tracks if mutex/cond are initialized
  bool handled_sync_state_time;        // Track if --sync-state option was already processed
  bool handled_backtrace_time;         // Track if --backtrace option was already processed
  bool handled_memory_report;          // Track if --memory-report option was already processed
} debug_state_request_t;

static debug_state_request_t g_debug_state_request = {
    .request_type = DEBUG_REQUEST_STATE,
    .delay_ns = 0,
    .should_run = false,
    .should_exit = false,
    .signal_triggered = false,
    .memory_report_interval_ns = 0,
    .last_memory_report_time_ns = 0,
    .initialized = false,
    .handled_sync_state_time = false,
    .handled_backtrace_time = false,
    .handled_memory_report = false,
};
static asciichat_thread_t g_debug_thread;
static uint64_t g_debug_main_thread_id = 0; // Main thread ID for memory reporting

/**
 * @brief Thread function for scheduled debug state printing
 *
 * Handles both delayed printing and signal-triggered printing (via SIGUSR1).
 * Logging is performed on this thread, not in signal handler context,
 * avoiding potential deadlocks with logging mutexes.
 *
 * Uses a condition variable to wake up immediately when SIGUSR1 is received,
 * without polling or busy-waiting.
 */
static void *debug_print_thread_fn(void *arg) {
  (void)arg;

  while (!atomic_load(&g_debug_state_request.should_exit)) {
    // Handle delayed printing
    if (atomic_load(&g_debug_state_request.should_run) && g_debug_state_request.delay_ns > 0) {
      platform_sleep_ns(g_debug_state_request.delay_ns);
      g_debug_state_request.delay_ns = 0;
    }

    // Handle both scheduled and signal-triggered printing
    mutex_lock(&g_debug_state_request.mutex);
    bool should_run = atomic_load(&g_debug_state_request.should_run);
    bool signal_triggered = atomic_load(&g_debug_state_request.signal_triggered);
    bool should_exit = atomic_load(&g_debug_state_request.should_exit);

    if ((should_run || signal_triggered) && !should_exit) {
      debug_request_type_t request_type = g_debug_state_request.request_type;
      mutex_unlock(&g_debug_state_request.mutex);

      // Print based on request type
      if (request_type == DEBUG_REQUEST_STATE) {
        debug_sync_print_state();
      } else if (request_type == DEBUG_REQUEST_BACKTRACE) {
        backtrace_t bt;
        backtrace_capture_and_symbolize(&bt);
        backtrace_print("Backtrace", &bt, 0, 0, NULL);
        backtrace_t_free(&bt);
      }

      mutex_lock(&g_debug_state_request.mutex);
      atomic_store(&g_debug_state_request.should_run, false);
      atomic_store(&g_debug_state_request.signal_triggered, false);
      should_exit = atomic_load(&g_debug_state_request.should_exit);
    }

    // Wait for work or signal, with 100ms timeout to check should_exit
    if (!should_exit) {
      cond_timedwait(&g_debug_state_request.cond, &g_debug_state_request.mutex, 100000000); // 100ms
    }
    mutex_unlock(&g_debug_state_request.mutex);

    // Periodic deadlock detection (runs every 100ms during wait timeout)
    debug_sync_check_cond_deadlocks();
    mutex_stack_detect_deadlocks();

#ifndef NDEBUG
    // Periodic memory report (if enabled)
    if (g_debug_state_request.memory_report_interval_ns > 0) {
      uint64_t now = time_get_ns();
      uint64_t last_time = g_debug_state_request.last_memory_report_time_ns;

      // Check if enough time has passed (or first report)
      if (last_time == 0 || (now - last_time >= g_debug_state_request.memory_report_interval_ns)) {
        debug_memory_report();
        g_debug_state_request.last_memory_report_time_ns = now;
      }
    }

    options_t *opts = options_get();
    if (!opts) {
      continue;
    }

    // Handle --debug-state (debug builds only)
    // Schedule sync state printing after specified delay (execute only once, when option is first detected)
    // Uses non-blocking scheduled printing instead of sleep to avoid blocking the debug thread
    if (!g_debug_state_request.handled_sync_state_time && IS_OPTION_EXPLICIT(debug_sync_state_time, opts) &&
        opts->debug_sync_state_time > 0.0) {
      g_debug_state_request.handled_sync_state_time = true;
      log_info("Will print sync state after %f seconds", opts->debug_sync_state_time);
      uint64_t delay_ns = (uint64_t)(opts->debug_sync_state_time * NS_PER_SEC_INT);
      debug_sync_print_state_delayed(delay_ns);
    }

    // Handle --backtrace (debug builds only)
    // Schedule backtrace printing after specified delay (execute only once, when option is first detected)
    // Uses non-blocking scheduled printing instead of sleep to avoid blocking the debug thread
    if (!g_debug_state_request.handled_backtrace_time && IS_OPTION_EXPLICIT(debug_backtrace_time, opts) &&
        opts->debug_backtrace_time > 0.0) {
      g_debug_state_request.handled_backtrace_time = true;
      log_info("Will print backtrace after %f seconds", opts->debug_backtrace_time);
      uint64_t delay_ns = (uint64_t)(opts->debug_backtrace_time * NS_PER_SEC_INT);
      debug_sync_print_backtrace_delayed(delay_ns);
    }

    // Handle --memory-report (debug builds only)
    // Enable periodic memory reporting at specified interval (execute only once, when option is first detected)
    if (!g_debug_state_request.handled_memory_report && IS_OPTION_EXPLICIT(debug_memory_report_interval, opts) &&
        opts->debug_memory_report_interval > 0.0) {
      g_debug_state_request.handled_memory_report = true;
      log_info("Enabling memory reports every %f seconds", opts->debug_memory_report_interval);
      uint64_t interval_ns = (uint64_t)(opts->debug_memory_report_interval * NS_PER_SEC_INT);
      debug_sync_set_memory_report_interval(interval_ns);
    }
#else
    options_t *opts = options_get();
    if (!opts) {
      continue;
    }
#endif
  }

  return NULL;
}

/**
 * @brief Final cleanup of all debug allocations at shutdown
 */
void debug_sync_final_cleanup(void) {
  // Clean up current thread's mutex stack explicitly
  mutex_stack_cleanup_current_thread();
}

/**
 * @brief Schedule delayed debug state printing on debug thread
 * @param delay_ns Nanoseconds to sleep before printing
 */
void debug_sync_print_state_delayed(uint64_t delay_ns) {
  g_debug_state_request.request_type = DEBUG_REQUEST_STATE;
  g_debug_state_request.delay_ns = delay_ns;
  atomic_store(&g_debug_state_request.should_run, true);
  cond_signal(&g_debug_state_request.cond);
}

/**
 * @brief Schedule delayed backtrace printing on debug thread
 * @param delay_ns Nanoseconds to sleep before printing
 */
void debug_sync_print_backtrace_delayed(uint64_t delay_ns) {
  g_debug_state_request.request_type = DEBUG_REQUEST_BACKTRACE;
  g_debug_state_request.delay_ns = delay_ns;
  atomic_store(&g_debug_state_request.should_run, true);
  cond_signal(&g_debug_state_request.cond);
}

/**
 * @brief Set periodic memory report interval
 * @param interval_ns Interval in nanoseconds (0 to disable)
 */
void debug_sync_set_memory_report_interval(uint64_t interval_ns) {
  g_debug_state_request.memory_report_interval_ns = interval_ns;
  g_debug_state_request.last_memory_report_time_ns = 0; // Reset timer
}

// ============================================================================
// Debug Sync API - Thread management
// ============================================================================

void debug_sync_set_main_thread_id(void) {
  // Save main thread ID for memory reporting (call very early)
  g_debug_main_thread_id = asciichat_thread_current_id();
}

int debug_sync_init(void) {
  // debug_sync_set_main_thread_id() should have already been called
  return 0;
}

uint64_t debug_sync_get_main_thread_id(void) {
  return g_debug_main_thread_id;
}

int debug_sync_start_thread(void) {
  // Initialize mutex and condition variable for signal wakeup
  if (!g_debug_state_request.initialized) {
    mutex_init(&g_debug_state_request.mutex, "debug_sync_state");
    cond_init(&g_debug_state_request.cond, "debug_sync_signal");
    g_debug_state_request.initialized = true;
  }

  g_debug_state_request.should_exit = false;
  int err = asciichat_thread_create(&g_debug_thread, "debug_sync", debug_print_thread_fn, NULL);
  return err;
}

void debug_sync_destroy(void) {
  debug_sync_cleanup_thread();
}

void debug_sync_cleanup_thread(void) {
  log_debug("[DEBUG_SYNC_CLEANUP] Starting cleanup");

  // Only join if thread was actually created
  if (!g_debug_state_request.initialized) {
    log_debug("[DEBUG_SYNC_CLEANUP] Thread not initialized, returning");
    return;
  }
  log_debug("[DEBUG_SYNC_CLEANUP] Thread was initialized, proceeding with cleanup");

  log_debug("[DEBUG_SYNC_CLEANUP] Setting initialized to false");
  g_debug_state_request.initialized = false; // Prevent double-join

  // Signal the thread to wake up immediately instead of waiting for 100ms timeout
  log_debug("[DEBUG_SYNC_CLEANUP] Signaling thread to exit");
  atomic_store(&g_debug_state_request.should_exit, true);
  cond_signal(&g_debug_state_request.cond);
  log_debug("[DEBUG_SYNC_CLEANUP] Signal sent, about to join thread");

  // Use a timeout join to ensure we don't deadlock, but still unregister the thread
  // The debug thread should exit quickly after should_exit is set above
  int join_result = asciichat_thread_join_timeout(&g_debug_thread, NULL, 1000000000ULL); // 1 second timeout
  if (join_result == 0) {
    log_debug("[DEBUG_SYNC_CLEANUP] Thread joined successfully");
  } else if (join_result == -2) {
    log_debug("[DEBUG_SYNC_CLEANUP] Thread join timed out (thread may still be running)");
    // Don't unregister if timeout - thread is still alive
  } else {
    log_debug("[DEBUG_SYNC_CLEANUP] Thread join failed with error %d", join_result);
  }
}

void debug_sync_trigger_print(void) {
  // Set flag to trigger printing on debug thread (from SIGUSR1 handler).
  // We don't call debug_sync_print_state() directly here to avoid logging
  // in signal handler context, which could deadlock with logging mutexes.
  // Uses atomic_store for thread-safe flag setting from signal handler.
  //
  // Signal the condition variable to wake up the debug thread immediately
  // (without waiting for the 100ms timeout).
  atomic_store(&g_debug_state_request.signal_triggered, true);
  cond_signal(&g_debug_state_request.cond);
}

void debug_sync_get_stats(uint64_t *total_acquired, uint64_t *total_released, uint32_t *currently_held) {
  if (total_acquired)
    *total_acquired = 0;
  if (total_released)
    *total_released = 0;
  if (currently_held)
    *currently_held = 0;
}

// ============================================================================
// Debug Lock Operation Stubs - pass-through to implementations
// ============================================================================

int debug_sync_mutex_lock(mutex_t *mutex, const char *file_name, int line_number, const char *function_name) {
  (void)file_name;
  (void)line_number;
  (void)function_name;
  return mutex_lock_impl(mutex);
}

int debug_sync_mutex_trylock(mutex_t *mutex, const char *file_name, int line_number, const char *function_name) {
  (void)file_name;
  (void)line_number;
  (void)function_name;
  return mutex_trylock_impl(mutex);
}

int debug_sync_mutex_unlock(mutex_t *mutex, const char *file_name, int line_number, const char *function_name) {
  (void)file_name;
  (void)line_number;
  (void)function_name;
  return mutex_unlock_impl(mutex);
}

int debug_sync_rwlock_rdlock(rwlock_t *lock, const char *file_name, int line_number, const char *function_name) {
  (void)file_name;
  (void)line_number;
  (void)function_name;
  return rwlock_rdlock_impl(lock);
}

int debug_sync_rwlock_rdunlock(rwlock_t *lock, const char *file_name, int line_number, const char *function_name) {
  (void)file_name;
  (void)line_number;
  (void)function_name;
  return rwlock_rdunlock_impl(lock);
}

int debug_sync_rwlock_wrlock(rwlock_t *lock, const char *file_name, int line_number, const char *function_name) {
  (void)file_name;
  (void)line_number;
  (void)function_name;
  return rwlock_wrlock_impl(lock);
}

int debug_sync_rwlock_wrunlock(rwlock_t *lock, const char *file_name, int line_number, const char *function_name) {
  (void)file_name;
  (void)line_number;
  (void)function_name;
  return rwlock_wrunlock_impl(lock);
}

int debug_sync_cond_wait(cond_t *cond, mutex_t *mutex, const char *file_name, int line_number,
                         const char *function_name) {
  // Note: pthread_cond_wait() atomically releases and re-acquires the mutex,
  // but this happens at the kernel level. Debug tracking can't monitor this atomic
  // operation properly, so we skip cond_on_wait() to avoid false deadlock reports.
  (void)file_name;
  (void)line_number;
  (void)function_name;
  return cond_wait_impl(cond, mutex);
}

int debug_sync_cond_timedwait(cond_t *cond, mutex_t *mutex, uint64_t timeout_ns, const char *file_name, int line_number,
                              const char *function_name) {
  // Same as debug_sync_cond_wait(): skip tracking the atomic unlock/relock.
  (void)file_name;
  (void)line_number;
  (void)function_name;
  return cond_timedwait_impl(cond, mutex, timeout_ns);
}

int debug_sync_cond_signal(cond_t *cond, const char *file_name, int line_number, const char *function_name) {
  (void)file_name;
  (void)line_number;
  (void)function_name;
  return cond_signal(cond);
}

int debug_sync_cond_broadcast(cond_t *cond, const char *file_name, int line_number, const char *function_name) {
  (void)file_name;
  (void)line_number;
  (void)function_name;
  return cond_broadcast(cond);
}

bool debug_sync_is_initialized(void) {
  return true;
}
