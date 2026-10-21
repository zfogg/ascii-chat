/**
 * @file lib/debug/named.c
 * @brief Named object registry for debugging
 * @ingroup debug_named
 *
 * Provides a centralized registry for naming any addressable resource.
 * Uses uthash for O(1) lookup by uintptr_t key.
 * Rwlock protects the hash table with minimal critical section.
 */

#include "ascii-chat/debug/named.h"
#include "ascii-chat/platform/rwlock.h"
#include "ascii-chat/platform/string.h"
#include "ascii-chat/platform/thread.h"
#include <ascii-chat/uthash.h>
#include "ascii-chat/log/log.h"
#include "ascii-chat/util/path.h"
#include "ascii-chat/util/lifecycle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

// Hash table growth runs under entries_lock. Memory tracking looks up names
// under the same lock, so registry metadata must use untracked allocations.
#undef uthash_malloc
#undef uthash_free
#define uthash_malloc(sz) malloc(sz)
#define uthash_free(ptr, sz) free(ptr)

#define MAX_NAME_LEN 256
#define DESCRIBE_BUFFER_SIZE 768

/**
 * @brief String duplication using regular malloc (not SAFE_MALLOC)
 *
 * Uses regular malloc/free to avoid lock acquisition in memory tracking
 * system, which can cause deadlocks when called while holding entries_lock.
 */
static char *entry_strdup(const char *s) {
  if (!s)
    return NULL;
  size_t len = strlen(s) + 1;
  char *dup = malloc(len);
  if (dup) {
    asciichat_error_t strcpy_result = SAFE_STRCPY(dup, len, s);
    if (strcpy_result != ASCIICHAT_OK) {
      log_error("Failed to duplicate string: %s", asciichat_error_format(NULL, strcpy_result));
      free(dup);
      return NULL;
    }
  }
  return dup;
}

/**
 * @brief Registry entry for uthash
 */
typedef struct named_entry {
  uintptr_t key;
  uint64_t generation;
  char *name;
  char *type;
  char *format_spec;
  char *file;
  int line;
  char *func;
  UT_hash_handle hh; // uthash handle
} named_entry_t;

/**
 * @brief Global registry with uthash and rwlock
 *
 * Uses uthash for O(1) lookups protected by rwlock.
 * Rwlock is held ONLY during HASH_ADD/HASH_FIND operations.
 * No mutations occur while holding the lock - avoids deadlocks.
 */
typedef struct {
  named_entry_t *entries; // uthash hash table
  rwlock_t entries_lock;  // Protects hash table
  lifecycle_t lifecycle;
} named_registry_t;

static named_registry_t g_named_registry = {
    .entries = NULL,
    .entries_lock = {0},
    .lifecycle = LIFECYCLE_INIT,
};

// Writers publish a revision while holding entries_lock. Readers only load it;
// their snapshots own all display strings and never retain object pointers.
static _Atomic uint64_t g_metadata_revision = 1;
static tls_key_t g_metadata_tls;
static bool g_metadata_tls_ready;

typedef struct {
  uintptr_t key;
  bool valid;
  named_metadata_t metadata;
} named_metadata_entry_t;

typedef struct {
  uint64_t revision;
  size_t count;
  size_t capacity;
  named_metadata_entry_t *entries;
} named_metadata_cache_t;
static _Thread_local named_metadata_cache_t *g_thread_metadata;

static void metadata_cache_destroy(void *data) {
  named_metadata_cache_t *cache = data;
  if (cache) {
    if (g_thread_metadata == cache)
      g_thread_metadata = NULL;
    UNTRACKED_FREE(cache->entries);
    UNTRACKED_FREE(cache);
  }
}

static int metadata_compare(const void *left, const void *right) {
  uintptr_t a = ((const named_metadata_entry_t *)left)->key;
  uintptr_t b = ((const named_metadata_entry_t *)right)->key;
  return (a > b) - (a < b);
}

