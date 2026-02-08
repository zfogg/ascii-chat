/**
 * @file lock.c
 * @ingroup lock_debug
 * @brief 🔒 Lock debugging and deadlock detection with call stack backtraces and lock ordering validation
 * @date September 2025
 */

// Header must be included even in release builds to get inline no-op stubs
#include <ascii-chat/debug/lock.h>

#ifdef DEBUG_LOCKS
// Only compile lock_debug implementation when DEBUG_LOCKS is defined
// Without DEBUG_LOCKS, lock_debug.h provides inline no-op stubs

#include <ascii-chat/common.h>
#include <ascii-chat/common/buffer_sizes.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/util/fnv1a.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/uthash/uthash.h>
#include <stdlib.h>
#include <time.h>
#include <stdatomic.h>
#include <string.h>

// Enable lock debug tracing to stderr (bypasses logging system)
// Set to 1 to trace all lock operations for debugging deadlocks
#define TRACE_LOCK_DEBUG 0

#if TRACE_LOCK_DEBUG
#include <stdio.h>
// Simple trace for internal lock debug operations
#define LOCK_TRACE(fmt, ...) fprintf(stderr, "[LOCK_TRACE] %s:%d " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
// Trace every lock/unlock with caller info
#define LOCK_OP_TRACE(op, lock_type, file, line, func)                                                                 \
  fprintf(stderr, "[LOCK_OP] %s %s @ %s:%d in %s()\n", op, lock_type, file, line, func)
#else
#define LOCK_TRACE(fmt, ...) ((void)0)
#define LOCK_OP_TRACE(op, lock_type, file, line, func) ((void)0)
#endif

#ifdef _WIN32
#include <io.h>
#include <conio.h>
#else
#include <sys/select.h>
#include <unistd.h>
// termios.h no longer needed - lock debug thread no longer reads stdin
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

