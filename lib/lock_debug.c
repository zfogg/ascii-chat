/**
 * @file lock_debug.c
 * @brief Lock debugging and deadlock detection system implementation
 *
 * This file implements the lock tracking system that helps identify deadlocks
 * by monitoring all mutex and rwlock acquisitions with call stack backtraces.
 * Uses the existing hashtable.c implementation for efficient lock record storage.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include "lock_debug.h"
#include "common.h"
#include "platform/abstraction.h"
#include "hashtable.h" // Need access to hashtable internals
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
  record->lock_address = lock_address;
  record->lock_type = lock_type;
  record->thread_id = ascii_thread_current_id();
  record->file_name = file_name;
  record->line_number = line_number;
  record->function_name = function_name;

  // Get current time
  if (clock_gettime(CLOCK_MONOTONIC, &record->acquisition_time) != 0) {
    log_error("Failed to get acquisition time");
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
 * @param key Hashtable key (unused)
 * @param value Lock record pointer
 * @param user_data Pointer to lock collector structure
 */
static void collect_lock_record_callback(uint32_t key, void *value, void *user_data) {
  UNUSED(key);
  lock_record_t *record = (lock_record_t *)value;
  struct lock_collector {
    uint32_t count;
    char *buffer;
    int *offset;
  } *collector = (struct lock_collector *)user_data;

  collector->count++;
  int *offset = collector->offset;
  char *buffer = collector->buffer;
  size_t buffer_size = 16384; // Match the buffer size in print_all_held_locks

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

  *offset += snprintf(buffer + *offset, buffer_size - *offset, "Lock #%u: %s at %p\n", collector->count, lock_type_str,
                      record->lock_address);
  *offset +=
      snprintf(buffer + *offset, buffer_size - *offset, "  Thread ID: %llu\n", (unsigned long long)record->thread_id);
  *offset += snprintf(buffer + *offset, buffer_size - *offset, "  Acquired: %s:%d in %s()\n", record->file_name,
                      record->line_number, record->function_name);

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

    *offset +=
        snprintf(buffer + *offset, buffer_size - *offset, "  Held for: %lld.%09ld seconds\n", held_sec, held_nsec);
  } else {
    *offset += snprintf(buffer + *offset, buffer_size - *offset, "  Acquired at: %lld.%09ld seconds (monotonic)\n",
                        (long long)record->acquisition_time.tv_sec, record->acquisition_time.tv_nsec);
  }

  // Print backtrace using platform symbol resolution
  if (record->backtrace_size > 0) {
    *offset += snprintf(buffer + *offset, buffer_size - *offset, "  Call stack (%d frames):\n", record->backtrace_size);

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
        *offset += snprintf(buffer + *offset, buffer_size - *offset, "    %2d: %s\n", j, symbols[j]);
      } else {
        *offset += snprintf(buffer + *offset, buffer_size - *offset, "    %2d: %p\n", j, record->backtrace_buffer[j]);
      }
    }

    // For static binaries, provide helpful message
    if (!has_symbols) {
      *offset += snprintf(buffer + *offset, buffer_size - *offset,
                          "  Resolve symbols with: addr2line -e <binary> -f -C <addresses>\n");
    }

    // Clean up symbols
    if (symbols) {
      platform_backtrace_symbols_free(symbols);
    }
  } else {
    *offset += snprintf(buffer + *offset, buffer_size - *offset, "  Call stack: <capture failed>\n");
  }
}

/**
 * @brief Callback function for cleaning up lock records
 * @param key Hashtable key (unused)
 * @param value Lock record pointer
 * @param user_data Pointer to counter for number of records cleaned up
 */
static void cleanup_lock_record_callback(uint32_t key, void *value, void *user_data) {
  UNUSED(key);
  uint32_t *count = (uint32_t *)user_data;
  lock_record_t *record = (lock_record_t *)value;

  (*count)++;
  free_lock_record(record);
}

