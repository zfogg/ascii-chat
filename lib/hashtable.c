#include "hashtable.h"
#include "common.h"
#include "asciichat_errno.h" // For asciichat_errno system
#include <stdlib.h>
#include <string.h>

// Global flag to disable hashtable stats printing (for tests)
static bool g_disable_hashtable_stats = false;

// Function to control hashtable stats printing (for tests)
void hashtable_set_stats_enabled(bool enabled) {
  g_disable_hashtable_stats = !enabled;
}

/* ============================================================================
 * Hash Function
 * ============================================================================ */

// Simple hash function for 32-bit integers (client IDs)
// Using FNV-1a hash which has good distribution properties
static inline uint32_t hash_uint32(uint32_t key) {
  // Use 64-bit arithmetic to avoid overflow, then mask to 32-bit
  uint64_t hash = 2166136261ULL;          // FNV-1a 32-bit offset basis
  const uint64_t fnv_prime = 16777619ULL; // FNV-1a 32-bit prime

  // Hash each byte of the key
  hash ^= (key & 0xFF);
  hash = (hash * fnv_prime) & 0xFFFFFFFFULL;

  hash ^= ((key >> 8) & 0xFF);
  hash = (hash * fnv_prime) & 0xFFFFFFFFULL;

  hash ^= ((key >> 16) & 0xFF);
  hash = (hash * fnv_prime) & 0xFFFFFFFFULL;

  hash ^= ((key >> 24) & 0xFF);
  hash = (hash * fnv_prime) & 0xFFFFFFFFULL;

  // Use bit masking instead of modulo for power-of-2 bucket counts
  return (uint32_t)hash & (HASHTABLE_BUCKET_COUNT - 1);
}

/* ============================================================================
 * Hash Table Implementation
 * ============================================================================ */

hashtable_t *hashtable_create(void) {
  hashtable_t *ht;
  ht = SAFE_MALLOC(sizeof(hashtable_t), hashtable_t *);

  // Initialize buckets to NULL
  SAFE_MEMSET((void *)ht->buckets, sizeof(ht->buckets), 0, sizeof(ht->buckets));

  // Pre-allocate entry pool
  ht->pool_size = HASHTABLE_MAX_ENTRIES;
  ht->entry_pool = SAFE_MALLOC(sizeof(hashtable_entry_t) * ht->pool_size, hashtable_entry_t *);

  // Initialize free list (stack of free entries)
  ht->free_list = NULL;
  for (size_t i = 0; i < ht->pool_size; i++) {
    hashtable_entry_t *entry = &ht->entry_pool[i];
    entry->in_use = false;
    entry->next = ht->free_list;
    ht->free_list = entry;
  }

  ht->entry_count = 0;

  // Initialize reader-writer lock
  if (rwlock_init(&ht->rwlock) != 0) {
    SET_ERRNO(ERROR_THREAD, "Failed to initialize hashtable rwlock");
    SAFE_FREE(ht->entry_pool);
    SAFE_FREE(ht);
    return NULL;
  }

  // Initialize statistics
  ht->lookups = 0;
  ht->hits = 0;
  ht->insertions = 0;
  ht->deletions = 0;
  ht->collisions = 0;

  return ht;
}

void hashtable_destroy(hashtable_t *ht) {
  if (!ht)
    return;

  // Print final statistics
  hashtable_print_stats(ht, "Final");

  rwlock_destroy(&ht->rwlock);
  SAFE_FREE(ht->entry_pool);
  SAFE_FREE(ht);
}

// Get a free entry from the pool (must be called with write lock held)
static hashtable_entry_t *get_free_entry(hashtable_t *ht) {
  if (!ht->free_list) {
    return NULL; // Pool exhausted
  }

  hashtable_entry_t *entry = ht->free_list;
  ht->free_list = entry->next;

  entry->next = NULL;
  entry->in_use = true;

  return entry;
}

// Return an entry to the pool (must be called with write lock held)
static void return_entry_to_pool(hashtable_t *ht, hashtable_entry_t *entry) {
  entry->key = 0;
  entry->value = NULL;
  entry->in_use = false;
  entry->next = ht->free_list;
  ht->free_list = entry;
}

bool hashtable_insert(hashtable_t *ht, uint32_t key, void *value) {
  if (!ht || !value || key == 0) {
    return false; // Invalid parameters (key 0 is reserved)
  }

  uint32_t bucket_idx = hash_uint32(key);
  rwlock_wrlock(&ht->rwlock);

  // Check if key already exists
  hashtable_entry_t *existing = ht->buckets[bucket_idx];
  while (existing) {
    if (existing->key == key) {
      // Update existing value
      existing->value = value;
      rwlock_wrunlock(&ht->rwlock);
      return true;
    }
    existing = existing->next;
  }

  // Get a free entry from pool
  hashtable_entry_t *entry = get_free_entry(ht);
  if (!entry) {
    SET_ERRNO(ERROR_MEMORY, "Hashtable entry pool exhausted");
    rwlock_wrunlock(&ht->rwlock);
    return false;
  }

  // Initialize new entry
  entry->key = key;
  entry->value = value;

  // Insert at head of bucket chain
  entry->next = ht->buckets[bucket_idx];
  if (ht->buckets[bucket_idx]) {
    ht->collisions++; // Count collision
  }
  ht->buckets[bucket_idx] = entry;

  ht->entry_count++;
  ht->insertions++;

  rwlock_wrunlock(&ht->rwlock);

  return true;
}

