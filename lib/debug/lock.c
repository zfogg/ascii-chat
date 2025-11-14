/**
 * @file lock.c
 * @ingroup lock_debug
 * @brief 🔒 Lock debugging and deadlock detection with call stack backtraces and lock ordering validation
 * @date September 2025
 */

// Header must be included even in release builds to get inline no-op stubs
#include "debug/lock.h"

#ifndef NDEBUG
// Only compile lock_debug implementation in debug builds
// In release builds, lock_debug.h provides inline no-op stubs

#include "common.h"
#include "platform/abstraction.h"
#include "util/fnv1a.h"
#include "util/time.h"
#include "util/uthash.h"
#include <stdlib.h>
#include <time.h>
#include <stdatomic.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <conio.h>
#else
#include <sys/select.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#endif

// ============================================================================
// Constants
// ============================================================================

/**
 * @brief Lock hold time threshold in milliseconds
 *
 * Locks held longer than this will trigger a warning with backtrace.
 * For ascii-chat with 60 FPS video (16.67ms/frame) and 172 FPS audio (5.8ms/frame),
 * 100ms is a reasonable threshold - any lock held this long could cause frame drops.
 */
#define LOCK_HOLD_TIME_WARNING_MS 500

/**
 * @brief Safe buffer size calculation for snprintf
 *
 * Casts offset to size_t to avoid sign conversion warnings when subtracting from buffer_size.
 * Returns 0 if offset is negative or >= buffer_size (prevents underflow).
 */
#define SAFE_BUFFER_SIZE(buffer_size, offset)                                                                          \
  ((offset) < 0 || (size_t)(offset) >= (buffer_size) ? 0 : (buffer_size) - (size_t)(offset))

// ============================================================================
// Global State
// ============================================================================

lock_debug_manager_t g_lock_debug_manager = {0};
atomic_bool g_initializing = false; // Flag to prevent tracking during initialization

#ifndef _WIN32
// Terminal state for POSIX systems to enable raw mode input
static struct termios g_original_termios;
static bool g_termios_saved = false;
#endif

/**
 * @brief Create a new lock record with backtrace
 * @param lock_address Address of the lock object
 * @param lock_type Type of lock
 * @param file_name Source file name
 * @param line_number Source line number
 * @param function_name Function name
 * @return Pointer to new lock record or NULL on failure
 */
static lock_record_t *create_lock_record(void *lock_address, lock_type_t lock_type, const char *file_name,
                                         int line_number, const char *function_name) {
  lock_record_t *record = SAFE_CALLOC(1, sizeof(lock_record_t), lock_record_t *);

  // Fill in basic information
  record->key = lock_record_key(lock_address, lock_type);
  record->lock_address = lock_address;
  record->lock_type = lock_type;
  record->thread_id = ascii_thread_current_id();
  record->file_name = file_name;
  record->line_number = line_number;
  record->function_name = function_name;

  // Get current time
  if (clock_gettime(CLOCK_MONOTONIC, &record->acquisition_time) != 0) {
    SET_ERRNO(ERROR_PLATFORM_INIT, "Failed to get acquisition time");
    SAFE_FREE(record);
    return NULL;
  }

  // Capture backtrace
  // NOTE: platform_backtrace_symbols_free() safely handles the case where
  // backtrace_symbols() uses system malloc() but mimalloc overrides free()
  // by detecting our enhanced format and not freeing system-allocated memory
  record->backtrace_size = platform_backtrace(record->backtrace_buffer, MAX_BACKTRACE_FRAMES);
  if (record->backtrace_size > 0) {
    record->backtrace_symbols = platform_backtrace_symbols(record->backtrace_buffer, record->backtrace_size);
    if (!record->backtrace_symbols) {
      static bool symbolize_error_logged = false;
      if (!symbolize_error_logged) {
        log_warn("Failed to symbolize backtrace for lock record (backtrace support may be unavailable)");
        symbolize_error_logged = true;
      }
    }
  } else {
    // Backtrace unavailable (e.g., no libexecinfo)
    // Only log this once to avoid spam
    static bool backtrace_error_logged = false;
    if (!backtrace_error_logged) {
      log_debug("Backtrace not available for lock debugging");
      backtrace_error_logged = true;
    }
  }

  return record;
}

/**
 * @brief Free a lock record and its associated memory
 * @param record Lock record to free
 */
static void free_lock_record(lock_record_t *record) {
  if (!record) {
    SET_ERRNO(ERROR_INVALID_STATE, "Freeing NULL lock record");
    return;
  }

  // Free symbolized backtrace
  if (record->backtrace_symbols) {
    platform_backtrace_symbols_free(record->backtrace_symbols);
  }

  SAFE_FREE(record);
}

// ============================================================================
// Callback Functions
// ============================================================================

/**
 * @brief Callback function for collecting lock records into a buffer
 * @param record Lock record pointer
 * @param user_data Pointer to lock collector structure
 */
