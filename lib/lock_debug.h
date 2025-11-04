#pragma once

#include "platform/thread.h" // For ascii_thread_self()
#include "util/fnv1a.h"      // For fnv1a_hash_string, fnv1a_hash_bytes, FNV1A_32_OFFSET_BASIS, FNV1A_32_PRIME

/**
 * @file lock_debug.h
 * @ingroup lock_debug
 * @brief Lock debugging and deadlock detection system for ascii-chat
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
 * @date September 2025
 */

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "platform/thread.h"
#include "platform/mutex.h"
#include "platform/rwlock.h"
#include <uthash.h> // Use angle brackets to get deps/uthash/src/uthash.h, not util/uthash.h

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

/**
 * @brief Lock type enumeration
 * @ingroup lock_debug
 */
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
 *
 * @ingroup lock_debug
 */
struct lock_record {
  uint32_t key;                     ///< Hash key for uthash lookup
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

  UT_hash_handle hash_handle; ///< Makes this structure hashable (uthash internal bookkeeping)
};

// ============================================================================
// Lock Usage Statistics Structure
// ============================================================================

/**
 * @brief Statistics for lock usage by code location
 *
 * This structure tracks aggregate statistics for each unique
 * file:line:function location that acquires locks.
 *
 * @ingroup lock_debug
 */
typedef struct lock_usage_stats {
  uint32_t key;                      ///< Hash key for uthash lookup
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
  UT_hash_handle hash_handle;        ///< Makes this structure hashable (uthash internal bookkeeping)
} lock_usage_stats_t;

// ============================================================================
// Lock Debug Manager
// ============================================================================

/**
 * @brief Main lock debugging manager structure
 *
 * This structure manages all lock tracking functionality:
 * - Hashtable of active lock records (using uthash)
 * - Thread-safe access using external rwlock
 * - Atomic counters for statistics
 * - Debug thread management
 *
 * @ingroup lock_debug
 */
typedef struct {
  // Uthash hash tables (head pointers, NULL-initialized)
  lock_record_t *lock_records;      ///< Hash table storing lock records
  lock_usage_stats_t *usage_stats;  ///< Hash table storing usage statistics
  lock_record_t *orphaned_releases; ///< Hash table storing orphaned release records

  // External rwlocks for thread safety (uthash requires external locking)
  rwlock_t lock_records_lock;      ///< Read-write lock for lock_records
  rwlock_t usage_stats_lock;       ///< Read-write lock for usage_stats
  rwlock_t orphaned_releases_lock; ///< Read-write lock for orphaned_releases

  // Statistics (atomic for thread safety)
  atomic_uint_fast64_t total_locks_acquired; ///< Total locks acquired (lifetime)
  atomic_uint_fast64_t total_locks_released; ///< Total locks released (lifetime)
  atomic_uint_fast32_t current_locks_held;   ///< Current number of held locks

  // Debug thread management
  asciithread_t debug_thread;       ///< Debug thread handle
  atomic_bool debug_thread_running; ///< Debug thread running flag
  atomic_bool debug_thread_created; ///< Track if thread was ever created (for proper cleanup)
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
 * @param thread_id Thread ID that holds/held the lock
 * @return Unique 32-bit key for hashtable lookup
 *
 * IMPORTANT: The key must include thread_id because:
 * - Multiple threads can hold read locks on the same rwlock
 * - Each thread's lock must be tracked separately
 * - Unlock must match the exact thread that locked
 */
static inline uint32_t lock_record_key(void *lock_address, lock_type_t lock_type) {
  // Combine lock address, type, and thread ID into a unique key using FNV-1a
  // This prevents key collisions when the same lock is used by different threads
  // Hash all components together using FNV-1a for consistent hashing
  struct {
    uintptr_t addr;
    lock_type_t lock_type;
    uint64_t thread_id;
  } key_data = {.addr = (uintptr_t)lock_address, .lock_type = lock_type, .thread_id = ascii_thread_current_id()};
  return fnv1a_hash_bytes(&key_data, sizeof(key_data));
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
  uint32_t hash = fnv1a_hash_string(file_name);
  hash = fnv1a_hash_bytes(&line_number, sizeof(line_number)) ^ hash;
  hash = fnv1a_hash_string(function_name) ^ hash;
  hash = fnv1a_hash_bytes(&lock_type, sizeof(lock_type)) ^ hash;
  return hash;
}

// ============================================================================
// Public API Functions
// ============================================================================

/**
 * @brief Initialize the lock debugging system
 * @return 0 on success, error code on failure
 * @ingroup lock_debug
 */
int lock_debug_init(void);

/**
 * @brief Start the debug thread for lock monitoring
 * @return 0 on success, error code on failure
 * @ingroup lock_debug
 */
int lock_debug_start_thread(void);

/**
 * @brief Stop the debug thread and cleanup resources
 * @ingroup lock_debug
 */
void lock_debug_cleanup(void);

/**
 * @brief Join the debug thread (should be called as one of the last things before exit)
 * @ingroup lock_debug
 */
void lock_debug_cleanup_thread(void);

/**
 * @brief Trigger lock information printing (called by debug thread)
 * @ingroup lock_debug
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
 * @ingroup lock_debug
 */
int debug_mutex_lock(mutex_t *mutex, const char *file_name, int line_number, const char *function_name);

/**
 * @brief Tracked mutex unlock with record cleanup
 * @param mutex Pointer to mutex to unlock
 * @param file_name Source file name (use __FILE__)
 * @param line_number Source line number (use __LINE__)
 * @param function_name Function name (use __func__)
 * @return 0 on success, error code on failure
 * @ingroup lock_debug
 */
int debug_mutex_unlock(mutex_t *mutex, const char *file_name, int line_number, const char *function_name);

/**
 * @brief Tracked rwlock read lock with backtrace capture
 * @param rwlock Pointer to rwlock to lock for reading
 * @param file_name Source file name (use __FILE__)
 * @param line_number Source line number (use __LINE__)
 * @param function_name Function name (use __func__)
 * @return 0 on success, error code on failure
 * @ingroup lock_debug
 */
int debug_rwlock_rdlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name);