// Terminal state no longer needed - lock debug thread no longer reads stdin

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
  record->thread_id = asciichat_thread_current_id();
  record->file_name = file_name;
  record->line_number = line_number;
  record->function_name = function_name;

  // Get current time in nanoseconds
  record->acquisition_time_ns = time_get_ns();

  // Capture backtrace
  // NOTE: platform_backtrace_symbols_destroy() safely handles the case where
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
    platform_backtrace_symbols_destroy(record->backtrace_symbols);
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

  *offset += safe_snprintf(buffer + *offset, SAFE_BUFFER_SIZE(buffer_size, *offset), "Lock #%u: %s at %p\n",
                           collector->count, lock_type_str, record->lock_address);
  *offset += safe_snprintf(buffer + *offset, SAFE_BUFFER_SIZE(buffer_size, *offset), "  Thread ID: %llu\n",
                           (unsigned long long)record->thread_id);
  *offset +=
      safe_snprintf(buffer + *offset, SAFE_BUFFER_SIZE(buffer_size, *offset), "  Acquired: %s:%d in %s()\n",
                    extract_project_relative_path(record->file_name), record->line_number, record->function_name);

  // Calculate how long the lock has been held
  uint64_t current_time_ns = time_get_ns();
  uint64_t held_ns = time_elapsed_ns(record->acquisition_time_ns, current_time_ns);
  char duration_str[32];
  format_duration_ns((double)held_ns, duration_str, sizeof(duration_str));
  *offset += safe_snprintf(buffer + *offset, SAFE_BUFFER_SIZE(buffer_size, *offset), "  Held for: %s\n", duration_str);

  // Print backtrace using platform symbol resolution with colored format
  if (record->backtrace_size > 0) {
    char **symbols = platform_backtrace_symbols(record->backtrace_buffer, record->backtrace_size);
    *offset += platform_format_backtrace_symbols(buffer + *offset, SAFE_BUFFER_SIZE(buffer_size, *offset), "Call stack",
                                                 symbols, record->backtrace_size, 0, 0, NULL);
    if (symbols) {
      platform_backtrace_symbols_destroy(symbols);
    }
  } else {
    *offset +=
        safe_snprintf(buffer + *offset, SAFE_BUFFER_SIZE(buffer_size, *offset), "  Call stack: <capture failed>\n");
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
  char log_message[BUFFER_SIZE_LARGE];
  int offset = 0;

  offset += safe_snprintf(log_message + offset, SAFE_BUFFER_SIZE(sizeof(log_message), offset),
                          "Usage #%u: %s at %s:%d in %s()\n", *count, lock_type_str,
                          extract_project_relative_path(stats->file_name), stats->line_number, stats->function_name);
  offset += safe_snprintf(log_message + offset, SAFE_BUFFER_SIZE(sizeof(log_message), offset),
                          "  Total acquisitions: %llu\n", (unsigned long long)stats->total_acquisitions);
  offset += safe_snprintf(log_message + offset, SAFE_BUFFER_SIZE(sizeof(log_message), offset),
                          "  Total hold time: %llu.%03llu ms\n",
                          (unsigned long long)(stats->total_hold_time_ns / NS_PER_MS_INT),
                          (unsigned long long)((stats->total_hold_time_ns % NS_PER_MS_INT) / 1000));
  offset +=
      safe_snprintf(log_message + offset, SAFE_BUFFER_SIZE(sizeof(log_message), offset),
                    "  Average hold time: %llu.%03llu ms\n", (unsigned long long)(avg_hold_time_ns / NS_PER_MS_INT),
                    (unsigned long long)((avg_hold_time_ns % NS_PER_MS_INT) / 1000));
  offset +=
      safe_snprintf(log_message + offset, SAFE_BUFFER_SIZE(sizeof(log_message), offset),
                    "  Max hold time: %llu.%03llu ms\n", (unsigned long long)(stats->max_hold_time_ns / NS_PER_MS_INT),
                    (unsigned long long)((stats->max_hold_time_ns % NS_PER_MS_INT) / 1000));
  offset +=
      safe_snprintf(log_message + offset, SAFE_BUFFER_SIZE(sizeof(log_message), offset),
                    "  Min hold time: %llu.%03llu ms\n", (unsigned long long)(stats->min_hold_time_ns / NS_PER_MS_INT),
                    (unsigned long long)((stats->min_hold_time_ns % NS_PER_MS_INT) / 1000));
  char first_acq_str[32];
  format_duration_ns((double)stats->first_acquisition_ns, first_acq_str, sizeof(first_acq_str));
  offset += safe_snprintf(log_message + offset, SAFE_BUFFER_SIZE(sizeof(log_message), offset),
                          "  First acquisition: %s\n", first_acq_str);

  char last_acq_str[32];
  format_duration_ns((double)stats->last_acquisition_ns, last_acq_str, sizeof(last_acq_str));
  offset += safe_snprintf(log_message + offset, SAFE_BUFFER_SIZE(sizeof(log_message), offset), "  Last acquisition: %s",
                          last_acq_str);

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
  log_info("  Released: %s:%d in %s()", extract_project_relative_path(record->file_name), record->line_number,
           record->function_name);
  char release_time_str[32];
  format_duration_ns((double)record->acquisition_time_ns, release_time_str, sizeof(release_time_str));
  log_info("  Released at: %s (nanosecond %llu)", release_time_str, (unsigned long long)record->acquisition_time_ns);

  // Print backtrace for the orphaned release
  if (record->backtrace_size > 0) {
    char **symbols = platform_backtrace_symbols(record->backtrace_buffer, record->backtrace_size);
    platform_print_backtrace_symbols("  Release call stack", symbols, record->backtrace_size, 0, 0, NULL);
    if (symbols) {
      platform_backtrace_symbols_destroy(symbols);
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
// Structure to hold info about a long-held lock (for deferred logging)
typedef struct {
  char duration_str[32];
  const char *lock_type_str;
  void *lock_address;
  char file_name[BUFFER_SIZE_SMALL];
  int line_number;
  char function_name[128];
  uint64_t thread_id;
} long_held_lock_info_t;

#define MAX_LONG_HELD_LOCKS 32

static void check_long_held_locks(void) {
  if (!atomic_load(&g_lock_debug_manager.initialized)) {
    return;
  }
  LOCK_TRACE("acquiring lock_records_lock (read)");

  // Collect long-held lock info while holding the read lock
  // We MUST NOT call log functions while holding lock_records_lock because:
  // - log functions may call mutex_lock() for rotation
  // - mutex_lock() macro calls debug_process_tracked_lock()
  // - debug_process_tracked_lock() tries to acquire lock_records_lock as WRITE
  // - DEADLOCK: we already hold it as READ, can't upgrade to WRITE
  long_held_lock_info_t long_held_locks[MAX_LONG_HELD_LOCKS];
  int num_long_held = 0;

  // Acquire read lock for lock_records
  rwlock_rdlock_impl(&g_lock_debug_manager.lock_records_lock);

  uint64_t current_time_ns = time_get_ns();

  // Iterate through all currently held locks and collect info
  lock_record_t *entry, *tmp;
  HASH_ITER(hash_handle, g_lock_debug_manager.lock_records, entry, tmp) {
    if (num_long_held >= MAX_LONG_HELD_LOCKS) {
      break; // Limit how many we report
    }

    // Calculate how long the lock has been held in nanoseconds
    uint64_t held_ns = time_elapsed_ns(entry->acquisition_time_ns, current_time_ns);

    if (held_ns > 100 * NS_PER_MS_INT) {
      long_held_lock_info_t *info = &long_held_locks[num_long_held];

      // Get lock type string
      switch (entry->lock_type) {
      case LOCK_TYPE_MUTEX:
        info->lock_type_str = "MUTEX";
        break;
      case LOCK_TYPE_RWLOCK_READ:
        info->lock_type_str = "RWLOCK_READ";
        break;
      case LOCK_TYPE_RWLOCK_WRITE:
        info->lock_type_str = "RWLOCK_WRITE";
        break;
      default:
        info->lock_type_str = "UNKNOWN";
        break;
      }

      // Copy all info we need for logging later
      format_duration_ns((double)held_ns, info->duration_str, sizeof(info->duration_str));
      info->lock_address = entry->lock_address;
      strncpy(info->file_name, entry->file_name, sizeof(info->file_name) - 1);
      info->file_name[sizeof(info->file_name) - 1] = '\0';
      info->line_number = entry->line_number;
      strncpy(info->function_name, entry->function_name, sizeof(info->function_name) - 1);
      info->function_name[sizeof(info->function_name) - 1] = '\0';
      info->thread_id = entry->thread_id;

      num_long_held++;
    }
  }

  // Release the lock BEFORE logging to prevent deadlock
  rwlock_rdunlock_impl(&g_lock_debug_manager.lock_records_lock);
  LOCK_TRACE("released lock_records_lock (read), found %d long-held locks", num_long_held);

  // Now safe to log - we no longer hold lock_records_lock
  for (int i = 0; i < num_long_held; i++) {
    long_held_lock_info_t *info = &long_held_locks[i];
    log_warn_every(LOG_RATE_FAST,
                   "Lock held for %s (threshold: 100ms) - %s at %p\n"
                   "  Acquired: %s:%d in %s()\n"
                   "  Thread ID: %llu",
                   info->duration_str, info->lock_type_str, info->lock_address,
                   extract_project_relative_path(info->file_name), info->line_number, info->function_name,
                   (unsigned long long)info->thread_id);
  }

  // Print backtrace only once if any long-held locks were found
  if (num_long_held > 0) {
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

  log_debug("Lock debug thread started (use SIGUSR1 to print lock state)");
  LOCK_TRACE("debug thread loop starting");

  while (atomic_load(&g_lock_debug_manager.debug_thread_running)) {
    // Check for locks held > 100ms and log warnings
    check_long_held_locks();

    // Allow external trigger via flag (non-blocking, set by SIGUSR1 handler)
    if (atomic_exchange(&g_lock_debug_manager.should_print_locks, false)) {
      lock_debug_print_state();
    }

    // Do not read from stdin. The keyboard thread is the sole reader.
    // Use SIGUSR1 to trigger lock state printing: kill -USR1 <pid>
#ifdef _WIN32
    platform_sleep_ms(10);
#else
    platform_sleep_ms(100);
#endif

    platform_sleep_ms(100);
  }

  // Thread exiting
  return NULL;
}

// ============================================================================
// Public API Implementation
// ============================================================================

int lock_debug_init(void) {
  log_debug("Starting lock debug system initialization...");

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

  // Note: lock_debug_destroy() will be called during normal shutdown sequence
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

  int thread_result = asciichat_thread_create(&g_lock_debug_manager.debug_thread, debug_thread_func, NULL);

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

void lock_debug_destroy(void) {
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
  log_debug("Lock debug system cleaned up");
}

void lock_debug_cleanup_thread(void) {
  // Check if thread is/was running and join it
  if (atomic_load(&g_lock_debug_manager.debug_thread_running)) {
    atomic_store(&g_lock_debug_manager.debug_thread_running, false);
  }

#ifdef _WIN32
  // On Windows, check if thread handle is valid before joining
  if (g_lock_debug_manager.debug_thread != NULL) {
    int join_result = asciichat_thread_join(&g_lock_debug_manager.debug_thread, NULL);
    if (join_result == 0) {
      // Thread handle is now NULL due to cleanup in asciichat_thread_join
    } else {
      // Force cleanup if join failed
      g_lock_debug_manager.debug_thread = NULL;
    }
  }
#else
  // On POSIX, only attempt join if thread was actually created
  // Use the debug_thread_created flag to reliably track if the thread exists
  if (atomic_load(&g_lock_debug_manager.debug_thread_created)) {
    asciichat_thread_join(&g_lock_debug_manager.debug_thread, NULL);
    // Clear the flag after joining
    atomic_store(&g_lock_debug_manager.debug_thread_created, false);
  }

  // Terminal restore no longer needed - lock debug thread no longer touches stdin
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

  // Skip tracking if system is not fully initialized or during initialization
  bool initialized = atomic_load(&g_lock_debug_manager.initialized);
  bool initializing = atomic_load(&g_initializing);

  if (!initialized || initializing) {
    return true;
  }

  // Filter out ALL functions that our lock debug system uses internally
  // to prevent infinite recursion and deadlock
  // Note: uthash uses macros (HASH_FIND_INT, etc.) so there are no function names to filter
  if (strstr(function_name, "log_") != NULL || strstr(function_name, "platform_") != NULL ||
      strstr(function_name, "create_lock_record") != NULL || strstr(function_name, "update_usage_stats") != NULL ||
      strstr(function_name, "print_") != NULL || strstr(function_name, "debug_") != NULL ||
      strstr(function_name, "lock_debug") != NULL || strstr(file_name, "symbols.c") != NULL ||
      strstr(function_name, "ascii_thread") != NULL ||
      strstr(function_name, "maybe_rotate_log") != NULL || // Logging mutex - prevent deadlock during backtrace
      strstr(function_name, "rotate_log") != NULL) {       // Also filter other rotation functions
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
 * @param file_name Source file name
 * @param line_number Source line number
 * @param function_name Function name
 * @return true if record was created and inserted successfully
 */
static bool debug_create_and_insert_lock_record(void *lock_address, lock_type_t lock_type, const char *file_name,
                                                int line_number, const char *function_name) {
#ifndef DEBUG_LOCKS
  UNUSED(lock_address);
  UNUSED(lock_type);
  UNUSED(file_name);
  UNUSED(line_number);
  UNUSED(function_name);
#endif

  lock_record_t *record = create_lock_record(lock_address, lock_type, file_name, line_number, function_name);
  if (record) {
    // Acquire write lock to make the entire operation atomic
    LOCK_TRACE("acquiring lock_records_lock (write) for %s:%d %s", file_name, line_number, function_name);
    rwlock_wrlock_impl(&g_lock_debug_manager.lock_records_lock);
    LOCK_TRACE("acquired lock_records_lock (write)");

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
    LOCK_TRACE("released lock_records_lock (write) - record added");

    atomic_fetch_add(&g_lock_debug_manager.total_locks_acquired, 1);
    atomic_fetch_add(&g_lock_debug_manager.current_locks_held, 1);
    return true;
  }
  // Record allocation failed
  LOCK_TRACE("record allocation failed");
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

  // Variables to hold info for deferred logging (MUST log AFTER releasing lock_records_lock)
  // Logging while holding lock_records_lock causes deadlock:
  // - log_warn() may call mutex_lock() for rotation
  // - mutex_lock macro calls debug_process_tracked_lock()
  // - debug_process_tracked_lock() tries to acquire lock_records_lock (write)
  // - DEADLOCK: we already hold it, rwlocks don't support recursive write locking
  bool should_log_warning = false;
  char deferred_duration_str[32] = {0};
  char deferred_file_name[BUFFER_SIZE_SMALL] = {0};
  int deferred_line_number = 0;
  char deferred_function_name[128] = {0};
  void *deferred_lock_ptr = NULL;
  const char *deferred_lock_type_str = NULL;
  char **deferred_backtrace_symbols = NULL;
  int deferred_backtrace_size = 0;

  // Acquire write lock for removal
  LOCK_TRACE("acquiring lock_records_lock (write) for unlock %s %s:%d", lock_type_str, file_name, line_number);
  rwlock_wrlock_impl(&g_lock_debug_manager.lock_records_lock);
  LOCK_TRACE("acquired lock_records_lock (write) for unlock");

  lock_record_t *record = NULL;
  HASH_FIND(hash_handle, g_lock_debug_manager.lock_records, &key, sizeof(key), record);
  if (record) {
    // Calculate lock hold time BEFORE removing record
    uint64_t current_time_ns = time_get_ns();
    uint64_t held_ns = time_elapsed_ns(record->acquisition_time_ns, current_time_ns);
    uint64_t held_ms = time_ns_to_ms(held_ns);

    // Check if lock was held too long - collect info for deferred logging
    if (held_ms > LOCK_HOLD_TIME_WARNING_MS) {
      should_log_warning = true;
      format_duration_ms((double)held_ms, deferred_duration_str, sizeof(deferred_duration_str));
      strncpy(deferred_file_name, file_name, sizeof(deferred_file_name) - 1);
      deferred_line_number = line_number;
      strncpy(deferred_function_name, function_name, sizeof(deferred_function_name) - 1);
      deferred_lock_ptr = lock_ptr;
      deferred_lock_type_str = lock_type_str;

      // Copy backtrace symbols if available (we need to copy, not just reference)
      if (record->backtrace_size > 0 && record->backtrace_symbols) {
        deferred_backtrace_size = record->backtrace_size;
        deferred_backtrace_symbols = record->backtrace_symbols;
        record->backtrace_symbols = NULL; // Transfer ownership to avoid double-free
      }
    }

    HASH_DELETE(hash_handle, g_lock_debug_manager.lock_records, record);
    rwlock_wrunlock_impl(&g_lock_debug_manager.lock_records_lock);
    LOCK_TRACE("released lock_records_lock (write) - record removed");

    free_lock_record(record);
    atomic_fetch_add(&g_lock_debug_manager.total_locks_released, 1);
    debug_decrement_lock_counter();

    // NOW safe to log - we no longer hold lock_records_lock
    if (should_log_warning) {
      log_warn("Lock held for %s (threshold: %d ms) at %s:%d in %s()\n"
               "  Lock type: %s, address: %p",
               deferred_duration_str, LOCK_HOLD_TIME_WARNING_MS, extract_project_relative_path(deferred_file_name),
               deferred_line_number, deferred_function_name, deferred_lock_type_str, deferred_lock_ptr);

      // Print backtrace from when lock was acquired
      if (deferred_backtrace_size > 0 && deferred_backtrace_symbols) {
        platform_print_backtrace_symbols("Backtrace from lock acquisition", deferred_backtrace_symbols,
                                         deferred_backtrace_size, 0, 10, NULL);
        free(deferred_backtrace_symbols); // Free the transferred ownership
      } else {
        // No backtrace available, print current backtrace
        log_warn("No backtrace available. Current backtrace:");
        platform_print_backtrace(2); // Skip 2 frames (this function and debug_process_tracked_unlock)
      }
    }

    return true;
  }

  rwlock_wrunlock_impl(&g_lock_debug_manager.lock_records_lock);
  LOCK_TRACE("released lock_records_lock (write) - no record found for %s", lock_type_str);
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
    SET_ERRNO(ERROR_INVALID_STATE, "Attempting to release %s lock when no locks held!", lock_type_str);
  }
#ifdef DEBUG_LOCKS
  SET_ERRNO(ERROR_INVALID_STATE,
            "%s UNTRACKED RELEASED: %p (key=%u) at %s:%d in %s() - total=%llu, held=%u (lock was tracked but record "
            "was lost)",
            lock_type_str, lock_ptr, key, extract_project_relative_path(file_name), line_number, function_name,
            (unsigned long long)released, held);
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
    orphan_record->thread_id = asciichat_thread_current_id();
    orphan_record->file_name = file_name;
    orphan_record->line_number = line_number;
    orphan_record->function_name = function_name;
    orphan_record->acquisition_time_ns = time_get_ns(); // Use release time

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
  LOCK_OP_TRACE("LOCK", "MUTEX", file_name, line_number, function_name);
  if (debug_should_skip_lock_tracking(mutex, file_name, function_name)) {
    return mutex_lock_impl(mutex);
  }

  // Acquire the actual lock first (call implementation to avoid recursion)
  int result = mutex_lock_impl(mutex);
  if (result != 0) {
    return result;
  }

  // Create and add lock record
  debug_create_and_insert_lock_record(mutex, LOCK_TYPE_MUTEX, file_name, line_number, function_name);

  return 0;
}

int debug_mutex_trylock(mutex_t *mutex, const char *file_name, int line_number, const char *function_name) {
  LOCK_OP_TRACE("TRYLOCK", "MUTEX", file_name, line_number, function_name);
  if (debug_should_skip_lock_tracking(mutex, file_name, function_name)) {
    return mutex_trylock_impl(mutex);
  }

  // Try to acquire the lock (call implementation to avoid recursion)
  int result = mutex_trylock_impl(mutex);
  if (result != 0) {
    // Lock not acquired - no tracking needed
    return result;
  }

  // Lock acquired - create and add lock record
  debug_create_and_insert_lock_record(mutex, LOCK_TYPE_MUTEX, file_name, line_number, function_name);

  return 0;
}

int debug_mutex_unlock(mutex_t *mutex, const char *file_name, int line_number, const char *function_name) {
  LOCK_OP_TRACE("UNLOCK", "MUTEX", file_name, line_number, function_name);
  if (debug_should_skip_lock_tracking(mutex, file_name, function_name)) {
    return mutex_unlock_impl(mutex);
  }

  // Look for mutex lock record specifically
  uint32_t key = lock_record_key(mutex, LOCK_TYPE_MUTEX);
  if (!debug_process_tracked_unlock(mutex, key, "MUTEX", file_name, line_number, function_name)) {
    // No record found - this is a genuine tracking error
    debug_process_untracked_unlock(mutex, key, "MUTEX", file_name, line_number, function_name);
  }

  // Unlock the actual mutex (call implementation to avoid recursion)
  return mutex_unlock_impl(mutex);
}

int debug_rwlock_rdlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name) {
  LOCK_OP_TRACE("LOCK", "RWLOCK_RD", file_name, line_number, function_name);
  if (debug_should_skip_lock_tracking(rwlock, file_name, function_name)) {
    return rwlock_rdlock_impl(rwlock);
  }

  // Acquire the actual lock first (call implementation to avoid recursion)
  int result = rwlock_rdlock_impl(rwlock);
  if (result != 0) {
    return result;
  }

  // Create and add lock record
  debug_create_and_insert_lock_record(rwlock, LOCK_TYPE_RWLOCK_READ, file_name, line_number, function_name);

  return 0;
}

int debug_rwlock_wrlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name) {
  LOCK_OP_TRACE("LOCK", "RWLOCK_WR", file_name, line_number, function_name);
  if (debug_should_skip_lock_tracking(rwlock, file_name, function_name)) {
    return rwlock_wrlock_impl(rwlock);
  }

  // Acquire the actual lock first (call implementation to avoid recursion)
  int result = rwlock_wrlock_impl(rwlock);
  if (result != 0) {
    return result;
  }

  // Create and add lock record
  debug_create_and_insert_lock_record(rwlock, LOCK_TYPE_RWLOCK_WRITE, file_name, line_number, function_name);

  return 0;
}

int debug_rwlock_rdunlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name) {
  LOCK_OP_TRACE("UNLOCK", "RWLOCK_RD", file_name, line_number, function_name);
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
  LOCK_OP_TRACE("UNLOCK", "RWLOCK_WR", file_name, line_number, function_name);
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
  offset +=
      safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset), "=== LOCK DEBUG STATE ===\n");

  // Historical Statistics
  offset +=
      safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset), "Historical Statistics:\n");
  offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                          "  Total locks acquired: %llu\n", (unsigned long long)total_acquired);
  offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                          "  Total locks released: %llu\n", (unsigned long long)total_released);
  offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset), "  Currently held: %u\n",
                          currently_held);

  // Check for underflow before subtraction to avoid UB
  if (total_acquired >= total_released) {
    offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                            "  Net locks (acquired - released): %lld\n", (long long)(total_acquired - total_released));
  } else {
    // This shouldn't happen - means more releases than acquires
    offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                            "  *** ERROR: More releases (%llu) than acquires (%llu)! Difference: -%lld ***\n",
                            (unsigned long long)total_released, (unsigned long long)total_acquired,
                            (long long)(total_released - total_acquired));
    offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                            "  *** This indicates lock tracking was not enabled for some acquires ***\n");
  }

  // Currently Active Locks
  offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                          "\n=== Currently Active Locks ===\n");
  if (active_locks == 0) {
    offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                            "  No locks currently held.\n");
    // Check for consistency issues
    if (currently_held > 0) {
      offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                              "  *** CONSISTENCY WARNING: Counter shows %u held locks but no records found! ***\n",
                              currently_held);
      offset +=
          safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                        "  *** This may indicate a crash during lock acquisition or hash table corruption. ***\n");

      // Additional debug: Check hash table statistics
      offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                              "  *** DEBUG: Hash table stats for lock_records: ***\n");
      rwlock_rdlock_impl(&g_lock_debug_manager.lock_records_lock);
      size_t count = HASH_CNT(hash_handle, g_lock_debug_manager.lock_records);
      rwlock_rdunlock_impl(&g_lock_debug_manager.lock_records_lock);
      offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                              "  *** Hash table size: %zu ***\n", count);
      if (count > 0) {
        offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                                "  *** Hash table has entries but iteration didn't find them! ***\n");
      }
    }
  } else {
    offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset), "  Active locks: %u\n",
                            active_locks);
    // Verify consistency the other way
    if (active_locks != currently_held) {
      offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                              "  *** CONSISTENCY WARNING: Found %u active locks but counter shows %u! ***\n",
                              active_locks, currently_held);
    }

    // The lock details are already in the buffer from collect_lock_record_callback
    // No need to add them again here
  }

  // Print usage statistics by code location
  offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                          "\n=== Lock Usage Statistics by Code Location ===\n");
  rwlock_rdlock_impl(&g_lock_debug_manager.usage_stats_lock);

  uint32_t total_usage_locations = 0;
  lock_usage_stats_t *stats_entry, *stats_tmp;
  HASH_ITER(hash_handle, g_lock_debug_manager.usage_stats, stats_entry, stats_tmp) {
    print_usage_stats_callback(stats_entry, &total_usage_locations);
  }

  rwlock_rdunlock_impl(&g_lock_debug_manager.usage_stats_lock);

  if (total_usage_locations == 0) {
    offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                            "  No lock usage statistics available.\n");
  } else {
    offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                            "  Total code locations with lock usage: %u\n", total_usage_locations);
  }

  // Print orphaned releases (unlocks without corresponding locks)
  offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
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

    offset +=
        safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                      "Orphaned Release #%u: %s at %p\n", orphan_count, lock_type_str, orphan_entry->lock_address);
    offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset), "  Thread ID: %llu\n",
                            (unsigned long long)orphan_entry->thread_id);
    offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                            "  Released: %s:%d in %s()\n", extract_project_relative_path(orphan_entry->file_name),
                            orphan_entry->line_number, orphan_entry->function_name);
    char release_time_str[32];
    format_duration_ns((double)orphan_entry->acquisition_time_ns, release_time_str, sizeof(release_time_str));
    offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                            "  Released at: %s (nanosecond %llu)\n", release_time_str,
                            (unsigned long long)orphan_entry->acquisition_time_ns);

    // Print backtrace for orphaned release
    if (orphan_entry->backtrace_size > 0) {
      char **symbols = platform_backtrace_symbols(orphan_entry->backtrace_buffer, orphan_entry->backtrace_size);
      offset +=
          platform_format_backtrace_symbols(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                                            "Release call stack", symbols, orphan_entry->backtrace_size, 0, 0, NULL);
      if (symbols) {
        platform_backtrace_symbols_destroy(symbols);
      }
    } else {
      offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                              "  Release call stack: <capture failed>\n");
    }
  }

  rwlock_rdunlock_impl(&g_lock_debug_manager.orphaned_releases_lock);

  if (orphan_count == 0) {
    offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                            "  No orphaned releases found.\n");
  } else {
    offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                            "  Total orphaned releases: %u\n", orphan_count);
    offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                            "  *** WARNING: %u releases without corresponding locks detected! ***\n", orphan_count);
    offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                            "  *** This indicates double unlocks or missing lock acquisitions! ***\n");
  }

  // End marker
  offset += safe_snprintf(log_buffer + offset, SAFE_BUFFER_SIZE(sizeof(log_buffer), offset),
                          "\n=== End Lock Debug State ===\n");

  // Print all at once
  log_info("%s", log_buffer);
}