static void collect_lock_record_callback(lock_record_t *record, void *user_data) {
  struct lock_collector {
    uint32_t count;
    char *buffer;
    int *offset;
  } *collector = (struct lock_collector *)user_data;

  collector->count++;
  int *offset = collector->offset;
  char *buffer = collector->buffer;
  size_t buffer_size = 32768; // Match the buffer size in lock_debug_print_state

  // Print lock information
  const char *lock_type_str;
  switch (record->lock_type) {
  case LOCK_TYPE_MUTEX:
    lock_type_str = "MUTEX";
    break;
  case LOCK_TYPE_RWLOCK_READ:
    lock_type_str = "RWLOCK_READ";
    break;
  case LOCK_TYPE_RWLOCK_WRITE:
    lock_type_str = "RWLOCK_WRITE";
    break;
  default:
    lock_type_str = "UNKNOWN";
    break;
  }

  *offset += snprintf(buffer + *offset, SAFE_BUFFER_SIZE(buffer_size, *offset), "Lock #%u: %s at %p\n",
                      collector->count, lock_type_str, record->lock_address);
  *offset += snprintf(buffer + *offset, SAFE_BUFFER_SIZE(buffer_size, *offset), "  Thread ID: %llu\n",
                      (unsigned long long)record->thread_id);
  *offset += snprintf(buffer + *offset, SAFE_BUFFER_SIZE(buffer_size, *offset), "  Acquired: %s:%d in %s()\n",
                      record->file_name, record->line_number, record->function_name);

  // Calculate how long the lock has been held
  struct timespec current_time;
  if (clock_gettime(CLOCK_MONOTONIC, &current_time) == 0) {
    long long held_sec = current_time.tv_sec - record->acquisition_time.tv_sec;
    long held_nsec = current_time.tv_nsec - record->acquisition_time.tv_nsec;

    // Handle nanosecond underflow
    if (held_nsec < 0) {
      held_sec--;
      held_nsec += 1000000000;
    }

    *offset += snprintf(buffer + *offset, SAFE_BUFFER_SIZE(buffer_size, *offset), "  Held for: %lld.%09ld seconds\n",
                        held_sec, held_nsec);
  } else {
    *offset += snprintf(buffer + *offset, SAFE_BUFFER_SIZE(buffer_size, *offset),
                        "  Acquired at: %lld.%09ld seconds (monotonic)\n", (long long)record->acquisition_time.tv_sec,
                        record->acquisition_time.tv_nsec);
  }

  // Print backtrace using platform symbol resolution
  if (record->backtrace_size > 0) {
    *offset += snprintf(buffer + *offset, SAFE_BUFFER_SIZE(buffer_size, *offset), "  Call stack (%d frames):\n",
                        record->backtrace_size);

    // Use platform backtrace symbols for proper symbol resolution
    char **symbols = platform_backtrace_symbols(record->backtrace_buffer, record->backtrace_size);

    // Check if we got useful symbols (not just addresses)
    bool has_symbols = false;
    if (symbols && symbols[0]) {
      // Check if symbol contains more than just the address
      char addr_str[32];
      (void)snprintf(addr_str, sizeof(addr_str), "%p", record->backtrace_buffer[0]);
      has_symbols = (strstr(symbols[0], "(") != NULL) || (strstr(symbols[0], "+") != NULL);
    }

    for (int j = 0; j < record->backtrace_size; j++) {
      // Build the full line for each stack frame
      if (symbols && symbols[j] && has_symbols) {
        *offset += snprintf(buffer + *offset, SAFE_BUFFER_SIZE(buffer_size, *offset), "    %2d: %s\n", j, symbols[j]);
      } else {
        *offset += snprintf(buffer + *offset, SAFE_BUFFER_SIZE(buffer_size, *offset), "    %2d: %p\n", j,
                            record->backtrace_buffer[j]);
      }
    }

    // For static binaries, provide helpful message
    if (!has_symbols) {
      *offset += snprintf(buffer + *offset, SAFE_BUFFER_SIZE(buffer_size, *offset),
                          "  Resolve symbols with: addr2line -e <binary> -f -C <addresses>\n");
    }

    // Clean up symbols
    if (symbols) {
      platform_backtrace_symbols_free(symbols);
    }
  } else {
    *offset += snprintf(buffer + *offset, SAFE_BUFFER_SIZE(buffer_size, *offset), "  Call stack: <capture failed>\n");
  }
}

/**
 * @brief Callback function for cleaning up lock records
 * @param record Lock record pointer
 * @param user_data Pointer to counter for number of records cleaned up
 */
static void cleanup_lock_record_callback(lock_record_t *record, void *user_data) {
  uint32_t *count = (uint32_t *)user_data;

  (*count)++;
  free_lock_record(record);
}

/**
 * @brief Callback function for printing usage statistics
 * @param stats Usage statistics pointer
 * @param user_data Pointer to total_stats counter
 */
static void print_usage_stats_callback(lock_usage_stats_t *stats, void *user_data) {
  uint32_t *count = (uint32_t *)user_data;
  (*count)++;

  // Get lock type string
  const char *lock_type_str;
  switch (stats->lock_type) {
  case LOCK_TYPE_MUTEX:
    lock_type_str = "MUTEX";
    break;
  case LOCK_TYPE_RWLOCK_READ:
    lock_type_str = "RWLOCK_READ";
    break;
  case LOCK_TYPE_RWLOCK_WRITE:
    lock_type_str = "RWLOCK_WRITE";
    break;
  default:
    lock_type_str = "UNKNOWN";
    break;
  }

  // Calculate average hold time
  uint64_t avg_hold_time_ns = stats->total_hold_time_ns / stats->total_acquisitions;

  // Format all information into a single log message with newlines
  char log_message[1024];
  int offset = 0;

  offset += safe_snprintf(log_message + offset, SAFE_BUFFER_SIZE(sizeof(log_message), offset),
                          "Usage #%u: %s at %s:%d in %s()\n", *count, lock_type_str, stats->file_name,
                          stats->line_number, stats->function_name);
  offset += safe_snprintf(log_message + offset, SAFE_BUFFER_SIZE(sizeof(log_message), offset),
                          "  Total acquisitions: %llu\n", (unsigned long long)stats->total_acquisitions);
  offset +=
      safe_snprintf(log_message + offset, SAFE_BUFFER_SIZE(sizeof(log_message), offset),
                    "  Total hold time: %llu.%03llu ms\n", (unsigned long long)(stats->total_hold_time_ns / 1000000),
                    (unsigned long long)((stats->total_hold_time_ns % 1000000) / 1000));
  offset += safe_snprintf(log_message + offset, SAFE_BUFFER_SIZE(sizeof(log_message), offset),
                          "  Average hold time: %llu.%03llu ms\n", (unsigned long long)(avg_hold_time_ns / 1000000),
                          (unsigned long long)((avg_hold_time_ns % 1000000) / 1000));
  offset += safe_snprintf(log_message + offset, SAFE_BUFFER_SIZE(sizeof(log_message), offset),
                          "  Max hold time: %llu.%03llu ms\n", (unsigned long long)(stats->max_hold_time_ns / 1000000),
                          (unsigned long long)((stats->max_hold_time_ns % 1000000) / 1000));
  offset += safe_snprintf(log_message + offset, SAFE_BUFFER_SIZE(sizeof(log_message), offset),
                          "  Min hold time: %llu.%03llu ms\n", (unsigned long long)(stats->min_hold_time_ns / 1000000),
                          (unsigned long long)((stats->min_hold_time_ns % 1000000) / 1000));
  offset += safe_snprintf(log_message + offset, SAFE_BUFFER_SIZE(sizeof(log_message), offset),
                          "  First acquisition: %lld.%09ld\n", (long long)stats->first_acquisition.tv_sec,
                          stats->first_acquisition.tv_nsec);
  offset += safe_snprintf(log_message + offset, SAFE_BUFFER_SIZE(sizeof(log_message), offset),
                          "  Last acquisition: %lld.%09ld", (long long)stats->last_acquisition.tv_sec,
                          stats->last_acquisition.tv_nsec);

  log_info("%s", log_message);
}

/**
 * @brief Callback function for cleaning up usage statistics
 * @param stats Usage statistics pointer
 * @param user_data Unused
 */
static void cleanup_usage_stats_callback(lock_usage_stats_t *stats, void *user_data) {
  UNUSED(user_data);
  SAFE_FREE(stats);
}

/**
 * @brief Callback function for printing orphaned releases
 * @param record Orphaned release record pointer
 * @param user_data Pointer to total_orphans counter
 */
