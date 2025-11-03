#pragma once

/**
 * @defgroup hashtable Hash Table
 * @ingroup module_data_structures
 *
 * @file hashtable.h
 * @ingroup hashtable
 * @brief High-Performance Hash Table for Client ID Lookup
 *
 * This header provides a specialized hash table implementation optimized
 * for client ID lookup in ascii-chat's multi-client server architecture.
 * The implementation uses separate chaining with reader-writer locks for
 * optimal concurrent access performance.
 *
 * CORE FEATURES:
 * ==============
 * - Separate chaining for collision resolution
 * - Power-of-2 bucket count for fast modulo via bit masking
 * - Reader-writer locks for improved concurrent read performance
 * - Pre-allocated entry pool to eliminate malloc overhead
 * - Thread-safe operations for concurrent access
 * - Comprehensive statistics tracking (lookups, hits, collisions)
 *
 * OPTIMIZATIONS:
 * ==============
 * - Power-of-2 bucket count enables bit masking instead of modulo
 * - Pre-allocated entry pool eliminates runtime allocations
 * - Free list stack provides O(1) allocation/deallocation
 * - Reader-writer locks allow concurrent read operations
 * - Hash function optimized for 32-bit client IDs
 *
 * HASH TABLE CONFIGURATION:
 * ==========================
 * - Bucket Count: 1024 (must be power of 2)
 * - Max Entries: 2048 (pool size for pre-allocated entries)
 * - Load Factor: Optimal range 0.5-1.0 for balanced performance
 *
 * THREAD SAFETY:
 * ==============
 * - All operations are thread-safe
 * - Reader-writer locks enable concurrent reads
 * - Write operations are serialized
 * - Iteration requires external locking
 *
 * STATISTICS:
 * ===========
 * The hash table tracks:
 * - Total lookup operations
 * - Successful lookups (hits)
 * - Total insertions
 * - Total deletions
 * - Collision count
 * - Current load factor
 *
 * These statistics enable performance analysis and capacity planning.
 *
 * @note The hash table is optimized for client ID (uint32_t) keys with
 *       void* values. It's specifically designed for the server's client
 *       management needs but can be used for other uint32_t key scenarios.
 * @note When the entry pool is exhausted, operations will fail gracefully.
 *       Monitor statistics to determine if pool size needs adjustment.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stdint.h>
#include <stdbool.h>
#include "platform/rwlock.h"

// Hash table configuration
#define HASHTABLE_BUCKET_COUNT 1024 // Must be power of 2, increased to reduce collisions
#define HASHTABLE_MAX_ENTRIES 2048  // Pool size for pre-allocated entries (increased for symbol cache)

/**
 * @brief Hash table entry for collision chaining
 *
 * Represents a single key-value mapping in the hash table. Entries form
 * linked lists (chains) within each bucket to handle hash collisions.
 *
 * @note Entries are allocated from a pre-allocated pool, not malloc/free.
 *
 * @note The 'next' pointer forms a linked list of entries in the same bucket
 *       (entries that hash to the same bucket index).
 *
 * @ingroup hashtable
 */
typedef struct hashtable_entry {
  /** @brief Hash key (client ID) */
  uint32_t key;
  /** @brief Value pointer (typically pointer to client_info_t) */
  void *value;
  /** @brief Next entry in collision chain (NULL for last entry) */
  struct hashtable_entry *next;
  /** @brief True if entry is currently in use (active) */
  bool in_use;
} hashtable_entry_t;

/**
 * @brief Hash table structure
 *
 * Implements a thread-safe hash table with separate chaining for collision
 * resolution. Uses reader-writer locks to enable concurrent reads while
 * protecting writes with exclusive locks.
 *
 * HASH TABLE STRUCTURE:
 * - buckets: Array of bucket heads (pointers to first entry in chain)
 * - entry_pool: Pre-allocated array of all entries
 * - free_list: Stack of unused entries (for allocation)
 * - rwlock: Reader-writer lock for thread-safe operations
 *
 * STATISTICS TRACKING:
 * - lookups: Total lookup operations (successful and unsuccessful)
 * - hits: Successful lookups (key found)
 * - insertions: Total key-value pairs inserted
 * - deletions: Total key-value pairs removed
 * - collisions: Number of hash collisions encountered
 *
 * @note Entry pool is pre-allocated as a single array. All entries are
 *       initialized and added to free_list during creation.
 *
 * @note Reader-writer lock allows multiple concurrent readers but exclusive
 *       access for writers (insert/remove operations).
 *
 * @warning Hash table must be locked externally during iteration. Use
 *          hashtable_read_lock() for iteration that doesn't modify the table.
 *
 * @ingroup hashtable
 */