static bool metadata_copy_entry(const named_entry_t *entry, named_metadata_t *output) {
  const char *name = entry->name ? entry->name : "?";
  const char *type = entry->type ? entry->type : "object";
  const char *format = entry->format_spec ? entry->format_spec : "0x%tx";
  if (strlen(name) >= sizeof(output->name) || strlen(type) >= sizeof(output->type) ||
      strlen(format) >= sizeof(output->format))
    return false;
  memcpy(output->name, name, strlen(name) + 1);
  memcpy(output->type, type, strlen(type) + 1);
  memcpy(output->format, format, strlen(format) + 1);
  return true;
}

static named_metadata_cache_t *metadata_snapshot(void) {
  if (!g_metadata_tls_ready || !lifecycle_is_initialized(&g_named_registry.lifecycle))
    return NULL;
  named_metadata_cache_t *cache = g_thread_metadata;
  if (cache && cache->revision == atomic_load(&g_metadata_revision))
    return cache;
  if (!cache) {
    cache = UNTRACKED_MALLOC(sizeof(*cache), named_metadata_cache_t *);
    if (!cache)
      return NULL;
    memset(cache, 0, sizeof(*cache));
    if (ascii_tls_set(g_metadata_tls, cache) != 0) {
      metadata_cache_destroy(cache);
      return NULL;
    }
    g_thread_metadata = cache;
  }
  bool acquired = false;
  if (rwlock_tryrdlock(&g_named_registry.entries_lock, &acquired) != ASCIICHAT_OK || !acquired)
    return NULL;
  size_t required = HASH_COUNT(g_named_registry.entries);
  rwlock_rdunlock(&g_named_registry.entries_lock);
  if (required > cache->capacity) {
    if (required > SIZE_MAX / sizeof(*cache->entries))
      return NULL;
    named_metadata_entry_t *entries = UNTRACKED_MALLOC(required * sizeof(*entries), named_metadata_entry_t *);
    if (!entries)
      return NULL;
    UNTRACKED_FREE(cache->entries);
    cache->entries = entries;
    cache->capacity = required;
  }
  if (rwlock_tryrdlock(&g_named_registry.entries_lock, &acquired) != ASCIICHAT_OK || !acquired)
    return NULL;
  if (HASH_COUNT(g_named_registry.entries) > cache->capacity) {
    rwlock_rdunlock(&g_named_registry.entries_lock);
    return NULL;
  }
  cache->count = 0;
  for (named_entry_t *entry = g_named_registry.entries; entry; entry = entry->hh.next) {
    // An initializer and owner can register the same address. Keep the latest
    // hash entry, matching HASH_FIND, rather than returning an older alias.
    named_entry_t *latest = NULL;
    HASH_FIND(hh, g_named_registry.entries, &entry->key, sizeof(entry->key), latest);
    if (latest != entry)
      continue;
    named_metadata_entry_t *copy = &cache->entries[cache->count++];
    copy->key = entry->key;
    copy->valid = metadata_copy_entry(entry, &copy->metadata);
  }
  cache->revision = atomic_load(&g_metadata_revision);
  rwlock_rdunlock(&g_named_registry.entries_lock);
  if (cache->count > 1)
    qsort(cache->entries, cache->count, sizeof(*cache->entries), metadata_compare);
  return cache;
}

bool named_metadata_copy(uintptr_t key, named_metadata_t *output) {
  if (!output)
    return false;
  named_metadata_cache_t *cache = metadata_snapshot();
  if (!cache) {
    // Keep names available if TLS/allocation is unavailable. No borrowed
    // strings escape this fallback either.
    if (!lifecycle_is_initialized(&g_named_registry.lifecycle))
      return false;
    bool acquired = false;
    if (rwlock_tryrdlock(&g_named_registry.entries_lock, &acquired) != ASCIICHAT_OK || !acquired)
      return false;
    named_entry_t *entry = NULL;
    HASH_FIND(hh, g_named_registry.entries, &key, sizeof(key), entry);
    bool copied = entry && metadata_copy_entry(entry, output);
    rwlock_rdunlock(&g_named_registry.entries_lock);
    return copied;
  }
  named_metadata_entry_t lookup = {.key = key};
  const named_metadata_entry_t *entry =
      cache->count ? bsearch(&lookup, cache->entries, cache->count, sizeof(*cache->entries), metadata_compare) : NULL;
  if (!entry || !entry->valid)
    return false;
  *output = entry->metadata;
  return true;
}