/**
 * @brief Tracked rwlock write lock with backtrace capture
 * @param rwlock Pointer to rwlock to lock for writing
 * @param file_name Source file name (use __FILE__)
 * @param line_number Source line number (use __LINE__)
 * @param function_name Function name (use __func__)
 * @return 0 on success, error code on failure
 * @ingroup lock_debug
 */
int debug_rwlock_wrlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name);

/**
 * @brief Tracked rwlock read unlock with record cleanup
 * @param rwlock Pointer to rwlock to unlock (read)
 * @param file_name Source file name (use __FILE__)
 * @param line_number Source line number (use __LINE__)
 * @param function_name Function name (use __func__)
 * @return 0 on success, error code on failure
 * @ingroup lock_debug
 */
int debug_rwlock_rdunlock(rwlock_t *rwlock, const char *file_name, int line_number, const char *function_name);

/**
 * @brief Tracked rwlock write unlock with record cleanup
 * @param rwlock Pointer to rwlock to unlock (write)
 * @param file_name Source file name (use __FILE__)
 * @param line_number Source line number (use __LINE__)
 * @param function_name Function name (use __func__)
 * @return 0 on success, error code on failure
 * @ingroup lock_debug
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

// ============================================================================
// Convenience Macros
// ============================================================================

/**
 * @brief Convenience macro for tracked mutex lock
 * @param mutex Pointer to mutex to lock
 */
#ifdef NDEBUG
#define DEBUG_MUTEX_LOCK(mutex) debug_mutex_lock(mutex, NULL, 0, NULL)
#define DEBUG_MUTEX_UNLOCK(mutex) debug_mutex_unlock(mutex, NULL, 0, NULL)
#define DEBUG_RWLOCK_RDLOCK(rwlock) debug_rwlock_rdlock(rwlock, NULL, 0, NULL)
#define DEBUG_RWLOCK_WRLOCK(rwlock) debug_rwlock_wrlock(rwlock, NULL, 0, NULL)
#else
#define DEBUG_MUTEX_LOCK(mutex) debug_mutex_lock(mutex, __FILE__, __LINE__, __func__)
#define DEBUG_MUTEX_UNLOCK(mutex) debug_mutex_unlock(mutex, __FILE__, __LINE__, __func__)
#define DEBUG_RWLOCK_RDLOCK(rwlock) debug_rwlock_rdlock(rwlock, __FILE__, __LINE__, __func__)
#define DEBUG_RWLOCK_WRLOCK(rwlock) debug_rwlock_wrlock(rwlock, __FILE__, __LINE__, __func__)
#endif

/**
 * @brief Convenience macro for tracked rwlock unlock
 * @param rwlock Pointer to rwlock to unlock
 */

// ============================================================================
// Statistics and Information Functions
// ============================================================================

/**
 * @brief Get current lock statistics
 * @param total_acquired Output: total locks acquired (lifetime)
 * @param total_released Output: total locks released (lifetime)
 * @param currently_held Output: current number of held locks
 * @ingroup lock_debug
 */
void lock_debug_get_stats(uint64_t *total_acquired, uint64_t *total_released, uint32_t *currently_held);

/**
 * @brief Check if lock debugging is initialized
 * @return true if initialized, false otherwise
 * @ingroup lock_debug
 */
bool lock_debug_is_initialized(void);

/**
 * @brief Print current lock debug state for debugging
 * @ingroup lock_debug
 */
void lock_debug_print_state(void);

/**
 * @brief Callback function for printing orphaned release records
 * @param record Orphaned release record pointer
 * @param user_data User data pointer
 * @ingroup lock_debug
 */
void print_orphaned_release_callback(lock_record_t *record, void *user_data);