typedef struct hashtable {
  /** @brief Array of bucket heads (pointers to first entry in each bucket) */
  hashtable_entry_t *buckets[HASHTABLE_BUCKET_COUNT];
  /** @brief Pre-allocated array of all entries */
  hashtable_entry_t *entry_pool;
  /** @brief Stack of free entries (LIFO for cache locality) */
  hashtable_entry_t *free_list;
  /** @brief Number of active entries (currently in use) */
  size_t entry_count;
  /** @brief Total size of entry pool (HASHTABLE_MAX_ENTRIES) */
  size_t pool_size;
  /** @brief Reader-writer lock for thread-safe operations */
  rwlock_t rwlock;

  /** @brief Total lookup operations (statistics) */
  uint64_t lookups;
  /** @brief Successful lookups (statistics) */
  uint64_t hits;
  /** @brief Total insertions (statistics) */
  uint64_t insertions;
  /** @brief Total deletions (statistics) */
  uint64_t deletions;
  /** @brief Number of hash collisions encountered (statistics) */
  uint64_t collisions;
} hashtable_t;

/**
 * @name Hash Table Lifecycle Functions
 * @{
 * @ingroup hashtable
 */

/**
 * @brief Create a new hash table
 * @return Pointer to new hash table, or NULL on failure
 *
 * Creates a new hash table with pre-allocated entry pool. All entries
 * are initialized and added to the free list. Reader-writer lock is
 * initialized for thread-safe operations.
 *
 * @note Entry pool is allocated as a single large allocation for better
 *       cache locality and memory efficiency.
 *
 * @ingroup hashtable
 */
hashtable_t *hashtable_create(void);

/**
 * @brief Destroy a hash table and free all resources
 * @param ht Hash table to destroy (can be NULL)
 *
 * Frees the entry pool and hash table structure. All entries are
 * automatically freed (no need to clear table first).
 *
 * @warning Hash table must not be in use by any threads when destroyed.
 *
 * @ingroup hashtable
 */
void hashtable_destroy(hashtable_t *ht);

/** @} */

/**
 * @name Core Hash Table Operations
 * @{
 * @ingroup hashtable
 */

/**
 * @brief Insert a key-value pair into the hash table
 * @param ht Hash table
 * @param key Hash key (client ID)
 * @param value Value pointer (typically client_info_t pointer)
 * @return true on success, false on failure (pool exhausted or duplicate key)
 *
 * Inserts a new key-value mapping. If key already exists, insertion fails
 * (table is unchanged). Entry is allocated from the free list.
 *
 * @note Thread-safe (acquires write lock internally).
 *
 * @note If entry pool is exhausted, insertion fails. Table never falls
 *       back to malloc (designed for bounded memory usage).
 *
 * @warning Duplicate keys are rejected (insertion fails). Use hashtable_contains()
 *          to check before insertion if needed.
 *
 * @ingroup hashtable
 */
bool hashtable_insert(hashtable_t *ht, uint32_t key, void *value);

/**
 * @brief Lookup a value by key
 * @param ht Hash table
 * @param key Hash key (client ID)
 * @return Value pointer if key found, NULL if not found
 *
 * Performs hash table lookup. Returns the value associated with the key,
 * or NULL if key is not present in the table.
 *
 * @note Thread-safe (acquires read lock internally).
 *
 * @note Statistics are updated (lookups and hits counters).
 *
 * @ingroup hashtable
 */
void *hashtable_lookup(hashtable_t *ht, uint32_t key);

/**
 * @brief Remove a key-value pair from the hash table
 * @param ht Hash table
 * @param key Hash key (client ID)
 * @return true if key was found and removed, false if key not found
 *
 * Removes the key-value mapping. Entry is returned to the free list
 * for reuse. If key is not present, operation has no effect.
 *
 * @note Thread-safe (acquires write lock internally).
 *
 * @note Value pointer is not freed (caller responsible for cleanup if needed).
 *
 * @ingroup hashtable
 */
bool hashtable_remove(hashtable_t *ht, uint32_t key);

/**
 * @brief Check if hash table contains a key
 * @param ht Hash table
 * @param key Hash key (client ID)
 * @return true if key exists, false otherwise
 *
 * Checks for key existence without returning the value. More efficient
 * than lookup if only existence check is needed.
 *
 * @note Thread-safe (acquires read lock internally).
 *
 * @note Statistics are updated (lookups counter, but not hits).
 *
 * @ingroup hashtable
 */
