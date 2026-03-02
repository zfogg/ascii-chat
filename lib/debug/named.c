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
#include <ascii-chat/uthash.h>
#include "ascii-chat/log/log.h"
#include "ascii-chat/util/path.h"
#include "ascii-chat/util/lifecycle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#ifndef NDEBUG

#define MAX_NAME_LEN 256
#define DESCRIBE_BUFFER_SIZE 768

/**
 * @brief String duplication using regular malloc (not SAFE_MALLOC)
 *
 * Uses regular malloc/free to avoid lock acquisition in memory tracking
 * system, which can cause deadlocks when called while holding entries_lock.
 */
static char *entry_strdup(const char *s) {
  if (!s) return NULL;
  size_t len = strlen(s) + 1;
  char *dup = malloc(len);
  if (dup) strcpy(dup, s);
  return dup;
}

/**
 * @brief Registry entry for uthash
 */
typedef struct named_entry {
  uintptr_t key;
  char *name;
  char *type;
  char *format_spec;
  char *file;
  int line;
  char *func;
  UT_hash_handle hh;  // uthash handle
} named_entry_t;

/**
 * @brief Global registry with uthash and rwlock
 *
 * Uses uthash for O(1) lookups protected by rwlock.
 * Rwlock is held ONLY during HASH_ADD/HASH_FIND operations.
 * No mutations occur while holding the lock - avoids deadlocks.
 */
typedef struct {
  named_entry_t *entries;  // uthash hash table
  rwlock_t entries_lock;   // Protects hash table
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
    return ASCIICHAT_OK;  // Continue even if rwlock init fails
  }

  lifecycle_init_commit(&g_named_registry.lifecycle);
  return ASCIICHAT_OK;
}