asciichat_error_t named_init(void) {
  // Idempotent: if already initialized, return success
  if (lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    return ASCIICHAT_OK;
  }

  if (!lifecycle_init(&g_named_registry.lifecycle, "named_registry")) {
    return ASCIICHAT_OK;
  }

  if (rwlock_init(&g_named_registry.entries_lock, "named_registry_lock") != 0) {
    lifecycle_shutdown(&g_named_registry.lifecycle);
    return ASCIICHAT_OK; // Continue even if rwlock init fails
  }

  g_metadata_tls_ready = ascii_tls_key_create(&g_metadata_tls, metadata_cache_destroy) == 0;
  atomic_fetch_add(&g_metadata_revision, 1);
  lifecycle_init_commit(&g_named_registry.lifecycle);
  return ASCIICHAT_OK;
}

void named_destroy(void) {
  if (!lifecycle_shutdown(&g_named_registry.lifecycle))
    return;
  rwlock_wrlock(&g_named_registry.entries_lock);
  named_entry_t *entry, *next;
  HASH_ITER(hh, g_named_registry.entries, entry, next) {
    // Remove each hash node so uthash releases its bucket storage as well.
    // Unregistration is disabled after lifecycle shutdown, including for the
    // registry lock itself; all entries must be freed here.
    HASH_DEL(g_named_registry.entries, entry);
    free(entry->name);
    free(entry->type);
    free(entry->format_spec);
    free(entry->file);
    free(entry->func);
    free(entry);
  }
  g_named_registry.entries_lock.name = NULL;
  atomic_fetch_add(&g_metadata_revision, 1);
  rwlock_wrunlock(&g_named_registry.entries_lock);
  rwlock_destroy(&g_named_registry.entries_lock);
  if (g_metadata_tls_ready) {
    void *cache = g_thread_metadata;
    ascii_tls_set(g_metadata_tls, NULL);
    metadata_cache_destroy(cache);
    ascii_tls_key_delete(g_metadata_tls);
    g_metadata_tls_ready = false;
  }
}

/**
 * @brief Lock-free atomic counters for unique naming
 */
static uint64_t mutex_counter = 0;
static uint64_t rwlock_counter = 0;
static uint64_t cond_counter = 0;
static uint64_t atomic_counter = 0;
static _Atomic uint64_t named_generation = 0;

/**
 * @brief Initialize a named registry entry with all fields
 * @note Assumes entry->name is already set (may be pre-allocated or via entry_strdup)
 */
static void named_entry_init(named_entry_t *entry, const char *type, const char *format_spec, const char *file,
                             int line, const char *func) {
  entry->generation = ++named_generation;
  entry->type = entry_strdup(type);
  entry->format_spec = entry_strdup(format_spec);
  entry->file = file ? entry_strdup(extract_project_relative_path(file)) : NULL;
  entry->line = line;
  entry->func = func ? entry_strdup(func) : NULL;
}

/**
 * @brief Look up a registered name by key (internal helper for parent resolution)
 * @param parent_key The key to look up
 * @return The registered name or NULL if not found
 * @note Caller must hold read lock
 */
static const char *named_lookup_name_unlocked(uintptr_t parent_key) {
  if (parent_key == 0)
    return NULL;
  named_entry_t *entry = NULL;
  HASH_FIND(hh, g_named_registry.entries, &parent_key, sizeof(uintptr_t), entry);
  return entry ? entry->name : NULL;
}