void print_orphaned_release_callback(lock_record_t *record, void *user_data) {
  uint32_t *count = (uint32_t *)user_data;
  (*count)++;

  // Print lock information
  const char *lock_type_str;
  switch (record->lock_type) {
  case LOCK_TYPE_MUTEX:
    lock_type_str = "MUTEX";
    break;
  case LOCK_TYPE_RWLOCK_READ:
    lock_type_str = "RWLOCK_READ";
    break;
  case LOCK_TYPE_RWLOCK_WRITE:
    lock_type_str = "RWLOCK_WRITE";
    break;
  default:
    lock_type_str = "UNKNOWN";
    break;
  }

  log_info("Orphaned Release #%u: %s at %p", *count, lock_type_str, record->lock_address);
  log_info("  Thread ID: %llu", (unsigned long long)record->thread_id);
  log_info("  Released: %s:%d in %s()", record->file_name, record->line_number, record->function_name);
  log_info("  Released at: %lld.%09ld seconds (monotonic)", (long long)record->acquisition_time.tv_sec,
           record->acquisition_time.tv_nsec);

  // Print backtrace for the orphaned release
  if (record->backtrace_size > 0) {
    log_info("  Release call stack (%d frames):", record->backtrace_size);

    // Use platform backtrace symbols for proper symbol resolution
    char **symbols = platform_backtrace_symbols(record->backtrace_buffer, record->backtrace_size);

    for (int j = 0; j < record->backtrace_size; j++) {
      // Build the full line for each stack frame
      if (symbols && symbols[j]) {
        log_info("    %2d: %p %s", j, record->backtrace_buffer[j], symbols[j]);
      } else {
        log_info("    %2d: %p <unresolved>", j, record->backtrace_buffer[j]);
      }
    }

    // Clean up symbols
    if (symbols) {
      platform_backtrace_symbols_free(symbols);
    }
  } else {
    log_info("  Release call stack: <capture failed>");
  }
}

// ============================================================================
// Debug Thread Functions
// ============================================================================

/**
 * @brief Check for locks held longer than the warning threshold and log warnings
 *
 * This function iterates through all currently held locks and logs warnings
 * for any locks held longer than LOCK_HOLD_TIME_WARNING_MS (100ms).
 * Only logs locks that exceed the threshold - no logging for locks held <= 100ms.
 *
 * Called periodically from the debug thread to monitor lock hold times.
 */
static void check_long_held_locks(void) {
  if (!atomic_load(&g_lock_debug_manager.initialized)) {
    return;
  }

  // Acquire read lock for lock_records
  rwlock_rdlock_impl(&g_lock_debug_manager.lock_records_lock);

  struct timespec current_time;
  if (clock_gettime(CLOCK_MONOTONIC, &current_time) != 0) {
    rwlock_rdunlock_impl(&g_lock_debug_manager.lock_records_lock);
    return;
  }

  // Iterate through all currently held locks
  bool found_long_held_lock = false;
  lock_record_t *entry, *tmp;
  HASH_ITER(hash_handle, g_lock_debug_manager.lock_records, entry, tmp) {
    // Calculate how long the lock has been held in nanoseconds
    long long held_sec = current_time.tv_sec - entry->acquisition_time.tv_sec;
    long held_nsec = current_time.tv_nsec - entry->acquisition_time.tv_nsec;

    // Handle nanosecond underflow
    if (held_nsec < 0) {
      held_sec--;
      held_nsec += 1000000000;
    }

    // Convert to nanoseconds (double for format_duration_ns)
    double held_ns = (held_sec * 1000000000.0) + held_nsec;

    // Only log if held longer than 100ms (100000000 nanoseconds)
    const double WARNING_THRESHOLD_NS = 100000000.0; // 100ms in nanoseconds
    if (held_ns > WARNING_THRESHOLD_NS) {
      found_long_held_lock = true;

      // Get lock type string
      const char *lock_type_str;
      switch (entry->lock_type) {
      case LOCK_TYPE_MUTEX:
        lock_type_str = "MUTEX";
        break;
      case LOCK_TYPE_RWLOCK_READ:
        lock_type_str = "RWLOCK_READ";
        break;
      case LOCK_TYPE_RWLOCK_WRITE:
        lock_type_str = "RWLOCK_WRITE";
        break;
      default:
        lock_type_str = "UNKNOWN";
        break;
      }

      // Format duration like STOP_TIMER_AND_LOG does
      char duration_str[32];
      format_duration_ns(held_ns, duration_str, sizeof(duration_str));

      // Log warning with formatted duration
      log_warn_every(1000000,
                     "Lock held for %s (threshold: 100ms) - %s at %p\n"
                     "  Acquired: %s:%d in %s()\n"
                     "  Thread ID: %llu",
                     duration_str, lock_type_str, entry->lock_address, entry->file_name, entry->line_number,
                     entry->function_name, (unsigned long long)entry->thread_id);
    }
  }

  rwlock_rdunlock_impl(&g_lock_debug_manager.lock_records_lock);

  // Print backtrace only once if any long-held locks were found
  if (found_long_held_lock) {
    platform_print_backtrace(1);
  }
}

/**
 * @brief Debug thread function - monitors for lock print requests and keyboard input
 * @param arg Thread argument (unused)
 * @return Thread return value
 */
static void *debug_thread_func(void *arg) {
  UNUSED(arg);

#ifndef _WIN32
  // Set terminal to raw mode for immediate key detection
  struct termios raw;
  if (tcgetattr(STDIN_FILENO, &g_original_termios) == 0) {
    g_termios_saved = true;
    raw = g_original_termios;
    raw.c_lflag &= ~((tcflag_t)(ICANON | ECHO)); // Disable canonical mode and echo
    raw.c_cc[VMIN] = 0;                          // Non-blocking read
    raw.c_cc[VTIME] = 0;                         // No timeout
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
      log_warn("Failed to set terminal to raw mode for lock debug");
      g_termios_saved = false;
    }
  } else {
    log_warn("Failed to get terminal attributes for lock debug");
  }
#endif

  log_debug("Lock debug thread started - press '?' to print lock state");

  while (atomic_load(&g_lock_debug_manager.debug_thread_running)) {
    // Check for locks held > 100ms and log warnings
    check_long_held_locks();

    // Allow external trigger via flag (non-blocking)
    if (atomic_exchange(&g_lock_debug_manager.should_print_locks, false)) {
      lock_debug_print_state();
    }

    // Check for keyboard input first
#ifdef _WIN32
    if (_kbhit()) {
      int ch = _getch();
      if (ch == '?') {
        lock_debug_print_state();
      }
    }

    // Small sleep to prevent CPU spinning
    platform_sleep_ms(10);
#else
    // POSIX: use select() for non-blocking input (now in raw mode)
    fd_set readfds;
    struct timeval timeout;

    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000; // 100ms timeout

    int result = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);
    if (result > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
      char input[2];
      if (read(STDIN_FILENO, input, 1) == 1) {
        if (input[0] == '?') {
          lock_debug_print_state();
        }
      }
    }