bool hashtable_contains(hashtable_t *ht, uint32_t key);

/** @} */

/**
 * @name Iteration Functions
 * @{
 * @ingroup hashtable
 */

/**
 * @brief Callback function type for hashtable iteration
 * @param key Hash key
 * @param value Value pointer
 * @param user_data User-provided context data
 *
 * Function called for each key-value pair during iteration. Can modify
 * the value pointer if needed (but should not modify the table structure).
 *
 * @ingroup hashtable
 */
typedef void (*hashtable_foreach_fn)(uint32_t key, void *value, void *user_data);

/**
 * @brief Iterate over all key-value pairs in the hash table
 * @param ht Hash table
 * @param callback Function called for each pair
 * @param user_data User context data passed to callback
 *
 * Iterates over all entries in the hash table, calling the callback
 * function for each key-value pair.
 *
 * @note Thread-safe (acquires read lock internally).
 *
 * @warning Callback must not modify the hash table structure (insert/remove).
 *          Only modifications to value pointers are safe.
 *
 * @ingroup hashtable
 */
void hashtable_foreach(hashtable_t *ht, hashtable_foreach_fn callback, void *user_data);

/** @} */

/**
 * @name Statistics and Debugging Functions
 * @{
 * @ingroup hashtable
 */

/**
 * @brief Get number of entries in hash table
 * @param ht Hash table
 * @return Number of active entries
 *
 * Returns the current number of key-value pairs in the table.
 * This is a snapshot and may change immediately after return.
 *
 * @note Thread-safe (acquires read lock internally).
 *
 * @ingroup hashtable
 */
size_t hashtable_size(hashtable_t *ht);

/**
 * @brief Print hash table statistics to stdout
 * @param ht Hash table
 * @param name Descriptive name for this table (for logging)
 *
 * Prints comprehensive statistics including:
 * - Entry count and pool size
 * - Load factor
 * - Lookup statistics (hits, misses, hit rate)
 * - Insertion/deletion counts
 * - Collision statistics
 *
 * @note Thread-safe (acquires read lock internally).
 *
 * @ingroup hashtable
 */
void hashtable_print_stats(hashtable_t *ht, const char *name);

/**
 * @brief Enable or disable statistics printing
 * @param enabled true to enable stats printing, false to disable
 *
 * Controls whether hashtable_print_stats() actually prints output.
 * Useful for test suites that want to suppress output.
 *
 * @ingroup hashtable
 */
void hashtable_set_stats_enabled(bool enabled);

/**
 * @brief Calculate hash table load factor
 * @param ht Hash table
 * @return Load factor (entries / buckets) as double
 *
 * Load factor indicates how full the hash table is. Lower values
 * (closer to 0) indicate better performance (fewer collisions).
 *
 * @note Thread-safe (acquires read lock internally).
 *
 * @note Optimal load factor is typically between 0.5 and 1.0.
 *
 * @ingroup hashtable
 */
double hashtable_load_factor(hashtable_t *ht);

/** @} */

/**
 * @name External Locking Functions
 * @{
 * @ingroup hashtable
 */

/**
 * @brief Acquire read lock on hash table
 * @param ht Hash table
 *
 * Acquires a read lock, allowing concurrent read operations. Must be
 * paired with hashtable_read_unlock().
 *
 * @note Use this for external iteration or multiple lookups that should
 *       be atomic as a group.
 *
 * @warning Lock ordering: Must acquire read lock before any per-entry locks.
 *
 * @ingroup hashtable
 */
void hashtable_read_lock(hashtable_t *ht);

/**
 * @brief Release read lock on hash table
 * @param ht Hash table
 *
 * Releases a previously acquired read lock. Must be paired with
 * hashtable_read_lock().
 *
 * @ingroup hashtable
 */
void hashtable_read_unlock(hashtable_t *ht);

/**
 * @brief Acquire write lock on hash table
 * @param ht Hash table
 *
 * Acquires an exclusive write lock, blocking all readers and writers.
 * Must be paired with hashtable_write_unlock().
 *
 * @note Use this for external operations that need exclusive access,
 *       such as batch modifications or table restructuring.
 *
 * @warning Lock ordering: Must acquire write lock before any per-entry locks.
 *
 * @ingroup hashtable
 */
void hashtable_write_lock(hashtable_t *ht);

/**
 * @brief Release write lock on hash table
 * @param ht Hash table
 *
 * Releases a previously acquired write lock. Must be paired with
 * hashtable_write_lock().
 *
 * @ingroup hashtable
 */
void hashtable_write_unlock(hashtable_t *ht);

/** @} */
