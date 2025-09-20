#pragma once

/**
 * @file lock_debug.h
 * @brief Lock debugging and deadlock detection system for ASCII-Chat
 *
 * This module provides comprehensive lock tracking to help identify deadlocks
 * and lock contention issues. It tracks all mutex and rwlock acquisitions
 * with call stack backtraces and provides a debug thread to print held locks.
 *
 * FEATURES:
 * =========
 * - Tracks all mutex and rwlock acquisitions with backtraces
 * - Thread-safe lock record management using rwlock and atomics
 * - Debug thread that prints held locks when '?' key is pressed
 * - Automatic cleanup of lock records when locks are released
 * - Integration with existing platform backtrace functionality
 *
 * USAGE:
 * ======
 * 1. Initialize the lock debug system: lock_debug_init()
 * 2. Replace mutex/rwlock calls with tracked versions:
 *    - mutex_lock() → debug_mutex_lock()
 *    - rwlock_rdlock() → debug_rwlock_rdlock()
 *    - rwlock_wrlock() → debug_rwlock_wrlock()
 * 3. Start debug thread: lock_debug_start_thread()
 * 4. Press '?' key to print all held locks and their backtraces
 * 5. Cleanup: lock_debug_cleanup()
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2025
 */

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "platform/thread.h"
#include "platform/mutex.h"
#include "platform/rwlock.h"
#include "hashtable.h"

// ============================================================================
// Constants and Limits
// ============================================================================

#define MAX_LOCK_RECORDS 1024     ///< Maximum number of concurrent lock records
#define MAX_BACKTRACE_FRAMES 32   ///< Maximum backtrace frames to capture
#define MAX_FUNCTION_NAME_LEN 256 ///< Maximum function name length
#define MAX_FILE_NAME_LEN 256     ///< Maximum file name length

// ============================================================================
// Lock Types
// ============================================================================

typedef enum {
  LOCK_TYPE_MUTEX = 0,   ///< Standard mutex
  LOCK_TYPE_RWLOCK_READ, ///< Read-write lock (read mode)
  LOCK_TYPE_RWLOCK_WRITE ///< Read-write lock (write mode)
} lock_type_t;

// ============================================================================
// Forward Declarations
// ============================================================================

typedef struct lock_record lock_record_t;

// ============================================================================
// Lock Record Structure
// ============================================================================

/**
 * @brief Individual lock record tracking a single lock acquisition
 *
 * This structure contains all information needed to track a lock:
 * - Lock identification (address, type)
 * - Acquisition details (timestamp, thread ID)
 * - Call stack backtrace with symbol information
 * - File and line number where lock was acquired
 */
struct lock_record {
  void *lock_address;               ///< Address of the actual lock object
  lock_type_t lock_type;            ///< Type of lock (mutex, rwlock read/write)
  uint64_t thread_id;               ///< Thread ID that acquired the lock
  struct timespec acquisition_time; ///< When the lock was acquired
  const char *file_name;            ///< Source file where lock was acquired
  int line_number;                  ///< Line number where lock was acquired
  const char *function_name;        ///< Function name where lock was acquired

  // Backtrace information
  void *backtrace_buffer[MAX_BACKTRACE_FRAMES]; ///< Raw backtrace addresses
  int backtrace_size;                           ///< Number of valid backtrace frames
  char **backtrace_symbols;                     ///< Symbolized backtrace (allocated)
};

// ============================================================================
// Lock Usage Statistics Structure
// ============================================================================

/**
 * @brief Statistics for lock usage by code location
 *
 * This structure tracks aggregate statistics for each unique
 * file:line:function location that acquires locks.
 */
typedef struct lock_usage_stats {
  const char *file_name;             ///< Source file name
  int line_number;                   ///< Source line number
  const char *function_name;         ///< Function name
  lock_type_t lock_type;             ///< Type of lock
  uint64_t total_acquisitions;       ///< Total number of times this location acquired a lock
  uint64_t total_hold_time_ns;       ///< Total time spent holding locks (nanoseconds)
  uint64_t max_hold_time_ns;         ///< Maximum time spent holding a single lock
  uint64_t min_hold_time_ns;         ///< Minimum time spent holding a single lock
  struct timespec first_acquisition; ///< When this location first acquired a lock
  struct timespec last_acquisition;  ///< When this location last acquired a lock
} lock_usage_stats_t;