const char *named_register(uintptr_t key, const char *base_name, const char *type, const char *format_spec,
                           const char *file, int line, const char *func, uintptr_t parent_key) {
#ifdef NDEBUG
  // In release builds, skip debug registry and return the base name
  return base_name ? base_name : "?";
#endif

  if (!base_name || !type || !format_spec) {
    return "?";
  }

  if (!lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    return base_name;
  }

  // If parent_key provided, look up parent name and create hierarchical name
  char final_base_name[256];
  if (parent_key != 0) {
    rwlock_rdlock(&g_named_registry.entries_lock);
    const char *parent_name = named_lookup_name_unlocked(parent_key);
    rwlock_rdunlock(&g_named_registry.entries_lock);

    if (parent_name) {
      snprintf(final_base_name, sizeof(final_base_name), "%s#%s", parent_name, base_name);
    } else {
      // Parent not found, fall back to base name
      snprintf(final_base_name, sizeof(final_base_name), "%s", base_name);
    }
  } else {
    snprintf(final_base_name, sizeof(final_base_name), "%s", base_name);
  }

  // Generate unique name using atomic counters (only for non-hierarchical names)
  char name_buffer[256];
  if (parent_key == 0) {
    uint64_t counter = 0;
    if (strcmp(type, "mutex") == 0) {
      counter = __sync_fetch_and_add(&mutex_counter, 1);
    } else if (strcmp(type, "rwlock") == 0) {
      counter = __sync_fetch_and_add(&rwlock_counter, 1);
    } else if (strcmp(type, "cond") == 0) {
      counter = __sync_fetch_and_add(&cond_counter, 1);
    } else if (strcmp(type, "atomic") == 0) {
      counter = __sync_fetch_and_add(&atomic_counter, 1);
    }
    snprintf(name_buffer, sizeof(name_buffer), "%s.%" PRIu64, final_base_name, counter);
  } else {
    // Hierarchical names don't get counters - parent already has one
    snprintf(name_buffer, sizeof(name_buffer), "%s", final_base_name);
  }

  // Allocate entry BEFORE acquiring lock (avoid long critical section)
  named_entry_t *entry = malloc(sizeof(named_entry_t));
  if (!entry)
    return base_name;

  entry->key = key;
  entry->name = entry_strdup(name_buffer);
  named_entry_init(entry, type, format_spec, file, line, func);

  // Only hold lock for the hash table operation
  log_dev("[NAMED_REGISTER_LOCK_1] About to acquire entries_lock for key=%p type=%s", (void *)key, type);
  rwlock_wrlock(&g_named_registry.entries_lock);
  HASH_ADD(hh, g_named_registry.entries, key, sizeof(uintptr_t), entry);
  atomic_fetch_add(&g_metadata_revision, 1);
  rwlock_wrunlock(&g_named_registry.entries_lock);
  log_dev("[NAMED_REGISTER_LOCK_4] ✅ Released entries_lock");

  return entry->name;
}

const char *named_register_fmt(uintptr_t key, const char *type, const char *format_spec, const char *file, int line,
                               const char *func, const char *fmt, ...) {
  if (!type || !format_spec || !fmt) {
    return "?";
  }

  if (!lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    return fmt;
  }

  // Format the name BEFORE acquiring lock
  va_list args;
  va_start(args, fmt);
  char *full_name = NULL;
  int ret = platform_vasprintf(&full_name, fmt, args);
  va_end(args);

  if (ret < 0) {
    return "?";
  }

  // Allocate entry BEFORE acquiring lock
  named_entry_t *entry = malloc(sizeof(named_entry_t));
  if (!entry) {
    free(full_name);
    return "?";
  }

  // Initialize entry (name is pre-allocated as full_name)
  entry->key = key;
  entry->name = full_name;
  named_entry_init(entry, type, format_spec, file, line, func);

  // Only hold lock for the hash table operation
  rwlock_wrlock(&g_named_registry.entries_lock);
  HASH_ADD(hh, g_named_registry.entries, key, sizeof(uintptr_t), entry);
  atomic_fetch_add(&g_metadata_revision, 1);
  rwlock_wrunlock(&g_named_registry.entries_lock);

  return entry->name;
}

