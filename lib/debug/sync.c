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
#include <ascii-chat/debug/debug_helpers.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/debug/backtrace.h>
#include <ascii-chat/debug/mutex.h>
#include <ascii-chat/debug/atomic.h>
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
#include <ascii-chat/atomic.h>

// ============================================================================
// Helper Functions (Debug builds only)
// ============================================================================

#ifndef NDEBUG

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
    char thread_name[256];
    NAMED_GET_BY_PTR(mutex->currently_held_by_key, thread_name, sizeof(thread_name));
    snprintf(held_str, sizeof(held_str), "[LOCKED_BY=%s]", thread_name);
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
    char thread_name[256];
    NAMED_GET_BY_PTR(rwlock->write_held_by_key, thread_name, sizeof(thread_name));
    snprintf(write_held_str, sizeof(write_held_str), "[WRITE_LOCKED_BY=%s]", thread_name);
  }

  uint64_t read_count = atomic_load_u64(&rwlock->read_lock_count);
  if (read_count > 0) {
    snprintf(read_held_str, sizeof(read_held_str), "[READ_LOCKED=%" PRIu64 "]", read_count);
  }

  if (rwlock->write_held_by_key == 0 && read_count == 0) {
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

  uint64_t waiting_count = atomic_load_u64(&cond->waiting_count);
  if (waiting_count > 0) {
    char thread_name[256];
    NAMED_GET_BY_PTR(cond->last_waiting_key, thread_name, sizeof(thread_name));
    snprintf(waiting_str, sizeof(waiting_str), "[WAITING=%" PRIu64 " threads, last=%s]", waiting_count, thread_name);
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

  if (!key) {
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

  if (!key) {
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

static void atomic_t_iter_callback(uintptr_t key, const char *name, void *user_data) {
  sync_buffer_t *buf = (sync_buffer_t *)user_data;
  if (!buf)
    return;

  const char *type = named_get_type(key);
  if (!type || strcmp(type, "atomic") != 0) {
    return;
  }

  const atomic_t *atomic = (const atomic_t *)key;
  char timing_str[512] = {0};
  int bytes = debug_atomic_format_timing(atomic, timing_str, sizeof(timing_str));

  // Only append if atomic has been used
  if (bytes > 0) {
    buf->offset +=
        snprintf(buf->buffer + buf->offset, buf->buffer_size - buf->offset, "  Atomic %s: %s\n", name, timing_str);
  }
}

static void atomic_ptr_iter_callback(uintptr_t key, const char *name, void *user_data) {
  sync_buffer_t *buf = (sync_buffer_t *)user_data;
  if (!buf)
    return;

  const char *type = named_get_type(key);
  if (!type || strcmp(type, "atomic_ptr") != 0) {
    return;
  }

  const atomic_ptr_t *atomic = (const atomic_ptr_t *)key;
  char timing_str[512] = {0};
  int bytes = debug_atomic_ptr_format_timing(atomic, timing_str, sizeof(timing_str));

  // Only append if atomic has been used
  if (bytes > 0) {
    buf->offset +=
        snprintf(buf->buffer + buf->offset, buf->buffer_size - buf->offset, "  AtomicPtr %s: %s\n", name, timing_str);
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

      *offset += snprintf(buffer + *offset, buffer_size - *offset, "    [%d] mutex @ %p (%s) %s", j,
                          (void *)entry->mutex_key, state_str, elapsed_str);
    }
  }

  mutex_stack_free_all_threads(all_stacks, stack_counts, thread_count);
}

// ============================================================================
// Public API Implementation (Debug builds only)
// ============================================================================

void debug_sync_print_state(void) {
// Use a single large buffer for all sync state output
#define SYNC_BUFFER_SIZE 65536 // Increased from 8192 to handle many syncs
  log_debug("[debug_sync_print_state] ENTRY");

  char *buffer = SAFE_MALLOC(SYNC_BUFFER_SIZE, char *);
  if (!buffer) {
    log_debug("[debug_sync_print_state] Failed to allocate buffer");
    return;
  }

  sync_buffer_t buf = {.buffer = buffer, .buffer_size = SYNC_BUFFER_SIZE, .offset = 0};

  // Iterate through all registered syncs
  log_debug("[debug_sync_print_state] Iterating mutexes");
  named_registry_for_each(mutex_iter_callback, &buf);
  log_debug("[debug_sync_print_state] Iterating rwlocks");
  named_registry_for_each(rwlock_iter_callback, &buf);
  log_debug("[debug_sync_print_state] Iterating conds");
  named_registry_for_each(cond_iter_callback, &buf);

  named_registry_for_each(atomic_t_iter_callback, &buf);
  named_registry_for_each(atomic_ptr_iter_callback, &buf);

  // Print lock stacks for deadlock analysis
  log_debug("[debug_sync_print_state] Getting lock stacks");
  debug_sync_print_lock_stacks(buf.buffer, buf.buffer_size, &buf.offset);

  // Log everything in one call
  log_debug("[debug_sync_print_state] Buffer size: %zu bytes", buf.offset);
  if (buf.offset > 0) {
    // Emit bounded lines so the logger does not truncate a large registry report.
    log_info("SYNC_STATE:");
    char *line = buf.buffer;
    while (*line) {
      char *end = strchr(line, '\n');
      if (end)
        *end = '\0';
      log_info("%s", line);
      if (!end)
        break;
      line = end + 1;
    }
  } else {
    log_info("SYNC_STATE: (empty)");
  }

  SAFE_FREE(buffer);
#undef SYNC_BUFFER_SIZE
}

void debug_sync_get_stats(uint64_t *total_acquired, uint64_t *total_released, uint32_t *currently_held) {
  if (total_acquired)
    *total_acquired = 0;
  if (total_released)
    *total_released = 0;
  if (currently_held)
    *currently_held = 0;
}

#endif // NDEBUG (close debug-only code section)

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

// ============================================================================
// Release Build Stubs (NDEBUG)
// ============================================================================

#ifdef NDEBUG

void debug_sync_print_state(void) {
  // No-op in release builds
}


#endif


static atomic_t g_signal = {0};
static atomic_t g_exiting = {0};
static atomic_t g_cleaning = {0};
static asciichat_thread_t g_thread;
static mutex_t g_mutex;
static cond_t g_condition;
static bool g_initialized;
static bool g_started;
static uint64_t g_state_deadline;
static uint64_t g_backtrace_deadline;

asciichat_error_t debug_sync_init(void) {
  if (g_initialized)
    return ASCIICHAT_OK;
  if (mutex_init(&g_mutex, "debug_sync") != 0)
    return SET_ERRNO(ERROR_THREAD, "Cannot initialize diagnostics mutex");
  if (cond_init(&g_condition, "debug_sync") != 0) {
    mutex_destroy(&g_mutex);
    return SET_ERRNO(ERROR_THREAD, "Cannot initialize diagnostics condition");
  }
  g_initialized = true;
  atomic_store_bool_impl(&g_cleaning, false);
  return ASCIICHAT_OK;
}
static void debug_sync_print_sync(void) {
#ifndef NDEBUG
  debug_sync_print_state();
  named_print_hash_stats();
#else
  log_info("SYNC_STATE: synchronization diagnostics are unavailable in Release builds");
#endif
}
void debug_sync_print(void) {
  debug_sync_print_sync();
}
void debug_sync_print_state_delayed(uint64_t delay) {
  if (!g_initialized)
    return;
  debug_report_schedule(&g_mutex, &g_condition, &g_state_deadline, delay);
}
void debug_sync_print_backtrace_delayed(uint64_t delay) {
  if (!g_initialized)
    return;
  debug_report_schedule(&g_mutex, &g_condition, &g_backtrace_deadline, delay);
}
void debug_sync_trigger_print(void) {
  // Do not signal a condition variable or invoke diagnostic hooks in a signal
  // handler. The worker checks this flag at most 100ms after its next wakeup.
  atomic_store_bool_impl(&g_signal, true);
}
bool debug_sync_is_cleanup_in_progress(void) {
  return atomic_load_bool_impl(&g_cleaning);
}
void debug_sync_poll(void) {
  if (!g_initialized)
    return;
  uint64_t now = time_get_ns();
  bool all = atomic_exchange_bool_impl(&g_signal, false);
  bool state = false;
  mutex_lock(&g_mutex);
  bool backtrace = g_backtrace_deadline && now >= g_backtrace_deadline;
  if (g_state_deadline && now >= g_state_deadline) {
    state = true;
    g_state_deadline = 0;
  }
  if (backtrace)
    g_backtrace_deadline = 0;
  mutex_unlock(&g_mutex);
  if (all || state)
    debug_sync_print_sync();
  if (backtrace) {
    backtrace_t trace = {0};
    backtrace_capture(&trace);
    backtrace_symbolize(&trace);
    if (trace.symbols)
      backtrace_print("Diagnostics worker backtrace", &trace, 0, 0, NULL);
    backtrace_t_free(&trace);
  }
}
#ifndef EMSCRIPTEN_BUILD
static void *debug_sync_worker(void *unused) {
  (void)unused;
  while (!atomic_load_bool_impl(&g_exiting)) {
    debug_sync_poll();
#ifndef NDEBUG
    if (!atomic_load_bool_impl(&g_exiting)) {
      debug_sync_check_cond_deadlocks();
      mutex_stack_detect_deadlocks();
    }
#endif
    mutex_lock(&g_mutex);
    uint64_t now = time_get_ns();
    uint64_t wait = 100 * NS_PER_MS_INT;
    uint64_t deadlines[] = {g_state_deadline, g_backtrace_deadline};
    for (size_t i = 0; i < sizeof(deadlines) / sizeof(deadlines[0]); ++i)
      if (deadlines[i]) {
        uint64_t remaining = deadlines[i] > now ? deadlines[i] - now : 1;
        if (remaining < wait)
          wait = remaining;
      }
    if (!atomic_load_bool_impl(&g_exiting))
      cond_timedwait(&g_condition, &g_mutex, wait);
    mutex_unlock(&g_mutex);
  }
  return NULL;
}
#endif
asciichat_error_t debug_sync_start_thread(void) {
  if (g_started)
    return ASCIICHAT_OK;
  asciichat_error_t result = debug_sync_init();
  if (result != ASCIICHAT_OK)
    return result;
  atomic_store_bool_impl(&g_exiting, false);
  atomic_store_bool_impl(&g_cleaning, false);
  options_t *opts = options_get();
  if (opts) {
    if (IS_OPTION_EXPLICIT(debug_sync_state_time, opts))
      debug_sync_print_state_delayed((uint64_t)(opts->debug_sync_state_time * NS_PER_SEC_INT));
    if (IS_OPTION_EXPLICIT(debug_backtrace_time, opts))
      debug_sync_print_backtrace_delayed((uint64_t)(opts->debug_backtrace_time * NS_PER_SEC_INT));
  }
#ifndef EMSCRIPTEN_BUILD
  if (asciichat_thread_create(&g_thread, "debug_sync", debug_sync_worker, NULL) != 0)
    return SET_ERRNO(ERROR_THREAD, "Cannot start diagnostics worker");
#endif
  g_started = true;
  return ASCIICHAT_OK;
}
void debug_sync_cleanup_thread(void) {
  if (!g_started)
    return;
  atomic_store_bool_impl(&g_cleaning, true);
  mutex_lock(&g_mutex);
  atomic_store_bool_impl(&g_exiting, true);
  cond_signal(&g_condition);
  mutex_unlock(&g_mutex);
#ifndef EMSCRIPTEN_BUILD
  // Join fully: diagnostic data must not be destroyed while a reader is alive.
  if (asciichat_thread_join(&g_thread, NULL) != 0)
    FATAL(ERROR_THREAD, "Cannot join diagnostics worker");
#endif
  g_started = false;
}
void debug_sync_destroy(void) {
  debug_sync_cleanup_thread();
  if (g_initialized) {
    cond_destroy(&g_condition);
    mutex_destroy(&g_mutex);
    g_initialized = false;
    atomic_store_bool_impl(&g_signal, false);
    g_state_deadline = g_backtrace_deadline = 0;
  }
}
void debug_sync_final_cleanup(void) {
#ifndef NDEBUG
  mutex_stack_cleanup_current_thread();
#endif
}