#endif

    platform_sleep_ms(100);
  }

#ifndef _WIN32
  // Restore terminal to original mode
  if (g_termios_saved) {
    tcsetattr(STDIN_FILENO, TCSANOW, &g_original_termios);
    g_termios_saved = false;
  }
#endif

  // Thread exiting
  return NULL;
}

// ============================================================================
// Public API Implementation
// ============================================================================

int lock_debug_init(void) {
  log_info("Starting lock debug system initialization...");

  if (atomic_load(&g_lock_debug_manager.initialized)) {
    log_info("Lock debug system already initialized");
    return 0; // Already initialized
  }

  // Set initialization flag to prevent tracking during init
  atomic_store(&g_initializing, true);

  // Initialize uthash hash tables to NULL (required)
  g_lock_debug_manager.lock_records = NULL;
  g_lock_debug_manager.usage_stats = NULL;
  g_lock_debug_manager.orphaned_releases = NULL;

  // Initialize rwlocks for thread safety (uthash requires external locking)
  if (rwlock_init(&g_lock_debug_manager.lock_records_lock) != 0) {
    atomic_store(&g_initializing, false);
    SET_ERRNO(ERROR_THREAD, "Failed to initialize lock_records rwlock");
    return -1;
  }

  if (rwlock_init(&g_lock_debug_manager.usage_stats_lock) != 0) {
    rwlock_destroy(&g_lock_debug_manager.lock_records_lock);
    atomic_store(&g_initializing, false);
    SET_ERRNO(ERROR_THREAD, "Failed to initialize usage_stats rwlock");
    return -1;
  }

  if (rwlock_init(&g_lock_debug_manager.orphaned_releases_lock) != 0) {
    rwlock_destroy(&g_lock_debug_manager.lock_records_lock);
    rwlock_destroy(&g_lock_debug_manager.usage_stats_lock);
    atomic_store(&g_initializing, false);
    SET_ERRNO(ERROR_THREAD, "Failed to initialize orphaned_releases rwlock");
    return -1;
  }

  // Initialize atomic variables
  atomic_store(&g_lock_debug_manager.total_locks_acquired, 0);
  atomic_store(&g_lock_debug_manager.total_locks_released, 0);
  atomic_store(&g_lock_debug_manager.current_locks_held, 0);
  atomic_store(&g_lock_debug_manager.debug_thread_running, false);
  atomic_store(&g_lock_debug_manager.debug_thread_created, false);
  atomic_store(&g_lock_debug_manager.should_print_locks, false);

  // Initialize thread handle to invalid value
#ifdef _WIN32
  g_lock_debug_manager.debug_thread = NULL;
#else
  // On POSIX, pthread_t doesn't have a standard "invalid" value
  // but we'll rely on the debug_thread_running flag
#endif

  // Clear initialization flag FIRST, then mark as initialized
  // This prevents race condition where initialized=true but initializing=true
  atomic_store(&g_initializing, false);
  atomic_store(&g_lock_debug_manager.initialized, true);

  // Note: lock_debug_cleanup() will be called during normal shutdown sequence
  // and lock_debug_cleanup_thread() will be called as one of the last things before exit

  // log_info("Lock debug system initialized with uthash");
  return 0;
}

int lock_debug_start_thread(void) {
  if (!atomic_load(&g_lock_debug_manager.initialized)) {
    return -1;
  }

  if (atomic_load(&g_lock_debug_manager.debug_thread_running)) {
    return 0; // Already running
  }

  atomic_store(&g_lock_debug_manager.debug_thread_running, true);

  int thread_result = ascii_thread_create(&g_lock_debug_manager.debug_thread, debug_thread_func, NULL);

  if (thread_result != 0) {
    SET_ERRNO(ERROR_THREAD, "Failed to create lock debug thread: %d", thread_result);
    atomic_store(&g_lock_debug_manager.debug_thread_running, false);
    atomic_store(&g_lock_debug_manager.debug_thread_created, false);
    return -1;
  }

  // Thread was successfully created
  atomic_store(&g_lock_debug_manager.debug_thread_created, true);
  return 0;
}

void lock_debug_trigger_print(void) {
  if (atomic_load(&g_lock_debug_manager.initialized)) {
    atomic_store(&g_lock_debug_manager.should_print_locks, true);
  }
}

void lock_debug_cleanup(void) {
  // Use atomic exchange to ensure cleanup only runs once
  // This prevents double-cleanup from both atexit() and manual calls
  bool was_initialized = atomic_exchange(&g_lock_debug_manager.initialized, false);
  if (!was_initialized) {
    return;
  }

  // Signal debug thread to stop but don't join it yet
  // Thread joining will happen later in lock_debug_cleanup_thread()
  if (atomic_load(&g_lock_debug_manager.debug_thread_running)) {
    atomic_store(&g_lock_debug_manager.debug_thread_running, false);
  }

  // Clean up all remaining lock records
  rwlock_wrlock_impl(&g_lock_debug_manager.lock_records_lock);
  // Free all lock records using HASH_ITER
  uint32_t lock_records_cleaned = 0;
  lock_record_t *entry, *tmp;
  HASH_ITER(hash_handle, g_lock_debug_manager.lock_records, entry, tmp) {
    HASH_DELETE(hash_handle, g_lock_debug_manager.lock_records, entry);
    cleanup_lock_record_callback(entry, &lock_records_cleaned);
  }
  if (lock_records_cleaned > 0) {
    log_info("Cleaned up %u lock records", lock_records_cleaned);
  }

  rwlock_wrunlock_impl(&g_lock_debug_manager.lock_records_lock);
  rwlock_destroy(&g_lock_debug_manager.lock_records_lock);
  g_lock_debug_manager.lock_records = NULL;

  // Clean up usage statistics
  rwlock_wrlock_impl(&g_lock_debug_manager.usage_stats_lock);
  // Free all usage statistics using HASH_ITER
  lock_usage_stats_t *stats_entry, *stats_tmp;
  HASH_ITER(hash_handle, g_lock_debug_manager.usage_stats, stats_entry, stats_tmp) {
    HASH_DELETE(hash_handle, g_lock_debug_manager.usage_stats, stats_entry);
    cleanup_usage_stats_callback(stats_entry, NULL);
  }

  rwlock_wrunlock_impl(&g_lock_debug_manager.usage_stats_lock);
  rwlock_destroy(&g_lock_debug_manager.usage_stats_lock);
  g_lock_debug_manager.usage_stats = NULL;

  // Clean up orphaned releases
  rwlock_wrlock_impl(&g_lock_debug_manager.orphaned_releases_lock);

  // Free all orphaned release records using HASH_ITER
  uint32_t orphaned_releases_cleaned = 0;
  lock_record_t *orphan_entry, *orphan_tmp;
  HASH_ITER(hash_handle, g_lock_debug_manager.orphaned_releases, orphan_entry, orphan_tmp) {
    HASH_DELETE(hash_handle, g_lock_debug_manager.orphaned_releases, orphan_entry);
    cleanup_lock_record_callback(orphan_entry, &orphaned_releases_cleaned);
  }
  if (orphaned_releases_cleaned > 0) {
    log_info("Cleaned up %u orphaned release records", orphaned_releases_cleaned);
  }

  rwlock_wrunlock_impl(&g_lock_debug_manager.orphaned_releases_lock);
  rwlock_destroy(&g_lock_debug_manager.orphaned_releases_lock);
  g_lock_debug_manager.orphaned_releases = NULL;

  // initialized flag already set to false at the beginning via atomic_exchange
  log_info("Lock debug system cleaned up");
}