void named_unregister(uintptr_t key) {
  if (!lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    return;
  }

  rwlock_wrlock(&g_named_registry.entries_lock);
  named_entry_t *entry = NULL;
  HASH_FIND(hh, g_named_registry.entries, &key, sizeof(uintptr_t), entry);
  bool changed = entry != NULL;
  // A primitive may be registered again with a parent-qualified name. Retire
  // every entry before its owner releases the object.
  while (entry) {
    HASH_DEL(g_named_registry.entries, entry);
    free(entry->name);
    if (entry->type)
      free(entry->type);
    if (entry->format_spec)
      free(entry->format_spec);
    if (entry->file)
      free(entry->file);
    if (entry->func)
      free(entry->func);
    free(entry);
    HASH_FIND(hh, g_named_registry.entries, &key, sizeof(uintptr_t), entry);
  }
  if (changed)
    atomic_fetch_add(&g_metadata_revision, 1);
  rwlock_wrunlock(&g_named_registry.entries_lock);
}

const char *named_update_name(uintptr_t key, const char *new_base_name) {
#ifndef NDEBUG
  if (!new_base_name || !lifecycle_is_initialized(&g_named_registry.lifecycle))
    return NULL;
  char name[MAX_NAME_LEN];
  snprintf(name, sizeof(name), "%s.%" PRIu64, new_base_name, ++named_generation);
  char *replacement = entry_strdup(name);
  if (!replacement)
    return NULL;
  rwlock_wrlock(&g_named_registry.entries_lock);
  named_entry_t *entry = NULL;
  HASH_FIND(hh, g_named_registry.entries, &key, sizeof(key), entry);
  const char *result = NULL;
  if (entry) {
    UNTRACKED_FREE(entry->name);
    entry->name = replacement;
    result = replacement;
    atomic_fetch_add(&g_metadata_revision, 1);
  } else {
    UNTRACKED_FREE(replacement);
  }
  rwlock_wrunlock(&g_named_registry.entries_lock);
  return result;
#else
  (void)key;
  (void)new_base_name;
  return NULL;
#endif
}

const char *named_get(uintptr_t key) {
  if (!lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    return NULL;
  }

  rwlock_rdlock(&g_named_registry.entries_lock);
  named_entry_t *entry = NULL;
  HASH_FIND(hh, g_named_registry.entries, &key, sizeof(uintptr_t), entry);
  const char *result = entry ? entry->name : NULL;
  rwlock_rdunlock(&g_named_registry.entries_lock);
  return result;
}

bool named_registry_has_type(const char *type, size_t length) {
  if (!type || !length)
    return false;
  named_metadata_cache_t *cache = metadata_snapshot();
  if (!cache)
    return false;
  for (size_t i = 0; i < cache->count; ++i) {
    const named_metadata_entry_t *entry = &cache->entries[i];
    if (entry->valid && strlen(entry->metadata.type) == length && memcmp(entry->metadata.type, type, length) == 0)
      return true;
  }
  return false;
}

const char *named_get_type(uintptr_t key) {
  if (!lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    return NULL;
  }

  rwlock_rdlock(&g_named_registry.entries_lock);
  named_entry_t *entry = NULL;
  HASH_FIND(hh, g_named_registry.entries, &key, sizeof(uintptr_t), entry);
  const char *result = entry ? entry->type : NULL;
  rwlock_rdunlock(&g_named_registry.entries_lock);
  return result;
}

const char *named_get_format_spec(uintptr_t key) {
  if (!lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    return NULL;
  }

  rwlock_rdlock(&g_named_registry.entries_lock);
  named_entry_t *entry = NULL;
  HASH_FIND(hh, g_named_registry.entries, &key, sizeof(uintptr_t), entry);
  const char *result = entry ? entry->format_spec : NULL;
  rwlock_rdunlock(&g_named_registry.entries_lock);
  return result;
}