// ============================================================================
// Lock Debug Manager
// ============================================================================

/**
 * @brief Main lock debugging manager structure
 *
 * This structure manages all lock tracking functionality:
 * - Hashtable of active lock records (using existing hashtable.c)
 * - Thread-safe access using hashtable's built-in rwlock
 * - Atomic counters for statistics
 * - Debug thread management
 */
typedef struct {
  // Hashtable for O(1) lock record lookup using existing hashtable implementation
  hashtable_t *lock_records; ///< Hashtable storing lock records

  // Hashtable for lock usage statistics by code location
  hashtable_t *usage_stats; ///< Hashtable storing usage statistics

  // Hashtable for tracking orphaned releases (unlocks without corresponding locks)
  hashtable_t *orphaned_releases; ///< Hashtable storing orphaned release records

  // Statistics (atomic for thread safety)
  atomic_uint_fast64_t total_locks_acquired; ///< Total locks acquired (lifetime)
  atomic_uint_fast64_t total_locks_released; ///< Total locks released (lifetime)
  atomic_uint_fast32_t current_locks_held;   ///< Current number of held locks

  // Debug thread management
  asciithread_t debug_thread;       ///< Debug thread handle
  atomic_bool debug_thread_running; ///< Debug thread running flag
  atomic_bool should_print_locks;   ///< Flag to trigger lock printing

  // Initialization state
  atomic_bool initialized; ///< System initialization state
} lock_debug_manager_t;

/**
 * @brief Get the lock debug manager
 * @return Pointer to the lock debug manager
 */
extern lock_debug_manager_t g_lock_debug_manager;
extern atomic_bool g_initializing;

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Generate a unique key for a lock record
 * @param lock_address Address of the lock object
 * @param lock_type Type of lock (mutex, rwlock read/write)
 * @return Unique 32-bit key for hashtable lookup
 */
static inline uint32_t lock_record_key(void *lock_address, lock_type_t lock_type) {
  // Combine lock address and type into a unique key
  uintptr_t addr = (uintptr_t)lock_address;
  return (uint32_t)((addr >> 2) ^ (uint32_t)lock_type);
}

/**
 * @brief Generate a unique key for usage statistics
 * @param file_name Source file name
 * @param line_number Source line number
 * @param function_name Function name
 * @param lock_type Type of lock
 * @return Unique 32-bit key for hashtable lookup
 */
static inline uint32_t usage_stats_key(const char *file_name, int line_number, const char *function_name,
                                       lock_type_t lock_type) {
  // Create a hash from file name, line number, function name, and lock type
  uint32_t hash = 0;
  const char *p = file_name;
  while (*p) {
    hash = hash * 31 + *p++;
  }
  hash = hash * 31 + (uint32_t)line_number;
  p = function_name;
  while (*p) {
    hash = hash * 31 + *p++;
  }
  hash = hash * 31 + (uint32_t)lock_type;
  return hash;
}

// ============================================================================
// Public API Functions
// ============================================================================

/**
 * @brief Initialize the lock debugging system
 * @return 0 on success, error code on failure
 */
int lock_debug_init(void);

/**
 * @brief Start the debug thread for lock monitoring
 * @return 0 on success, error code on failure
 */
int lock_debug_start_thread(void);

/**
 * @brief Stop the debug thread and cleanup resources
 */
void lock_debug_cleanup(void);

/**
 * @brief Join the debug thread (should be called as one of the last things before exit)
 */
void lock_debug_cleanup_thread(void);

/**
 * @brief Trigger lock information printing (called by debug thread)
 */
void lock_debug_trigger_print(void);

// ============================================================================
// Tracked Lock Functions
// ============================================================================

/**
 * @brief Tracked mutex lock with backtrace capture
 * @param mutex Pointer to mutex to lock
 * @param file_name Source file name (use __FILE__)
 * @param line_number Source line number (use __LINE__)
 * @param function_name Function name (use __func__)
 * @return 0 on success, error code on failure
 */
int debug_mutex_lock(mutex_t *mutex, const char *file_name, int line_number, const char *function_name);