void lock_debug_cleanup_thread(void) {
  // Check if thread is/was running and join it
  if (atomic_load(&g_lock_debug_manager.debug_thread_running)) {
    atomic_store(&g_lock_debug_manager.debug_thread_running, false);
  }

#ifdef _WIN32
  // On Windows, check if thread handle is valid before joining
  if (g_lock_debug_manager.debug_thread != NULL) {
    int join_result = ascii_thread_join(&g_lock_debug_manager.debug_thread, NULL);
    if (join_result == 0) {
      // Thread handle is now NULL due to cleanup in ascii_thread_join
    } else {
      // Force cleanup if join failed
      g_lock_debug_manager.debug_thread = NULL;
    }
  }
#else
  // On POSIX, only attempt join if thread was actually created
  // Use the debug_thread_created flag to reliably track if the thread exists
  if (atomic_load(&g_lock_debug_manager.debug_thread_created)) {
    ascii_thread_join(&g_lock_debug_manager.debug_thread, NULL);
    // Clear the flag after joining
    atomic_store(&g_lock_debug_manager.debug_thread_created, false);
  }

  // Restore terminal to original mode if it was changed
  if (g_termios_saved) {
    tcsetattr(STDIN_FILENO, TCSANOW, &g_original_termios);
    g_termios_saved = false;
  }
#endif
}

// ============================================================================
// Common Helper Functions
// ============================================================================

/**
 * @brief Common validation and filtering logic for all debug lock functions
 * @param lock_ptr Pointer to lock object
 * @param file_name Source file name
 * @param function_name Function name
 * @return true if tracking should be skipped, false if tracking should proceed
 */
static bool debug_should_skip_lock_tracking(void *lock_ptr, const char *file_name, const char *function_name) {
  if (!lock_ptr || !file_name || !function_name) {
    return true;
  }

  // Skip tracking if system is not fully initialized or during initialization/shutdown
  bool initialized = atomic_load(&g_lock_debug_manager.initialized);
  bool initializing = atomic_load(&g_initializing);
  bool should_exit = shutdown_is_requested();

  if (!initialized || initializing || should_exit) {
    return true;
  }

  // Filter out ALL functions that our lock debug system uses internally
  // to prevent infinite recursion
  // Note: uthash uses macros (HASH_FIND_INT, etc.) so there are no function names to filter
  if (strstr(function_name, "log_") != NULL || strstr(function_name, "platform_") != NULL ||
      strstr(function_name, "create_lock_record") != NULL || strstr(function_name, "update_usage_stats") != NULL ||
      strstr(function_name, "print_") != NULL || strstr(function_name, "debug_") != NULL ||
      strstr(function_name, "lock_debug") != NULL || strstr(file_name, "symbols.c") != NULL ||
      strstr(function_name, "ascii_thread") != NULL) {
    return true;
  }

  return false;
}

/**
 * @brief Common logic for decrementing lock counters with underflow protection
 * @return The new held count after decrement
 */
static uint32_t debug_decrement_lock_counter(void) {
  // Decrement using atomic fetch_sub which avoids type size issues
  uint_fast32_t current = atomic_load(&g_lock_debug_manager.current_locks_held);
  if (current > 0) {
    uint_fast32_t prev = atomic_fetch_sub(&g_lock_debug_manager.current_locks_held, 1);
    // If prev was already 0, we underflowed - add it back
    if (prev == 0) {
      atomic_fetch_add(&g_lock_debug_manager.current_locks_held, 1);
      return 0;
    }
    return (uint32_t)(prev - 1);
  }
  return 0;
}

/**
 * @brief Common logic for creating and inserting lock records
 * @param lock_address Address of the lock object
 * @param lock_type Type of lock (MUTEX, RWLOCK_READ, RWLOCK_WRITE)
 * @param lock_type_str String description of lock type
 * @param file_name Source file name
 * @param line_number Source line number
 * @param function_name Function name
 * @return true if record was created and inserted successfully
 */
static bool debug_create_and_insert_lock_record(void *lock_address, lock_type_t lock_type, const char *lock_type_str,
                                                const char *file_name, int line_number, const char *function_name) {
#ifndef DEBUG_LOCKS
  UNUSED(lock_address);
  UNUSED(lock_type);
  UNUSED(lock_type_str);
  UNUSED(file_name);
  UNUSED(line_number);
  UNUSED(function_name);
#endif

  lock_record_t *record = create_lock_record(lock_address, lock_type, file_name, line_number, function_name);
  if (record) {
    // Acquire write lock to make the entire operation atomic
    rwlock_wrlock_impl(&g_lock_debug_manager.lock_records_lock);

    // Double-check cache is still initialized after acquiring lock
    if (!atomic_load(&g_lock_debug_manager.initialized)) {
      rwlock_wrunlock_impl(&g_lock_debug_manager.lock_records_lock);
      free_lock_record(record);
      return false;
    }

    // Check if entry already exists
    lock_record_t *existing = NULL;
    HASH_FIND(hash_handle, g_lock_debug_manager.lock_records, &record->key, sizeof(record->key), existing);

    if (existing) {
      // Entry exists - this shouldn't happen, but handle it gracefully
      rwlock_wrunlock_impl(&g_lock_debug_manager.lock_records_lock);
      free_lock_record(record);
      return false;
    }

    // Add to hash table
    HASH_ADD(hash_handle, g_lock_debug_manager.lock_records, key, sizeof(record->key), record);

    // Release lock
    rwlock_wrunlock_impl(&g_lock_debug_manager.lock_records_lock);

    atomic_fetch_add(&g_lock_debug_manager.total_locks_acquired, 1);
    atomic_fetch_add(&g_lock_debug_manager.current_locks_held, 1);
    return true;
  }
  // Record allocation failed
  return false;
}