#else // !DEBUG_LOCKS - Provide stub implementations when lock debugging is disabled

#include <stdbool.h>
#include <stdint.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/platform/rwlock.h>

// Stub implementations - no-ops when DEBUG_LOCKS is not defined
int lock_debug_init(void) {
  return 0;
}
int lock_debug_start_thread(void) {
  return 0;
}
void lock_debug_destroy(void) {}
void lock_debug_cleanup_thread(void) {}
void lock_debug_get_stats(uint64_t *total_acquired, uint64_t *total_released, uint32_t *currently_held) {
  if (total_acquired)
    *total_acquired = 0;
  if (total_released)
    *total_released = 0;
  if (currently_held)
    *currently_held = 0;
}
bool lock_debug_is_initialized(void) {
  return false;
}
void lock_debug_print_state(void) {}
void lock_debug_trigger_print(void) {}

// Stub implementations for debug_* functions - just pass through to _impl versions
// These are needed because the macros in mutex.h/rwlock.h reference them even when
// lock_debug_is_initialized() returns false (linker still needs symbols to exist)
int debug_mutex_lock(mutex_t *mutex, const char *file_name, int line_number, const char *function_name) {
  (void)file_name;
  (void)line_number;
  (void)function_name;
  return mutex_lock_impl(mutex);
}

int debug_mutex_trylock(mutex_t *mutex, const char *file_name, int line_number, const char *function_name) {
  (void)file_name;
  (void)line_number;
  (void)function_name;
  return mutex_trylock_impl(mutex);
}

int debug_mutex_unlock(mutex_t *mutex, const char *file_name, int line_number, const char *function_name) {
  (void)file_name;
  (void)line_number;
  (void)function_name;
  return mutex_unlock_impl(mutex);
}

int debug_rwlock_rdlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name) {
  (void)file_name;
  (void)line_number;
  (void)function_name;
  return rwlock_rdlock_impl(rwlock);
}

int debug_rwlock_wrlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name) {
  (void)file_name;
  (void)line_number;
  (void)function_name;
  return rwlock_wrlock_impl(rwlock);
}

int debug_rwlock_rdunlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name) {
  (void)file_name;
  (void)line_number;
  (void)function_name;
  return rwlock_rdunlock_impl(rwlock);
}

int debug_rwlock_wrunlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name) {
  (void)file_name;
  (void)line_number;
  (void)function_name;
  return rwlock_wrunlock_impl(rwlock);
}

#endif // DEBUG_LOCKS