/**
 * @brief Tracked mutex unlock with record cleanup
 * @param mutex Pointer to mutex to unlock
 * @param file_name Source file name (use __FILE__)
 * @param line_number Source line number (use __LINE__)
 * @param function_name Function name (use __func__)
 * @return 0 on success, error code on failure
 */
int debug_mutex_unlock(mutex_t *mutex, const char *file_name, int line_number, const char *function_name);

/**
 * @brief Tracked rwlock read lock with backtrace capture
 * @param rwlock Pointer to rwlock to lock for reading
 * @param file_name Source file name (use __FILE__)
 * @param line_number Source line number (use __LINE__)
 * @param function_name Function name (use __func__)
 * @return 0 on success, error code on failure
 */
int debug_rwlock_rdlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name);

/**
 * @brief Tracked rwlock write lock with backtrace capture
 * @param rwlock Pointer to rwlock to lock for writing
 * @param file_name Source file name (use __FILE__)
 * @param line_number Source line number (use __LINE__)
 * @param function_name Function name (use __func__)
 * @return 0 on success, error code on failure
 */
int debug_rwlock_wrlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name);

/**
 * @brief Tracked rwlock read unlock with record cleanup
 * @param rwlock Pointer to rwlock to unlock (read)
 * @param file_name Source file name (use __FILE__)
 * @param line_number Source line number (use __LINE__)
 * @param function_name Function name (use __func__)
 * @return 0 on success, error code on failure
 */
int debug_rwlock_rdunlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name);

/**
 * @brief Tracked rwlock write unlock with record cleanup
 * @param rwlock Pointer to rwlock to unlock (write)
 * @param file_name Source file name (use __FILE__)
 * @param line_number Source line number (use __LINE__)
 * @param function_name Function name (use __func__)
 * @return 0 on success, error code on failure
 */
int debug_rwlock_wrunlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name);

/**
 * @brief Tracked rwlock unlock with record cleanup (generic)
 * @param rwlock Pointer to rwlock to unlock
 * @param file_name Source file name (use __FILE__)
 * @param line_number Source line number (use __LINE__)
 * @param function_name Function name (use __func__)
 * @return 0 on success, error code on failure
 */
int debug_rwlock_unlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name);

// ============================================================================
// Convenience Macros
// ============================================================================

/**
 * @brief Convenience macro for tracked mutex lock
 * @param mutex Pointer to mutex to lock
 */
#define DEBUG_MUTEX_LOCK(mutex) debug_mutex_lock(mutex, __FILE__, __LINE__, __func__)

/**
 * @brief Convenience macro for tracked mutex unlock
 * @param mutex Pointer to mutex to unlock
 */
#define DEBUG_MUTEX_UNLOCK(mutex) debug_mutex_unlock(mutex, __FILE__, __LINE__, __func__)

/**
 * @brief Convenience macro for tracked rwlock read lock
 * @param rwlock Pointer to rwlock to lock for reading
 */
#define DEBUG_RWLOCK_RDLOCK(rwlock) debug_rwlock_rdlock(rwlock, __FILE__, __LINE__, __func__)

/**
 * @brief Convenience macro for tracked rwlock write lock
 * @param rwlock Pointer to rwlock to lock for writing
 */
#define DEBUG_RWLOCK_WRLOCK(rwlock) debug_rwlock_wrlock(rwlock, __FILE__, __LINE__, __func__)

/**
 * @brief Convenience macro for tracked rwlock unlock
 * @param rwlock Pointer to rwlock to unlock
 */
#define DEBUG_RWLOCK_UNLOCK(rwlock) debug_rwlock_unlock(rwlock, __FILE__, __LINE__, __func__)

// ============================================================================
// Statistics and Information Functions
// ============================================================================

/**
 * @brief Get current lock statistics
 * @param total_acquired Output: total locks acquired (lifetime)
 * @param total_released Output: total locks released (lifetime)
 * @param currently_held Output: current number of held locks
 */
void lock_debug_get_stats(uint64_t *total_acquired, uint64_t *total_released, uint32_t *currently_held);

/**
 * @brief Check if lock debugging is initialized
 * @return true if initialized, false otherwise
 */
bool lock_debug_is_initialized(void);

/**
 * @brief Print current lock debug state for debugging
 */
void lock_debug_print_state(void);

void print_all_held_locks(void);

void print_orphaned_release_callback(uint32_t key, void *value, void *user_data);