/**
 * @brief Common logic for processing tracked rwlock unlock
 * @param rwlock Pointer to rwlock
 * @param key Lock record key
 * @param lock_type_str String description of lock type ("READ" or "WRITE")
 * @param file_name Source file name
 * @param line_number Source line number
 * @param function_name Function name
 * @return true if record was found and removed, false otherwise
 */
static bool debug_process_tracked_unlock(void *lock_ptr, uint32_t key, const char *lock_type_str, const char *file_name,
                                         int line_number, const char *function_name) {
#ifndef DEBUG_LOCKS
  UNUSED(lock_ptr);
  UNUSED(lock_type_str);
  UNUSED(file_name);
  UNUSED(line_number);
  UNUSED(function_name);
#endif

  // Acquire write lock for removal
  rwlock_wrlock_impl(&g_lock_debug_manager.lock_records_lock);

  lock_record_t *record = NULL;
  HASH_FIND(hash_handle, g_lock_debug_manager.lock_records, &key, sizeof(key), record);
  if (record) {
    // Calculate lock hold time BEFORE removing record
    struct timespec current_time;
    long long held_ms = 0;
    if (clock_gettime(CLOCK_MONOTONIC, &current_time) == 0) {
      long long held_sec = current_time.tv_sec - record->acquisition_time.tv_sec;
      long held_nsec = current_time.tv_nsec - record->acquisition_time.tv_nsec;

      // Handle nanosecond underflow
      if (held_nsec < 0) {
        held_sec--;
        held_nsec += 1000000000;
      }

      held_ms = (held_sec * 1000) + (held_nsec / 1000000);

      // Check if lock was held too long
      if (held_ms > LOCK_HOLD_TIME_WARNING_MS) {
        char duration_str[32];
        format_duration_ms((double)held_ms, duration_str, sizeof(duration_str));
        log_warn("Lock held for %s (threshold: %d ms) at %s:%d in %s()\n"
                 "  Lock type: %s, address: %p",
                 duration_str, LOCK_HOLD_TIME_WARNING_MS, file_name, line_number, function_name, lock_type_str,
                 lock_ptr);

        // Print backtrace from when lock was acquired
        if (record->backtrace_size > 0 && record->backtrace_symbols) {
          char backtrace_str[1024];
          int offset = 0;
          for (int i = 0; i < record->backtrace_size && i < 10; i++) { // Limit to first 10 frames
            offset += snprintf(backtrace_str + offset, sizeof(backtrace_str) - offset, "    #%d: %s\n", i,
                               record->backtrace_symbols[i]);
          }
          log_warn("Backtrace from lock acquisition:\n%s", backtrace_str);
        } else {
          // No backtrace available, print current backtrace
          log_warn("No backtrace available. Current backtrace:");
          platform_print_backtrace(2); // Skip 2 frames (this function and debug_process_tracked_unlock)
        }
      }
    }

    HASH_DELETE(hash_handle, g_lock_debug_manager.lock_records, record);
    rwlock_wrunlock_impl(&g_lock_debug_manager.lock_records_lock);

    free_lock_record(record);
    atomic_fetch_add(&g_lock_debug_manager.total_locks_released, 1);
    debug_decrement_lock_counter();
    return true;
  }

  rwlock_wrunlock_impl(&g_lock_debug_manager.lock_records_lock);
  return false;
}

/**
 * @brief Common logic for processing untracked rwlock unlock
 * @param lock_ptr Pointer to lock
 * @param key Lock record key
 * @param lock_type_str String description of lock type ("READ" or "WRITE")
 * @param file_name Source file name
 * @param line_number Source line number
 * @param function_name Function name
 */
static void debug_process_untracked_unlock(void *lock_ptr, uint32_t key, const char *lock_type_str,
                                           const char *file_name, int line_number, const char *function_name) {
#ifdef DEBUG_LOCKS
  uint64_t released = atomic_fetch_add(&g_lock_debug_manager.total_locks_released, 1) + 1;
#else
  atomic_fetch_add(&g_lock_debug_manager.total_locks_released, 1);
#endif
  uint32_t current_held = atomic_load(&g_lock_debug_manager.current_locks_held);
#ifdef DEBUG_LOCKS
  uint32_t held = 0;
#endif
  if (current_held > 0) {
#ifdef DEBUG_LOCKS
    held = debug_decrement_lock_counter();
#else
    debug_decrement_lock_counter();
#endif
  } else {
#ifdef DEBUG_LOCKS
    SET_ERRNO(ERROR_INVALID_STATE, "Attempting to release %s lock when no locks held!", lock_type_str);
#endif
    SET_ERRNO(ERROR_INVALID_STATE, "Attempting to release %s lock when no locks held!", lock_type_str);
  }
#ifdef DEBUG_LOCKS
  SET_ERRNO(ERROR_INVALID_STATE, "%s UNTRACKED RELEASED: %p (key=%u) at %s:%d in %s() - total=%llu, held=%u",
            lock_type_str, lock_ptr, key, file_name, line_number, function_name, (unsigned long long)released, held);
#endif
#ifdef DEBUG_LOCKS
  SET_ERRNO(ERROR_INVALID_STATE, "*** WARNING: %s lock was acquired and tracked but record was lost! ***",
            lock_type_str);
#endif

  // Create an orphaned release record to track this problematic unlock
  lock_record_t *orphan_record = SAFE_CALLOC(1, sizeof(lock_record_t), lock_record_t *);
  if (orphan_record) {
    orphan_record->key = key;
    orphan_record->lock_address = lock_ptr;
    if (strcmp(lock_type_str, "MUTEX") == 0) {
      orphan_record->lock_type = LOCK_TYPE_MUTEX;
    } else if (strcmp(lock_type_str, "READ") == 0) {
      orphan_record->lock_type = LOCK_TYPE_RWLOCK_READ;
    } else if (strcmp(lock_type_str, "WRITE") == 0) {
      orphan_record->lock_type = LOCK_TYPE_RWLOCK_WRITE;
    }
    orphan_record->thread_id = ascii_thread_current_id();
    orphan_record->file_name = file_name;
    orphan_record->line_number = line_number;
    orphan_record->function_name = function_name;
    (void)clock_gettime(CLOCK_MONOTONIC, &orphan_record->acquisition_time); // Use release time

    // Capture backtrace for this orphaned release
#ifdef _WIN32
    orphan_record->backtrace_size =
        CaptureStackBackTrace(1, MAX_BACKTRACE_FRAMES, orphan_record->backtrace_buffer, NULL);
#else
    orphan_record->backtrace_size = platform_backtrace(orphan_record->backtrace_buffer, MAX_BACKTRACE_FRAMES);
#endif

    if (orphan_record->backtrace_size > 0) {
      orphan_record->backtrace_symbols =
          platform_backtrace_symbols(orphan_record->backtrace_buffer, orphan_record->backtrace_size);
    }

    // Store in orphaned releases hash table for later analysis
    rwlock_wrlock_impl(&g_lock_debug_manager.orphaned_releases_lock);
    HASH_ADD(hash_handle, g_lock_debug_manager.orphaned_releases, key, sizeof(orphan_record->key), orphan_record);
    rwlock_wrunlock_impl(&g_lock_debug_manager.orphaned_releases_lock);
  }
}