const char *named_describe(uintptr_t key, const char *type_hint) {
  if (!type_hint)
    type_hint = "object";

  static _Thread_local char buffer[DESCRIBE_BUFFER_SIZE];

  if (!lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    snprintf(buffer, sizeof(buffer), "%s (0x%tx)", type_hint, (ptrdiff_t)key);
    return buffer;
  }

  rwlock_rdlock(&g_named_registry.entries_lock);
  named_entry_t *entry = NULL;
  HASH_FIND(hh, g_named_registry.entries, &key, sizeof(uintptr_t), entry);
  rwlock_rdunlock(&g_named_registry.entries_lock);

  if (entry) {
    const char *type = entry->type ? entry->type : type_hint;
    const char *name = entry->name ? entry->name : "unknown";
    if (entry->file && entry->func && entry->line > 0) {
      snprintf(buffer, sizeof(buffer), "%s/%s (0x%tx) @ %s:%d:%s()", type, name, (ptrdiff_t)key, entry->file,
               entry->line, entry->func);
    } else {
      snprintf(buffer, sizeof(buffer), "%s/%s (0x%tx)", type, name, (ptrdiff_t)key);
    }
  } else {
    snprintf(buffer, sizeof(buffer), "%s (0x%tx)", type_hint ? type_hint : "unknown", (ptrdiff_t)key);
  }

  return buffer;
}

const char *named_describe_thread(void *thread) {
  uintptr_t key = thread_to_key((asciichat_thread_t)thread);
  return named_describe(key, "thread");
}

typedef struct {
  uintptr_t key;
  char name[MAX_NAME_LEN];
} named_iter_entry_t;

asciichat_error_t named_registry_for_each(named_iter_callback_t callback, void *user_data, bool *completed) {
  if (!callback || !completed)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Registry iteration requires a callback");
  *completed = false;
  // Logging also enumerates names during startup and teardown.
  if (!lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    *completed = true;
    return ASCIICHAT_OK;
  }
  bool acquired = false;
  asciichat_error_t err = rwlock_tryrdlock(&g_named_registry.entries_lock, &acquired);
  if (err != ASCIICHAT_OK || !acquired)
    return err;
  size_t capacity = HASH_COUNT(g_named_registry.entries);
  rwlock_rdunlock(&g_named_registry.entries_lock);
  if (!capacity) {
    *completed = true;
    return ASCIICHAT_OK;
  }
  if (capacity > SIZE_MAX / sizeof(named_iter_entry_t))
    return SET_ERRNO(ERROR_MEMORY, "Named registry snapshot size overflow");
  // Snapshot metadata must not re-enter the registry through memory tracking.
  named_iter_entry_t *entries = UNTRACKED_MALLOC(capacity * sizeof(*entries), named_iter_entry_t *);
  if (!entries)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate named registry snapshot");
  err = rwlock_tryrdlock(&g_named_registry.entries_lock, &acquired);
  if (err != ASCIICHAT_OK || !acquired) {
    UNTRACKED_FREE(entries);
    return err;
  }
  if (HASH_COUNT(g_named_registry.entries) > capacity) {
    rwlock_rdunlock(&g_named_registry.entries_lock);
    UNTRACKED_FREE(entries);
    return ASCIICHAT_OK;
  }
  size_t count = 0;
  for (named_entry_t *e = g_named_registry.entries; e != NULL; e = e->hh.next) {
    entries[count].key = e->key;
    const char *src = e->name ? e->name : "?";
    size_t src_len = strlen(src);
    if (src_len >= MAX_NAME_LEN)
      src_len = MAX_NAME_LEN - 1;
    memcpy(entries[count].name, src, src_len);
    entries[count].name[src_len] = '\0';
    count++;
  }
  rwlock_rdunlock(&g_named_registry.entries_lock);

  // Call callback outside the lock
  for (size_t i = 0; i < count; i++) {
    callback(entries[i].key, entries[i].name, user_data);
  }
  UNTRACKED_FREE(entries);
  *completed = true;
  return ASCIICHAT_OK;
}

