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
      log_error("Failed to duplicate string: %s", asciichat_error_string(strcpy_result));
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
  rwlock_wrunlock(&g_named_registry.entries_lock);
  rwlock_destroy(&g_named_registry.entries_lock);
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
  rwlock_wrunlock(&g_named_registry.entries_lock);
}

const char *named_update_name(uintptr_t key, const char *new_base_name) {
  (void)key;
  (void)new_base_name;
  return NULL;
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
  uintptr_t key = asciichat_thread_to_key((asciichat_thread_t)thread);
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