// ============================================================================
// Tracked Lock Functions Implementation
// ============================================================================

int debug_mutex_lock(mutex_t *mutex, const char *file_name, int line_number, const char *function_name) {
  if (debug_should_skip_lock_tracking(mutex, file_name, function_name)) {
    return mutex_lock_impl(mutex);
  }

  // Acquire the actual lock first (call implementation to avoid recursion)
  int result = mutex_lock_impl(mutex);
  if (result != 0) {
    return result;
  }

  // Create and add lock record
  debug_create_and_insert_lock_record(mutex, LOCK_TYPE_MUTEX, "MUTEX", file_name, line_number, function_name);

  return 0;
}

int debug_mutex_unlock(mutex_t *mutex, const char *file_name, int line_number, const char *function_name) {
  if (debug_should_skip_lock_tracking(mutex, file_name, function_name)) {
    return mutex_unlock_impl(mutex);
  }

  // Look for mutex lock record specifically
  uint32_t key = lock_record_key(mutex, LOCK_TYPE_MUTEX);
  if (!debug_process_tracked_unlock(mutex, key, "MUTEX", file_name, line_number, function_name)) {
    // No record found - check if this is because the lock was filtered or because of a tracking error
    uint32_t current_held = atomic_load(&g_lock_debug_manager.current_locks_held);

    if (current_held > 0) {
      // We have tracked locks but can't find this specific one - this is a tracking error
      debug_process_untracked_unlock(mutex, key, "MUTEX", file_name, line_number, function_name);
    } else {
      // No tracked locks - this means the lock was filtered during lock operation
    }
  }

  // Unlock the actual mutex (call implementation to avoid recursion)
  return mutex_unlock_impl(mutex);
}

int debug_rwlock_rdlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name) {
  if (debug_should_skip_lock_tracking(rwlock, file_name, function_name)) {
    return rwlock_rdlock_impl(rwlock);
  }

  // Acquire the actual lock first (call implementation to avoid recursion)
  int result = rwlock_rdlock_impl(rwlock);
  if (result != 0) {
    return result;
  }

  // Create and add lock record
  debug_create_and_insert_lock_record(rwlock, LOCK_TYPE_RWLOCK_READ, "RWLOCK READ", file_name, line_number,
                                      function_name);

  return 0;
}

int debug_rwlock_wrlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name) {
  if (debug_should_skip_lock_tracking(rwlock, file_name, function_name)) {
    return rwlock_wrlock_impl(rwlock);
  }

  // Acquire the actual lock first (call implementation to avoid recursion)
  int result = rwlock_wrlock_impl(rwlock);
  if (result != 0) {
    return result;
  }

  // Create and add lock record
  debug_create_and_insert_lock_record(rwlock, LOCK_TYPE_RWLOCK_WRITE, "RWLOCK WRITE", file_name, line_number,
                                      function_name);

  return 0;
}

int debug_rwlock_rdunlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name) {
  if (debug_should_skip_lock_tracking(rwlock, file_name, function_name)) {
    return rwlock_rdunlock_impl(rwlock);
  }

  // Look for read lock record specifically
  uint32_t read_key = lock_record_key(rwlock, LOCK_TYPE_RWLOCK_READ);

  if (!debug_process_tracked_unlock(rwlock, read_key, "READ", file_name, line_number, function_name)) {
    debug_process_untracked_unlock(rwlock, read_key, "READ", file_name, line_number, function_name);
  }

  return rwlock_rdunlock_impl(rwlock);
}

int debug_rwlock_wrunlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name) {
  if (debug_should_skip_lock_tracking(rwlock, file_name, function_name)) {
    return rwlock_wrunlock_impl(rwlock);
  }

  // Look for write lock record specifically
  uint32_t write_key = lock_record_key(rwlock, LOCK_TYPE_RWLOCK_WRITE);
  if (!debug_process_tracked_unlock(rwlock, write_key, "WRITE", file_name, line_number, function_name)) {
    debug_process_untracked_unlock(rwlock, write_key, "WRITE", file_name, line_number, function_name);
  }

  return rwlock_wrunlock_impl(rwlock);
}

// Removed debug_rwlock_unlock - generic unlock is ambiguous and problematic
// Use debug_rwlock_rdunlock or debug_rwlock_wrunlock instead

// ============================================================================
// Statistics Functions
// ============================================================================

void lock_debug_get_stats(uint64_t *total_acquired, uint64_t *total_released, uint32_t *currently_held) {
  if (total_acquired) {
    *total_acquired = atomic_load(&g_lock_debug_manager.total_locks_acquired);
  }
  if (total_released) {
    *total_released = atomic_load(&g_lock_debug_manager.total_locks_released);
  }
  if (currently_held) {
    *currently_held = atomic_load(&g_lock_debug_manager.current_locks_held);
  }
}

bool lock_debug_is_initialized(void) {
  bool initialized = atomic_load(&g_lock_debug_manager.initialized);
  bool initializing = atomic_load(&g_initializing);
  bool result = initialized && !initializing;

  return result;
}

