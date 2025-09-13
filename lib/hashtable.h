#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "platform/abstraction.h"

/*
 * Simple Hash Table Implementation
 *
 * Optimized for client ID lookup with:
 * - Separate chaining for collision resolution
 * - Power-of-2 bucket count for fast modulo via bit masking
 * - Reader-writer locks for better concurrency
 * - Pre-allocated entry pool to reduce malloc overhead
 */

// Hash table configuration
#define HASHTABLE_BUCKET_COUNT 16 // Must be power of 2, > MAX_CLIENTS (10)
#define HASHTABLE_MAX_ENTRIES 32  // Pool size for pre-allocated entries

// Hash table entry (for collision chaining)
typedef struct hashtable_entry {
  uint32_t key;                 // Client ID
  void *value;                  // Pointer to client_info_t
  struct hashtable_entry *next; // Next entry in chain (for collisions)
  bool in_use;                  // Entry is active
} hashtable_entry_t;

// Hash table structure
typedef struct {
  hashtable_entry_t *buckets[HASHTABLE_BUCKET_COUNT]; // Array of bucket heads
  hashtable_entry_t *entry_pool;                      // Pre-allocated entry pool
  hashtable_entry_t *free_list;                       // Stack of free entries
  size_t entry_count;                                 // Number of active entries
  size_t pool_size;                                   // Size of entry pool
  rwlock_t rwlock;                                    // Reader-writer lock for concurrency

  // Statistics
  uint64_t lookups;    // Total lookup operations
  uint64_t hits;       // Successful lookups
  uint64_t insertions; // Total insertions
  uint64_t deletions;  // Total deletions
  uint64_t collisions; // Number of collisions encountered
} hashtable_t;

// Hash table operations
hashtable_t *hashtable_create(void);
void hashtable_destroy(hashtable_t *ht);

// Core operations (thread-safe)
bool hashtable_insert(hashtable_t *ht, uint32_t key, void *value);
void *hashtable_lookup(hashtable_t *ht, uint32_t key);
bool hashtable_remove(hashtable_t *ht, uint32_t key);
bool hashtable_contains(hashtable_t *ht, uint32_t key);

// Iteration (requires external locking)
typedef void (*hashtable_foreach_fn)(uint32_t key, void *value, void *user_data);
void hashtable_foreach(hashtable_t *ht, hashtable_foreach_fn callback, void *user_data);

// Statistics and debugging
size_t hashtable_size(hashtable_t *ht);
void hashtable_print_stats(hashtable_t *ht, const char *name);
void hashtable_set_stats_enabled(bool enabled); // Control stats printing (for tests)
double hashtable_load_factor(hashtable_t *ht);

// Locking (for external coordination)
void hashtable_read_lock(hashtable_t *ht);
void hashtable_read_unlock(hashtable_t *ht);
void hashtable_write_lock(hashtable_t *ht);
void hashtable_write_unlock(hashtable_t *ht);