/**
 * @brief Callback function for printing usage statistics
 * @param key Hashtable key (unused)
 * @param value Usage statistics pointer
 * @param user_data Pointer to total_stats counter
 */
static void print_usage_stats_callback(uint32_t key, void *value, void *user_data) {
  UNUSED(key);
  lock_usage_stats_t *stats = (lock_usage_stats_t *)value;
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

  offset += safe_snprintf(log_message + offset, sizeof(log_message) - offset, "Usage #%u: %s at %s:%d in %s()\n",
                          *count, lock_type_str, stats->file_name, stats->line_number, stats->function_name);
  offset += safe_snprintf(log_message + offset, sizeof(log_message) - offset, "  Total acquisitions: %llu\n",
                          (unsigned long long)stats->total_acquisitions);
  offset += safe_snprintf(log_message + offset, sizeof(log_message) - offset, "  Total hold time: %llu.%03llu ms\n",
                          (unsigned long long)(stats->total_hold_time_ns / 1000000),
                          (unsigned long long)((stats->total_hold_time_ns % 1000000) / 1000));
  offset += safe_snprintf(log_message + offset, sizeof(log_message) - offset, "  Average hold time: %llu.%03llu ms\n",
                          (unsigned long long)(avg_hold_time_ns / 1000000),
                          (unsigned long long)((avg_hold_time_ns % 1000000) / 1000));
  offset += safe_snprintf(log_message + offset, sizeof(log_message) - offset, "  Max hold time: %llu.%03llu ms\n",
                          (unsigned long long)(stats->max_hold_time_ns / 1000000),
                          (unsigned long long)((stats->max_hold_time_ns % 1000000) / 1000));
  offset += safe_snprintf(log_message + offset, sizeof(log_message) - offset, "  Min hold time: %llu.%03llu ms\n",
                          (unsigned long long)(stats->min_hold_time_ns / 1000000),
                          (unsigned long long)((stats->min_hold_time_ns % 1000000) / 1000));
  offset += safe_snprintf(log_message + offset, sizeof(log_message) - offset, "  First acquisition: %lld.%09ld\n",
                          (long long)stats->first_acquisition.tv_sec, stats->first_acquisition.tv_nsec);
  offset += safe_snprintf(log_message + offset, sizeof(log_message) - offset, "  Last acquisition: %lld.%09ld",
                          (long long)stats->last_acquisition.tv_sec, stats->last_acquisition.tv_nsec);

  log_info("%s", log_message);
}

/**
 * @brief Callback function for cleaning up usage statistics
 * @param key Hashtable key (unused)
 * @param value Usage statistics pointer
 * @param user_data Unused
 */
static void cleanup_usage_stats_callback(uint32_t key, void *value, void *user_data) {
  UNUSED(key);
  UNUSED(user_data);
  lock_usage_stats_t *stats = (lock_usage_stats_t *)value;
  SAFE_FREE(stats);
}

/**
 * @brief Callback function for printing orphaned releases
 * @param key Hashtable key (unused)
 * @param value Orphaned release record pointer
 * @param user_data Pointer to total_orphans counter
 */