asciichat_error_t named_registry_try_snapshot(size_t capacity, size_t *required, bool *completed,
                                              named_snapshot_fn copy, void *data) {
  if (!required || !completed || !copy)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid protected registry snapshot");
  *completed = false;
  *required = 0;
  if (!lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    *completed = true;
    return ASCIICHAT_OK;
  }
  bool acquired = false;
  asciichat_error_t result = rwlock_tryrdlock(&g_named_registry.entries_lock, &acquired);
  if (result != ASCIICHAT_OK || !acquired)
    return result;
  *required = HASH_COUNT(g_named_registry.entries);
  if (*required <= capacity) {
    for (named_entry_t *e = g_named_registry.entries; e; e = e->hh.next)
      copy(e->key, e->generation, e->name, e->type, e->file, e->line, data);
    *completed = true;
  }
  rwlock_rdunlock(&g_named_registry.entries_lock);
  return ASCIICHAT_OK;
}

bool named_registry_read(uintptr_t key, const char *type, void (*read_object)(uintptr_t, void *), void *user_data) {
  if (!type || !read_object || !lifecycle_is_initialized(&g_named_registry.lifecycle))
    return false;
  bool acquired = false;
  if (rwlock_tryrdlock(&g_named_registry.entries_lock, &acquired) != ASCIICHAT_OK || !acquired)
    return false;
  named_entry_t *entry = NULL;
  HASH_FIND(hh, g_named_registry.entries, &key, sizeof(key), entry);
  bool found = entry && entry->type && strcmp(entry->type, type) == 0;
  if (found)
    read_object(key, user_data);
  rwlock_rdunlock(&g_named_registry.entries_lock);
  return found;
}

const char *named_search_by_type_id(const char *type, void *id) {
  (void)type;
  (void)id;
  return NULL;
}

/**
 * @brief Encode FD into a unique key namespace
 */
static inline uintptr_t encode_fd_key(int fd) {
  return ((uintptr_t)-1 - (unsigned int)fd);
}

/**
 * @brief Encode packet type into a unique key namespace
 */
static inline uintptr_t encode_pkt_type_key(int pkt_type) {
  return ((uintptr_t)-1 - 100000U - (unsigned int)pkt_type);
}

bool named_fd_metadata_copy(int fd, named_metadata_t *output) {
  return fd >= 0 && named_metadata_copy(encode_fd_key(fd), output);
}

bool named_packet_metadata_copy(int packet_type, named_metadata_t *output) {
  return packet_type >= 0 && named_metadata_copy(encode_pkt_type_key(packet_type), output);
}

const char *named_register_fd(int fd, const char *name, const char *file, int line, const char *func) {
  if (fd < 0) {
    return "?";
  }

  if (!lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    return "?";
  }

  char name_buffer[256];
  if (name) {
    snprintf(name_buffer, sizeof(name_buffer), "%s", name);
  } else {
    snprintf(name_buffer, sizeof(name_buffer), "fd=%d", fd);
  }

  // Allocate entry BEFORE acquiring lock
  named_entry_t *entry = malloc(sizeof(named_entry_t));
  if (!entry)
    return "?";

  uintptr_t key = encode_fd_key(fd);
  entry->key = key;
  entry->name = entry_strdup(name_buffer);
  named_entry_init(entry, "fd", "%d", file, line, func);

  // Only hold lock for the hash table operation
  rwlock_wrlock(&g_named_registry.entries_lock);
  HASH_ADD(hh, g_named_registry.entries, key, sizeof(uintptr_t), entry);
  atomic_fetch_add(&g_metadata_revision, 1);
  rwlock_wrunlock(&g_named_registry.entries_lock);

  return entry->name;
}