void lock_debug_print_state(void) {
  if (!atomic_load(&g_lock_debug_manager.initialized)) {
    log_warn("Lock debug system not initialized.");
    return;
  }

  // Acquire read lock for lock_records
  rwlock_rdlock_impl(&g_lock_debug_manager.lock_records_lock);

  // Read counters atomically while holding the lock to ensure consistency with lock records
  uint64_t total_acquired = atomic_load(&g_lock_debug_manager.total_locks_acquired);
  uint64_t total_released = atomic_load(&g_lock_debug_manager.total_locks_released);
  uint32_t currently_held = atomic_load(&g_lock_debug_manager.current_locks_held);

  // Build comprehensive log message with all information
  char log_buffer[32768]; // Large buffer for lock details
  int offset = 0;

  // Collect lock information during iteration
  struct lock_collector {
    uint32_t count;
    char *buffer;
    int *offset;
  } lock_collector = {0, log_buffer, &offset};

  lock_record_t *entry, *tmp;
  HASH_ITER(hash_handle, g_lock_debug_manager.lock_records, entry, tmp) {
    collect_lock_record_callback(entry, &lock_collector);
  }
  uint32_t active_locks = lock_collector.count;

  rwlock_rdunlock_impl(&g_lock_debug_manager.lock_records_lock);

  // Header
  offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset), "=== LOCK DEBUG STATE ===\n");

  // Historical Statistics
  offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset), "Historical Statistics:\n");
  offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                     "  Total locks acquired: %llu\n", (unsigned long long)total_acquired);
  offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                     "  Total locks released: %llu\n", (unsigned long long)total_released);
  offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset), "  Currently held: %u\n",
                     currently_held);

  // Check for underflow before subtraction to avoid UB
  if (total_acquired >= total_released) {
    offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                       "  Net locks (acquired - released): %lld\n", (long long)(total_acquired - total_released));
  } else {
    // This shouldn't happen - means more releases than acquires
    offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                       "  *** ERROR: More releases (%llu) than acquires (%llu)! Difference: -%lld ***\n",
                       (unsigned long long)total_released, (unsigned long long)total_acquired,
                       (long long)(total_released - total_acquired));
    offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                       "  *** This indicates lock tracking was not enabled for some acquires ***\n");
  }

  // Currently Active Locks
  offset +=
      snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset), "\n=== Currently Active Locks ===\n");
  if (active_locks == 0) {
    offset +=
        snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset), "  No locks currently held.\n");
    // Check for consistency issues
    if (currently_held > 0) {
      offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                         "  *** CONSISTENCY WARNING: Counter shows %u held locks but no records found! ***\n",
                         currently_held);
      offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                         "  *** This may indicate a crash during lock acquisition or hash table corruption. ***\n");

      // Additional debug: Check hash table statistics
      offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                         "  *** DEBUG: Hash table stats for lock_records: ***\n");
      rwlock_rdlock_impl(&g_lock_debug_manager.lock_records_lock);
      size_t count = HASH_CNT(hash_handle, g_lock_debug_manager.lock_records);
      rwlock_rdunlock_impl(&g_lock_debug_manager.lock_records_lock);
      offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                         "  *** Hash table size: %zu ***\n", count);
      if (count > 0) {
        offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                           "  *** Hash table has entries but iteration didn't find them! ***\n");
      }
    }
  } else {
    offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset), "  Active locks: %u\n",
                       active_locks);
    // Verify consistency the other way
    if (active_locks != currently_held) {
      offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                         "  *** CONSISTENCY WARNING: Found %u active locks but counter shows %u! ***\n", active_locks,
                         currently_held);
    }

    // The lock details are already in the buffer from collect_lock_record_callback
    // No need to add them again here
  }

  // Print usage statistics by code location
  offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                     "\n=== Lock Usage Statistics by Code Location ===\n");
  rwlock_rdlock_impl(&g_lock_debug_manager.usage_stats_lock);

  uint32_t total_usage_locations = 0;
  lock_usage_stats_t *stats_entry, *stats_tmp;
  HASH_ITER(hash_handle, g_lock_debug_manager.usage_stats, stats_entry, stats_tmp) {
    print_usage_stats_callback(stats_entry, &total_usage_locations);
  }

  rwlock_rdunlock_impl(&g_lock_debug_manager.usage_stats_lock);

  if (total_usage_locations == 0) {
    offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                       "  No lock usage statistics available.\n");
  } else {
    offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                       "  Total code locations with lock usage: %u\n", total_usage_locations);
  }

  // Print orphaned releases (unlocks without corresponding locks)
  offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                     "\n=== Orphaned Releases (unlocks without corresponding locks) ===\n");
  rwlock_rdlock_impl(&g_lock_debug_manager.orphaned_releases_lock);

  uint32_t orphan_count = 0;
  lock_record_t *orphan_entry, *orphan_tmp;
  HASH_ITER(hash_handle, g_lock_debug_manager.orphaned_releases, orphan_entry, orphan_tmp) {
    orphan_count++;

    // Get lock type string
    const char *lock_type_str;
    switch (orphan_entry->lock_type) {
    case LOCK_TYPE_MUTEX:
      lock_type_str = "MUTEX";
      break;
    case LOCK_TYPE_RWLOCK_READ:
      lock_type_str = "RWLOCK_READ";
      break;
    case LOCK_TYPE_RWLOCK_WRITE:
      lock_type_str = "RWLOCK_WRITE";
      break;
    default:
      lock_type_str = "UNKNOWN";
      break;
    }

    offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                       "Orphaned Release #%u: %s at %p\n", orphan_count, lock_type_str, orphan_entry->lock_address);
    offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset), "  Thread ID: %llu\n",
                       (unsigned long long)orphan_entry->thread_id);
    offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset), "  Released: %s:%d in %s()\n",
                       orphan_entry->file_name, orphan_entry->line_number, orphan_entry->function_name);
    offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                       "  Released at: %lld.%09ld seconds (monotonic)\n",
                       (long long)orphan_entry->acquisition_time.tv_sec, orphan_entry->acquisition_time.tv_nsec);

    // Print backtrace for orphaned release
    if (orphan_entry->backtrace_size > 0) {
      offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                         "  Release call stack (%d frames):\n", orphan_entry->backtrace_size);

      char **symbols = platform_backtrace_symbols(orphan_entry->backtrace_buffer, orphan_entry->backtrace_size);

      bool has_symbols = false;
      if (symbols && symbols[0]) {
        char addr_str[32];
        (void)snprintf(addr_str, sizeof(addr_str), "%p", orphan_entry->backtrace_buffer[0]);
        has_symbols = (strstr(symbols[0], "(") != NULL) || (strstr(symbols[0], "+") != NULL);
      }

      for (int j = 0; j < orphan_entry->backtrace_size; j++) {
        if (symbols && symbols[j] && has_symbols) {
          offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset), "    %2d: %s\n", j,
                             symbols[j]);
        } else {
          offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset), "    %2d: %p\n", j,
                             orphan_entry->backtrace_buffer[j]);
        }
      }

      if (!has_symbols) {
        offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                           "  Resolve symbols with: addr2line -e <binary> -f -C <addresses>\n");
      }

      if (symbols) {
        platform_backtrace_symbols_free(symbols);
      }
    } else {
      offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                         "  Release call stack: <capture failed>\n");
    }
  }

  rwlock_rdunlock_impl(&g_lock_debug_manager.orphaned_releases_lock);

  if (orphan_count == 0) {
    offset +=
        snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset), "  No orphaned releases found.\n");
  } else {
    offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                       "  Total orphaned releases: %u\n", orphan_count);
    offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                       "  *** WARNING: %u releases without corresponding locks detected! ***\n", orphan_count);
    offset += snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                       "  *** This indicates double unlocks or missing lock acquisitions! ***\n");
  }

  // End marker
  offset +=
      snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset), "\n=== End Lock Debug State ===\n");

  // Print all at once
  log_info("%s", log_buffer);
}

#endif // NDEBUG - Release builds: no implementation, header provides inline stubs