void print_orphaned_release_callback(uint32_t key, void *value, void *user_data) {
  UNUSED(key);
  lock_record_t *record = (lock_record_t *)value;
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
 * @brief Print all currently held locks with their backtraces and historical stats
 */
void print_all_held_locks(void) {
#ifdef DEBUG_LOCKS
  log_info("[LOCK_DEBUG] print_all_held_locks() called from thread %llu",
           (unsigned long long)ascii_thread_current_id());
#endif

  if (!g_lock_debug_manager.lock_records) {
    log_warn("Lock debug system not initialized.");
    return;
  }

  // Use implementation function directly to avoid recursion
  rwlock_rdlock_impl(&g_lock_debug_manager.lock_records->rwlock);

  // Read counters atomically while holding the lock to ensure consistency with lock records
  uint64_t total_acquired = atomic_load(&g_lock_debug_manager.total_locks_acquired);
  uint64_t total_released = atomic_load(&g_lock_debug_manager.total_locks_released);
  uint32_t currently_held = atomic_load(&g_lock_debug_manager.current_locks_held);

  // Build comprehensive log message with all information
  char log_buffer[16384]; // Increased buffer size for lock details
  int offset = 0;

  // Collect lock information during iteration
  struct lock_collector {
    uint32_t count;
    char *buffer;
    int *offset;
  } lock_collector = {0, log_buffer, &offset};

  hashtable_foreach(g_lock_debug_manager.lock_records, collect_lock_record_callback, &lock_collector);
  uint32_t active_locks = lock_collector.count;

  rwlock_rdunlock_impl(&g_lock_debug_manager.lock_records->rwlock);

  // Header
  offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset, "=== LOCK DEBUG: Lock Status Report ===\n");

  // Historical Statistics
  offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset, "Historical Statistics:\n");
  offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset, "  Total locks acquired: %llu\n",
                     (unsigned long long)total_acquired);
  offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset, "  Total locks released: %llu\n",
                     (unsigned long long)total_released);
  offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset, "  Currently held: %u\n", currently_held);

  // Check for underflow before subtraction to avoid UB
  if (total_acquired >= total_released) {
    offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset, "  Net locks (acquired - released): %lld\n",
                       (long long)(total_acquired - total_released));
  } else {
    // This shouldn't happen - means more releases than acquires
    offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                       "  *** ERROR: More releases (%llu) than acquires (%llu)! Difference: -%lld ***\n",
                       (unsigned long long)total_released, (unsigned long long)total_acquired,
                       (long long)(total_released - total_acquired));
    offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                       "  *** This indicates lock tracking was not enabled for some acquires ***\n");
  }

  // Currently Active Locks
  offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset, "Currently Active Locks:\n");
  if (active_locks == 0) {
    offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset, "  No locks currently held.\n");
    // Check for consistency issues
    if (currently_held > 0) {
      offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                         "  *** CONSISTENCY WARNING: Counter shows %u held locks but no records found! ***\n",
                         currently_held);
      offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                         "  *** This may indicate a crash during lock acquisition or hashtable corruption. ***\n");

      // Additional debug: Check hashtable statistics
      offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                         "  *** DEBUG: Hashtable stats for lock_records: ***\n");
      if (g_lock_debug_manager.lock_records) {
        size_t count = hashtable_size(g_lock_debug_manager.lock_records);
        offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset, "  *** Hashtable size: %zu ***\n", count);
        if (count > 0) {
          offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                             "  *** Hashtable has entries but foreach didn't find them! ***\n");
        }
      } else {
        offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset, "  *** Hashtable is NULL! ***\n");
      }
    }
  } else {
    offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset, "  Active locks: %u\n", active_locks);
    // Verify consistency the other way
    if (active_locks != currently_held) {
      offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                         "  *** CONSISTENCY WARNING: Found %u active locks but counter shows %u! ***\n", active_locks,
                         currently_held);
    }

    // The lock details are already in the buffer from collect_lock_record_callback
    // No need to add them again here
  }

  // Print usage statistics by code location
  offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset, "Lock Usage Statistics by Code Location:\n");
  if (g_lock_debug_manager.usage_stats) {
    rwlock_rdlock_impl(&g_lock_debug_manager.usage_stats->rwlock);

    uint32_t total_usage_locations = 0;
    hashtable_foreach(g_lock_debug_manager.usage_stats, print_usage_stats_callback, &total_usage_locations);

    rwlock_rdunlock_impl(&g_lock_debug_manager.usage_stats->rwlock);

    if (total_usage_locations == 0) {
      offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset, "  No lock usage statistics available.\n");
    } else {
      offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                         "  Total code locations with lock usage: %u\n", total_usage_locations);
    }
  } else {
    offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset, "  Usage statistics not available.\n");
  }

  // Print orphaned releases (unlocks without corresponding locks)
  offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                     "Orphaned Releases (unlocks without corresponding locks):\n");
  if (g_lock_debug_manager.orphaned_releases) {
#ifdef DEBUG_LOCKS
    offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                       "[DEBUG] About to call hashtable_foreach on orphaned_releases\n");