void named_destroy(void) {
  if (!lifecycle_shutdown(&g_named_registry.lifecycle)) {
    return;
  }

  rwlock_wrlock(&g_named_registry.entries_lock);
  for (named_entry_t *e = g_named_registry.entries; e != NULL;) {
    named_entry_t *next = e->hh.next;
    free(e->name);
    if (e->type) free(e->type);
    if (e->format_spec) free(e->format_spec);
    if (e->file) free(e->file);
    if (e->func) free(e->func);
    free(e);
    e = next;
  }
  g_named_registry.entries = NULL;
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

/**
 * @brief Look up a registered name by key (internal helper for parent resolution)
 * @param parent_key The key to look up
 * @return The registered name or NULL if not found
 * @note Caller must hold read lock
 */
static const char *named_lookup_name_unlocked(uintptr_t parent_key) {
  if (parent_key == 0) return NULL;
  named_entry_t *entry = NULL;
  HASH_FIND(hh, g_named_registry.entries, &parent_key, sizeof(uintptr_t), entry);
  return entry ? entry->name : NULL;
}

const char *named_register(uintptr_t key, const char *base_name, const char *type, const char *format_spec,
                           const char *file, int line, const char *func, uintptr_t parent_key) {
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
    snprintf(name_buffer, sizeof(name_buffer), "%s.%"PRIu64, final_base_name, counter);
  } else {
    // Hierarchical names don't get counters - parent already has one
    snprintf(name_buffer, sizeof(name_buffer), "%s", final_base_name);
  }

  // Allocate entry BEFORE acquiring lock (avoid long critical section)
  named_entry_t *entry = malloc(sizeof(named_entry_t));
  if (!entry) return base_name;

  entry->key = key;
  entry->name = entry_strdup(name_buffer);
  entry->type = entry_strdup(type);
  entry->format_spec = entry_strdup(format_spec);
  entry->file = file ? entry_strdup(file) : NULL;
  entry->line = line;
  entry->func = func ? entry_strdup(func) : NULL;

  // Only hold lock for the hash table operation
  rwlock_wrlock(&g_named_registry.entries_lock);
  HASH_ADD(hh, g_named_registry.entries, key, sizeof(uintptr_t), entry);
  rwlock_wrunlock(&g_named_registry.entries_lock);

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
  int ret = vasprintf(&full_name, fmt, args);
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

  entry->key = key;
  entry->name = full_name;
  entry->type = entry_strdup(type);
  entry->format_spec = entry_strdup(format_spec);
  entry->file = file ? entry_strdup(file) : NULL;
  entry->line = line;
  entry->func = func ? entry_strdup(func) : NULL;

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
  if (entry) {
    HASH_DEL(g_named_registry.entries, entry);
    free(entry->name);
    if (entry->type) free(entry->type);
    if (entry->format_spec) free(entry->format_spec);
    if (entry->file) free(entry->file);
    if (entry->func) free(entry->func);
    free(entry);
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
  if (!type_hint) type_hint = "object";

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
      snprintf(buffer, sizeof(buffer), "%s/%s (0x%tx) @ %s:%d:%s()", type, name, (ptrdiff_t)key,
               entry->file, entry->line, entry->func);
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

void named_registry_for_each(named_iter_callback_t callback, void *user_data) {
  if (!callback || !lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    return;
  }

  // Snapshot entries while holding read lock
  named_iter_entry_t entries[256];
  int count = 0;

  rwlock_rdlock(&g_named_registry.entries_lock);
  for (named_entry_t *e = g_named_registry.entries; e != NULL && count < 256; e = e->hh.next) {
    entries[count].key = e->key;
    strncpy(entries[count].name, e->name ? e->name : "?", MAX_NAME_LEN - 1);
    entries[count].name[MAX_NAME_LEN - 1] = '\0';
    count++;
  }
  rwlock_rdunlock(&g_named_registry.entries_lock);

  // Call callback outside the lock
  for (int i = 0; i < count; i++) {
    callback(entries[i].key, entries[i].name, user_data);
  }
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

const char *named_register_fd(int fd, const char *file, int line, const char *func) {
  if (fd < 0) {
    return "?";
  }

  if (!lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    return "?";
  }

  char name_buffer[256];
  snprintf(name_buffer, sizeof(name_buffer), "fd=%d", fd);

  // Allocate entry BEFORE acquiring lock
  named_entry_t *entry = malloc(sizeof(named_entry_t));
  if (!entry) return "?";

  uintptr_t key = encode_fd_key(fd);
  entry->key = key;
  entry->name = entry_strdup(name_buffer);
  entry->type = entry_strdup("fd");
  entry->format_spec = entry_strdup("%d");
  entry->file = file ? entry_strdup(file) : NULL;
  entry->line = line;
  entry->func = func ? entry_strdup(func) : NULL;

  // Only hold lock for the hash table operation
  rwlock_wrlock(&g_named_registry.entries_lock);
  HASH_ADD(hh, g_named_registry.entries, key, sizeof(uintptr_t), entry);
  rwlock_wrunlock(&g_named_registry.entries_lock);

  return entry->name;
}

const char *named_get_fd(int fd) {
  if (fd < 0) return NULL;

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
  if (fd < 0) return NULL;

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
  if (!entry) return "?";

  uintptr_t key = encode_pkt_type_key(pkt_type);
  entry->key = key;
  entry->name = entry_strdup(name_buffer);
  entry->type = entry_strdup("packet_type");
  entry->format_spec = entry_strdup("%d");
  entry->file = file ? entry_strdup(file) : NULL;
  entry->line = line;
  entry->func = func ? entry_strdup(func) : NULL;

  // Only hold lock for the hash table operation
  rwlock_wrlock(&g_named_registry.entries_lock);
  HASH_ADD(hh, g_named_registry.entries, key, sizeof(uintptr_t), entry);
  rwlock_wrunlock(&g_named_registry.entries_lock);

  return entry->name;
}

const char *named_get_packet_type(int pkt_type) {
  if (pkt_type < 0) return NULL;

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
  if (pkt_type < 0) return NULL;

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

#else // Release builds

asciichat_error_t named_init(void) { return ASCIICHAT_OK; }
void named_destroy(void) {}
const char *named_register(uintptr_t key, const char *base_name, const char *type, const char *format_spec,
                           const char *file, int line, const char *func, uintptr_t parent_key) {
  (void)key; (void)type; (void)format_spec; (void)file; (void)line; (void)func; (void)parent_key;
  return base_name ? base_name : "?";
}
const char *named_register_fmt(uintptr_t key, const char *type, const char *format_spec, const char *file, int line,
                               const char *func, const char *fmt, ...) {
  (void)key; (void)type; (void)format_spec; (void)file; (void)line; (void)func;
  return fmt ? fmt : "?";
}
void named_unregister(uintptr_t key) { (void)key; }
const char *named_update_name(uintptr_t key, const char *new_base_name) { (void)key; (void)new_base_name; return NULL; }
const char *named_get(uintptr_t key) { (void)key; return NULL; }
const char *named_get_type(uintptr_t key) { (void)key; return NULL; }
const char *named_get_format_spec(uintptr_t key) { (void)key; return NULL; }
const char *named_describe(uintptr_t key, const char *type_hint) { return type_hint ? type_hint : "object"; }
const char *named_describe_thread(void *thread) { (void)thread; return "thread"; }
void named_registry_for_each(named_iter_callback_t callback, void *user_data) { (void)callback; (void)user_data; }
const char *named_search_by_type_id(const char *type, void *id) { (void)type; (void)id; return NULL; }
const char *named_register_fd(int fd, const char *file, int line, const char *func) { (void)fd; (void)file; (void)line; (void)func; return "?"; }
const char *named_get_fd(int fd) { (void)fd; return NULL; }
const char *named_get_fd_format_spec(int fd) { (void)fd; return NULL; }
const char *named_register_packet_type(int pkt_type, const char *file, int line, const char *func) { (void)pkt_type; (void)file; (void)line; (void)func; return "?"; }
const char *named_get_packet_type(int pkt_type) { (void)pkt_type; return NULL; }
const char *named_get_packet_type_format_spec(int pkt_type) { (void)pkt_type; return NULL; }
void named_registry_register_packet_types(void) {}

#endif