void *hashtable_lookup(hashtable_t *ht, uint32_t key) {
  if (!ht || key == 0) {
    return NULL;
  }

  uint32_t bucket_idx = hash_uint32(key);

  rwlock_rdlock(&ht->rwlock);

  ht->lookups++;

  hashtable_entry_t *entry = ht->buckets[bucket_idx];
  while (entry) {
    if (entry->key == key) {
      void *value = entry->value;
      ht->hits++;
      rwlock_rdunlock(&ht->rwlock);
      return value;
    }
    entry = entry->next;
  }

  rwlock_rdunlock(&ht->rwlock);
  return NULL; // Not found
}

bool hashtable_remove(hashtable_t *ht, uint32_t key) {
  if (!ht || key == 0) {
    return false;
  }

  uint32_t bucket_idx = hash_uint32(key);

  rwlock_wrlock(&ht->rwlock);

  hashtable_entry_t *entry = ht->buckets[bucket_idx];
  hashtable_entry_t *prev = NULL;

  while (entry) {
    if (entry->key == key) {
      // Remove from chain
      if (prev) {
        prev->next = entry->next;
      } else {
        ht->buckets[bucket_idx] = entry->next;
      }

      // Return entry to pool
      return_entry_to_pool(ht, entry);

      ht->entry_count--;
      ht->deletions++;

      rwlock_wrunlock(&ht->rwlock);

      return true;
    }

    prev = entry;
    entry = entry->next;
  }

  rwlock_wrunlock(&ht->rwlock);
  return false; // Not found
}

bool hashtable_contains(hashtable_t *ht, uint32_t key) {
  return hashtable_lookup(ht, key) != NULL;
}

void hashtable_foreach(hashtable_t *ht, hashtable_foreach_fn callback, void *user_data) {
  if (!ht || !callback)
    return;

  // NOTE: This requires external locking for thread safety
  // The caller should hold a read or write lock

  for (size_t i = 0; i < HASHTABLE_BUCKET_COUNT; i++) {
    hashtable_entry_t *entry = ht->buckets[i];
    while (entry) {
      if (entry->in_use) {
        callback(entry->key, entry->value, user_data);
      }
      entry = entry->next;
    }
  }
}

size_t hashtable_size(hashtable_t *ht) {
  if (!ht)
    return 0;

  rwlock_rdlock(&ht->rwlock);
  size_t size = ht->entry_count;
  rwlock_rdunlock(&ht->rwlock);

  return size;
}

double hashtable_load_factor(hashtable_t *ht) {
  if (!ht)
    return 0.0;
  return (double)ht->entry_count / (double)HASHTABLE_BUCKET_COUNT;
}

void hashtable_print_stats(hashtable_t *ht, const char *name) {
  if (!ht)
    return;

  // Check if stats printing is disabled (for tests)
  if (g_disable_hashtable_stats) {
    return;
  }

  rwlock_rdlock(&ht->rwlock);

  double hit_rate = (ht->lookups > 0) ? ((double)ht->hits * 100.0 / (double)ht->lookups) : 0.0;
  double load_factor = (double)ht->entry_count / (double)HASHTABLE_BUCKET_COUNT;
  size_t free_entries = ht->pool_size - ht->entry_count;

  log_info("=== Hashtable Stats: %s ===", name ? name : "Unknown");
  log_info("Size: %zu/%zu entries, Load factor: %.2f, Free: %zu", ht->entry_count, ht->pool_size, load_factor,
           free_entries);
  log_info("Operations: %llu lookups (%.1f%% hit rate), %llu insertions, %llu deletions",
           (unsigned long long)ht->lookups, hit_rate, (unsigned long long)ht->insertions,
           (unsigned long long)ht->deletions);
  log_info("Collisions: %llu", (unsigned long long)ht->collisions);

  rwlock_rdunlock(&ht->rwlock);
}

// Locking functions for external coordination
void hashtable_read_lock(hashtable_t *ht) {
  if (ht)
    rwlock_rdlock(&ht->rwlock);
}

void hashtable_read_unlock(hashtable_t *ht) {
  if (ht)
    rwlock_rdunlock(&ht->rwlock);
}

void hashtable_write_lock(hashtable_t *ht) {
  if (ht)
    rwlock_wrlock(&ht->rwlock);
}

void hashtable_write_unlock(hashtable_t *ht) {
  if (ht)
    rwlock_wrunlock(&ht->rwlock);
}