const char *named_get_fd(int fd) {
  if (fd < 0)
    return NULL;

  if (!lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    return NULL;
  }

  uintptr_t key = encode_fd_key(fd);
  rwlock_rdlock(&g_named_registry.entries_lock);
  named_entry_t *entry = NULL;
  HASH_FIND(hh, g_named_registry.entries, &key, sizeof(uintptr_t), entry);
  const char *result = entry ? entry->name : NULL;
  rwlock_rdunlock(&g_named_registry.entries_lock);
  return result;
}

const char *named_get_fd_format_spec(int fd) {
  if (fd < 0)
    return NULL;

  if (!lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    return NULL;
  }

  uintptr_t key = encode_fd_key(fd);
  rwlock_rdlock(&g_named_registry.entries_lock);
  named_entry_t *entry = NULL;
  HASH_FIND(hh, g_named_registry.entries, &key, sizeof(uintptr_t), entry);
  const char *result = entry ? entry->format_spec : NULL;
  rwlock_rdunlock(&g_named_registry.entries_lock);
  return result;
}

const char *named_register_packet_type(int pkt_type, const char *file, int line, const char *func) {
  if (pkt_type < 0) {
    return "?";
  }

  if (!lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    return "?";
  }

  char name_buffer[256];
  snprintf(name_buffer, sizeof(name_buffer), "PACKET_TYPE=%d", pkt_type);

  // Allocate entry BEFORE acquiring lock
  named_entry_t *entry = malloc(sizeof(named_entry_t));
  if (!entry)
    return "?";

  uintptr_t key = encode_pkt_type_key(pkt_type);
  entry->key = key;
  entry->name = entry_strdup(name_buffer);
  named_entry_init(entry, "packet_type", "%d", file, line, func);

  // Only hold lock for the hash table operation
  rwlock_wrlock(&g_named_registry.entries_lock);
  HASH_ADD(hh, g_named_registry.entries, key, sizeof(uintptr_t), entry);
  atomic_fetch_add(&g_metadata_revision, 1);
  rwlock_wrunlock(&g_named_registry.entries_lock);

  return entry->name;
}

const char *named_get_packet_type(int pkt_type) {
  if (pkt_type < 0)
    return NULL;

  if (!lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    return NULL;
  }

  uintptr_t key = encode_pkt_type_key(pkt_type);
  rwlock_rdlock(&g_named_registry.entries_lock);
  named_entry_t *entry = NULL;
  HASH_FIND(hh, g_named_registry.entries, &key, sizeof(uintptr_t), entry);
  const char *result = entry ? entry->name : NULL;
  rwlock_rdunlock(&g_named_registry.entries_lock);
  return result;
}

const char *named_get_packet_type_format_spec(int pkt_type) {
  if (pkt_type < 0)
    return NULL;

  if (!lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    return NULL;
  }

  uintptr_t key = encode_pkt_type_key(pkt_type);
  rwlock_rdlock(&g_named_registry.entries_lock);
  named_entry_t *entry = NULL;
  HASH_FIND(hh, g_named_registry.entries, &key, sizeof(uintptr_t), entry);
  const char *result = entry ? entry->format_spec : NULL;
  rwlock_rdunlock(&g_named_registry.entries_lock);
  return result;
}

void named_registry_register_packet_types(void) {
  // No-op
}

void named_print_hash_stats(void) {
  if (!lifecycle_is_initialized(&g_named_registry.lifecycle))
    return;
  rwlock_rdlock(&g_named_registry.entries_lock);
  size_t entries = HASH_COUNT(g_named_registry.entries);
  size_t buckets = g_named_registry.entries ? g_named_registry.entries->hh.tbl->num_buckets : 0;
  rwlock_rdunlock(&g_named_registry.entries_lock);
  log_plain("Named registry: entries=%zu buckets=%zu", entries, buckets);
}