#endif
    rwlock_rdlock_impl(&g_lock_debug_manager.orphaned_releases->rwlock);

    uint32_t total_orphaned_releases = 0;
    hashtable_foreach(g_lock_debug_manager.orphaned_releases, print_orphaned_release_callback,
                      &total_orphaned_releases);
#ifdef DEBUG_LOCKS
    offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                       "[DEBUG] hashtable_foreach completed, total_orphaned_releases=%u\n", total_orphaned_releases);
#endif

    rwlock_rdunlock_impl(&g_lock_debug_manager.orphaned_releases->rwlock);

    if (total_orphaned_releases == 0) {
      offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset, "  No orphaned releases found.\n");
    } else {
      offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset, "  Total orphaned releases: %u\n",
                         total_orphaned_releases);
      offset +=
          snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                   "  *** WARNING: %u releases without corresponding locks detected! ***\n", total_orphaned_releases);
      offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset,
                         "  *** This indicates double unlocks or missing lock acquisitions! ***\n");
    }
  } else {
    offset +=
        snprintf(log_buffer + offset, sizeof(log_buffer) - offset, "  Orphaned release tracking not available.\n");
  }

  // End marker
  offset += snprintf(log_buffer + offset, sizeof(log_buffer) - offset, "=== End Lock Debug ===");

  // Single log_debug call with all the information
  log_debug("%s", log_buffer);
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

  log_info("Lock debug thread started - press '?' to print held locks");

  while (atomic_load(&g_lock_debug_manager.debug_thread_running)) {
    // Allow external trigger via flag (non-blocking)
    if (atomic_exchange(&g_lock_debug_manager.should_print_locks, false)) {
      print_all_held_locks();
    }

    // Check for keyboard input first
#ifdef _WIN32
    if (_kbhit()) {
      int ch = _getch();
      if (ch == '?') {
        print_all_held_locks();
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
          print_all_held_locks();
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

  log_info("Setting initialization flag...");
  // Set initialization flag to prevent tracking during init
  atomic_store(&g_initializing, true);

  log_info("Creating hashtable for lock records...");
  // Create hashtable for lock records
  g_lock_debug_manager.lock_records = hashtable_create();
  if (!g_lock_debug_manager.lock_records) {
    atomic_store(&g_initializing, false);
    SET_ERRNO(ERROR_MEMORY, "Failed to create lock records hashtable");
    return -1;
  }

  log_info("Creating hashtable for usage statistics...");
  // Create hashtable for usage statistics
  g_lock_debug_manager.usage_stats = hashtable_create();
  if (!g_lock_debug_manager.usage_stats) {
    hashtable_destroy(g_lock_debug_manager.lock_records);
    g_lock_debug_manager.lock_records = NULL;
    atomic_store(&g_initializing, false);
    SET_ERRNO(ERROR_MEMORY, "Failed to create usage statistics hashtable");
    return -1;
  }

  log_info("Creating hashtable for orphaned releases...");
  // Create hashtable for orphaned releases
  g_lock_debug_manager.orphaned_releases = hashtable_create();
  if (!g_lock_debug_manager.orphaned_releases) {
    hashtable_destroy(g_lock_debug_manager.lock_records);
    hashtable_destroy(g_lock_debug_manager.usage_stats);
    g_lock_debug_manager.lock_records = NULL;
    g_lock_debug_manager.usage_stats = NULL;
    atomic_store(&g_initializing, false);
    SET_ERRNO(ERROR_MEMORY, "Failed to create orphaned releases hashtable");
    return -1;
  }

  log_info("Initializing atomic variables...");
  // Initialize atomic variables
  atomic_store(&g_lock_debug_manager.total_locks_acquired, 0);
  atomic_store(&g_lock_debug_manager.total_locks_released, 0);
  atomic_store(&g_lock_debug_manager.current_locks_held, 0);
  atomic_store(&g_lock_debug_manager.debug_thread_running, false);
  atomic_store(&g_lock_debug_manager.should_print_locks, false);

  // Initialize thread handle to invalid value
#ifdef _WIN32
  g_lock_debug_manager.debug_thread = NULL;
#else
  // On POSIX, pthread_t doesn't have a standard "invalid" value
  // but we'll rely on the debug_thread_running flag
#endif

#ifdef DEBUG_LOCKS
  log_debug("[LOCK_DEBUG] System initialized: initialized=%d, initializing=%d",
            atomic_load(&g_lock_debug_manager.initialized), atomic_load(&g_initializing));
#endif

  // Clear initialization flag FIRST, then mark as initialized
  // This prevents race condition where initialized=true but initializing=true
  atomic_store(&g_initializing, false);
  atomic_store(&g_lock_debug_manager.initialized, true);

#ifdef DEBUG_LOCKS
  log_debug("[LOCK_DEBUG] After clearing init flag: initialized=%d, initializing=%d",
            atomic_load(&g_lock_debug_manager.initialized), atomic_load(&g_initializing));
#endif

#ifdef DEBUG_LOCKS
  log_info("[LOCK_DEBUG] *** LOCK TRACKING IS NOW ENABLED ***");
#endif

  // Note: lock_debug_cleanup() will be called during normal shutdown sequence
  // and lock_debug_cleanup_thread() will be called as one of the last things before exit

  // log_info("Lock debug system initialized with hashtable");
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
    log_error("Failed to create lock debug thread: %d", thread_result);
    atomic_store(&g_lock_debug_manager.debug_thread_running, false);
    return -1;
  }

  return 0;
}

void lock_debug_trigger_print(void) {
  if (atomic_load(&g_lock_debug_manager.initialized)) {
    atomic_store(&g_lock_debug_manager.should_print_locks, true);
  }
}

void lock_debug_cleanup(void) {
#ifdef DEBUG_LOCKS
  log_debug("[LOCK_DEBUG] lock_debug_cleanup() starting...");
#endif

  // Use atomic exchange to ensure cleanup only runs once
  // This prevents double-cleanup from both atexit() and manual calls
  bool was_initialized = atomic_exchange(&g_lock_debug_manager.initialized, false);
  if (!was_initialized) {
#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] lock_debug_cleanup() - system not initialized or already cleaned up, returning");
#endif
    return;
  }

  // Signal debug thread to stop but don't join it yet
  // Thread joining will happen later in lock_debug_cleanup_thread()
#ifdef DEBUG_LOCKS
  log_debug("[LOCK_DEBUG] lock_debug_cleanup() - signaling debug thread to stop...");
#endif
  if (atomic_load(&g_lock_debug_manager.debug_thread_running)) {
#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] lock_debug_cleanup() - setting debug thread running flag to false");
#endif
    atomic_store(&g_lock_debug_manager.debug_thread_running, false);
#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] lock_debug_cleanup() - debug thread signaled to stop (will be joined later)");
#endif
  } else {
#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] lock_debug_cleanup() - debug thread was not running");
#endif
  }

  // Clean up all remaining lock records
#ifdef DEBUG_LOCKS
  log_debug("[LOCK_DEBUG] lock_debug_cleanup() - cleaning up lock records...");
#endif

  if (g_lock_debug_manager.lock_records) {
#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] lock_debug_cleanup() - acquiring write lock on lock_records hashtable...");
#endif
    rwlock_wrlock_impl(&g_lock_debug_manager.lock_records->rwlock);

#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] lock_debug_cleanup() - freeing all lock records...");
#endif
    // Free all lock records
    uint32_t lock_records_cleaned = 0;
    hashtable_foreach(g_lock_debug_manager.lock_records, cleanup_lock_record_callback, &lock_records_cleaned);
    if (lock_records_cleaned > 0) {
      log_info("Cleaned up %u lock records", lock_records_cleaned);
    }

#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] lock_debug_cleanup() - releasing write lock on lock_records hashtable...");
#endif
    rwlock_wrunlock_impl(&g_lock_debug_manager.lock_records->rwlock);

#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] lock_debug_cleanup() - destroying lock_records hashtable...");
#endif
    hashtable_destroy(g_lock_debug_manager.lock_records);
    g_lock_debug_manager.lock_records = NULL;
#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] lock_debug_cleanup() - lock_records hashtable destroyed");
#endif
  } else {
#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] lock_debug_cleanup() - lock_records hashtable was NULL");
#endif
  }

  // Clean up usage statistics
#ifdef DEBUG_LOCKS
  log_debug("[LOCK_DEBUG] lock_debug_cleanup() - cleaning up usage statistics...");
#endif

  if (g_lock_debug_manager.usage_stats) {
#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] lock_debug_cleanup() - acquiring write lock on usage_stats hashtable...");
#endif
    rwlock_wrlock_impl(&g_lock_debug_manager.usage_stats->rwlock);

#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] lock_debug_cleanup() - freeing all usage statistics...");
#endif
    // Free all usage statistics
    hashtable_foreach(g_lock_debug_manager.usage_stats, cleanup_usage_stats_callback, NULL);

#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] lock_debug_cleanup() - releasing write lock on usage_stats hashtable...");
#endif
    rwlock_wrunlock_impl(&g_lock_debug_manager.usage_stats->rwlock);

#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] lock_debug_cleanup() - destroying usage_stats hashtable...");
#endif
    hashtable_destroy(g_lock_debug_manager.usage_stats);
    g_lock_debug_manager.usage_stats = NULL;
#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] lock_debug_cleanup() - usage_stats hashtable destroyed");
#endif
  } else {
#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] lock_debug_cleanup() - usage_stats hashtable was NULL");
#endif
  }

  // Clean up orphaned releases
#ifdef DEBUG_LOCKS
  log_debug("[LOCK_DEBUG] lock_debug_cleanup() - cleaning up orphaned releases...");
#endif

  if (g_lock_debug_manager.orphaned_releases) {
#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] lock_debug_cleanup() - acquiring write lock on orphaned_releases hashtable...");
#endif
    rwlock_wrlock_impl(&g_lock_debug_manager.orphaned_releases->rwlock);

#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] lock_debug_cleanup() - freeing all orphaned releases...");
#endif

    // Free all orphaned release records
    uint32_t orphaned_releases_cleaned = 0;
    hashtable_foreach(g_lock_debug_manager.orphaned_releases, cleanup_lock_record_callback, &orphaned_releases_cleaned);
    if (orphaned_releases_cleaned > 0) {
      log_info("Cleaned up %u orphaned release records", orphaned_releases_cleaned);
    }

#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] lock_debug_cleanup() - releasing write lock on orphaned_releases hashtable...");
#endif
    rwlock_wrunlock_impl(&g_lock_debug_manager.orphaned_releases->rwlock);

#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] lock_debug_cleanup() - destroying orphaned_releases hashtable...");
#endif

    hashtable_destroy(g_lock_debug_manager.orphaned_releases);
    g_lock_debug_manager.orphaned_releases = NULL;

#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] lock_debug_cleanup() - orphaned_releases hashtable destroyed");
#endif
  } else {
#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] lock_debug_cleanup() - orphaned_releases hashtable was NULL");
#endif
  }

  // initialized flag already set to false at the beginning via atomic_exchange
#ifdef DEBUG_LOCKS
  log_debug("[LOCK_DEBUG] lock_debug_cleanup() - calling log_info...");
#endif
  log_info("Lock debug system cleaned up");

#ifdef DEBUG_LOCKS
  log_debug("[LOCK_DEBUG] lock_debug_cleanup() - completed successfully");
#endif
}

void lock_debug_cleanup_thread(void) {
#ifdef DEBUG_LOCKS
  log_debug("[LOCK_DEBUG] lock_debug_cleanup_thread() starting...");
#endif

  // Check if thread is/was running and join it
  if (atomic_load(&g_lock_debug_manager.debug_thread_running)) {
#ifdef DEBUG_LOCKS
    log_warn("[LOCK_DEBUG] lock_debug_cleanup_thread() - thread still running, this shouldn't happen");
#endif
    atomic_store(&g_lock_debug_manager.debug_thread_running, false);
  }

#ifdef _WIN32
  // On Windows, check if thread handle is valid before joining
  if (g_lock_debug_manager.debug_thread != NULL) {
#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] lock_debug_cleanup_thread() - joining debug thread (handle=%p)...",
              g_lock_debug_manager.debug_thread);
#endif
    int join_result = ascii_thread_join(&g_lock_debug_manager.debug_thread, NULL);
    if (join_result == 0) {
#ifdef DEBUG_LOCKS
      log_debug("[LOCK_DEBUG] lock_debug_cleanup_thread() - debug thread joined successfully");
#endif
      // Thread handle is now NULL due to cleanup in ascii_thread_join
    } else {
#ifdef DEBUG_LOCKS
      log_warn("[LOCK_DEBUG] lock_debug_cleanup_thread() - failed to join debug thread");
#endif
      // Force cleanup if join failed
      g_lock_debug_manager.debug_thread = NULL;
    }
  } else {
#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] lock_debug_cleanup_thread() - debug thread handle is NULL, nothing to join");
#endif
  }
#else
  // On POSIX, always attempt join if we have a thread
#ifdef DEBUG_LOCKS
  log_debug("[LOCK_DEBUG] lock_debug_cleanup_thread() - joining debug thread...");
#endif
  ascii_thread_join(&g_lock_debug_manager.debug_thread, NULL);
#ifdef DEBUG_LOCKS
  log_debug("[LOCK_DEBUG] lock_debug_cleanup_thread() - debug thread joined successfully");
#endif

  // Restore terminal to original mode if it was changed
  if (g_termios_saved) {
    tcsetattr(STDIN_FILENO, TCSANOW, &g_original_termios);
    g_termios_saved = false;
  }
#endif

#ifdef DEBUG_LOCKS
  log_debug("[LOCK_DEBUG] lock_debug_cleanup_thread() - completed successfully");
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
  if (strstr(function_name, "log_") != NULL || strstr(function_name, "platform_") != NULL ||
      strstr(function_name, "hashtable_") != NULL || strstr(function_name, "create_lock_record") != NULL ||
      strstr(function_name, "update_usage_stats") != NULL || strstr(function_name, "print_") != NULL ||
      strstr(function_name, "debug_") != NULL || strstr(function_name, "lock_debug") != NULL ||
      strstr(file_name, "symbols.c") != NULL || strstr(function_name, "ascii_thread") != NULL) {
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
    uint32_t key = lock_record_key(lock_address, lock_type);
    bool inserted = hashtable_insert(g_lock_debug_manager.lock_records, key, record);

    if (inserted) {
#ifdef DEBUG_LOCKS
      uint64_t acquired = atomic_fetch_add(&g_lock_debug_manager.total_locks_acquired, 1) + 1;
      uint32_t held = atomic_fetch_add(&g_lock_debug_manager.current_locks_held, 1) + 1;
#else
      atomic_fetch_add(&g_lock_debug_manager.total_locks_acquired, 1);
      atomic_fetch_add(&g_lock_debug_manager.current_locks_held, 1);
#endif
#ifdef DEBUG_LOCKS
      log_debug("[LOCK_DEBUG] %s ACQUIRED: %p (key=%u) at %s:%d in %s() - total=%llu, held=%u", lock_type_str,
                lock_address, key, file_name, line_number, function_name, (unsigned long long)acquired, held);
#endif
      return true;
    }
    // Hashtable insert failed - clean up the record and log an error
#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] ERROR: Failed to insert %s record for %p (key=%u) at %s:%d in %s()", lock_type_str,
              lock_address, key, file_name, line_number, function_name);
#endif
    free_lock_record(record);
  } else {
    // Record allocation failed - log an error
#ifdef DEBUG_LOCKS
    log_debug("[LOCK_DEBUG] ERROR: Failed to create lock record for %p at %s:%d in %s()", lock_address, file_name,
              line_number, function_name);
#endif
  }
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

  lock_record_t *record = (lock_record_t *)hashtable_lookup(g_lock_debug_manager.lock_records, key);
  if (record) {
    if (hashtable_remove(g_lock_debug_manager.lock_records, key)) {
      free_lock_record(record);
#ifdef DEBUG_LOCKS
      uint64_t released = atomic_fetch_add(&g_lock_debug_manager.total_locks_released, 1) + 1;
      uint32_t held = debug_decrement_lock_counter();
#else
      atomic_fetch_add(&g_lock_debug_manager.total_locks_released, 1);
      debug_decrement_lock_counter();
#endif
#ifdef DEBUG_LOCKS
      log_debug("[LOCK_DEBUG] %s RELEASED: %p (key=%u) at %s:%d in %s() - total=%llu, held=%u", lock_type_str, lock_ptr,
                key, file_name, line_number, function_name, (unsigned long long)released, held);
#endif
      return true;
    }
  }
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
    log_error("[LOCK_DEBUG] *** ERROR: Attempting to release %s lock when no locks held! ***", lock_type_str);
#endif
    log_error("%s:%d in %s()", file_name, line_number, function_name);
  }
#ifdef DEBUG_LOCKS
  log_error("[LOCK_DEBUG] %s UNTRACKED RELEASED: %p (key=%u) at %s:%d in %s() - total=%llu, held=%u", lock_type_str,
            lock_ptr, key, file_name, line_number, function_name, (unsigned long long)released, held);
#endif
#ifdef DEBUG_LOCKS
  log_error("[LOCK_DEBUG] *** WARNING: %s lock was acquired and tracked but record was lost! ***", lock_type_str);
#endif

  // Create an orphaned release record to track this problematic unlock
  lock_record_t *orphan_record = SAFE_CALLOC(1, sizeof(lock_record_t), lock_record_t *);
  if (orphan_record) {
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

    // Store in orphaned releases hashtable for later analysis
    if (g_lock_debug_manager.orphaned_releases) {
      hashtable_insert(g_lock_debug_manager.orphaned_releases, key, orphan_record);
    } else {
      free_lock_record(orphan_record);
    }
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
      static atomic_int debug_count = 0;

      int current_count = atomic_fetch_add(&debug_count, 1);
      if (current_count < 3) {
#ifdef DEBUG_LOCKS
        log_debug("[LOCK_DEBUG] FILTERED UNLOCK #%d: mutex=%p, key=%u at %s:%d in %s()", current_count + 1, mutex, key,
                  file_name, line_number, function_name);
      } else if (current_count == 50) {
#ifdef DEBUG_LOCKS
        log_debug("[LOCK_DEBUG] Suppressed further filtered unlock messages after 50 calls");
#endif
#endif
      }
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
#ifdef DEBUG_LOCKS
  log_debug("[LOCK_DEBUG] State: initialized=%d, initializing=%d, result=%d",
            atomic_load(&g_lock_debug_manager.initialized), atomic_load(&g_initializing), lock_debug_is_initialized());
#endif
#ifdef DEBUG_LOCKS
  log_debug("[LOCK_DEBUG] Stats: acquired=%llu, released=%llu, held=%u",
            (unsigned long long)atomic_load(&g_lock_debug_manager.total_locks_acquired),
            (unsigned long long)atomic_load(&g_lock_debug_manager.total_locks_released),
            atomic_load(&g_lock_debug_manager.current_locks_held));
#endif
}
