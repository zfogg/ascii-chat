/**
 * @file lib/debug/named.c
 * @brief Lock-free named object registry for debugging
 * @ingroup debug_named
 *
 * Uses atomic operations and lock-free linked lists to avoid deadlocks.
 * No rwlocks, no blocking operations - purely atomic CAS-based registration.
 */

#include "ascii-chat/debug/named.h"
#include <ascii-chat/uthash.h>
#include "ascii-chat/log/log.h"
#include "ascii-chat/util/path.h"
#include "ascii-chat/util/lifecycle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ascii-chat/atomic.h>
#include <inttypes.h>
#include <limits.h>

#ifndef NDEBUG

#define MAX_NAME_LEN 256
#define DESCRIBE_BUFFER_SIZE 768

/**
 * @brief Safe string duplication
 */
static char *safe_strdup(const char *s) {
  if (!s) return NULL;
  size_t len = strlen(s) + 1;
  char *dup = SAFE_MALLOC(len, char *);
  if (dup) strcpy(dup, s);
  return dup;
}

/**
 * @brief Registry entry - linked list node
 */
typedef struct named_entry {
  uintptr_t key;
  char *name;
  char *type;
  char *format_spec;
  char *file;
  int line;
  char *func;
  struct named_entry *next;
} named_entry_t;

/**
 * @brief Global lock-free registry
 */
typedef struct {
  atomic_ptr_t entries_head;
  lifecycle_t lifecycle;
} named_registry_t;

static named_registry_t g_named_registry = {
    .entries_head = {0},
    .lifecycle = LIFECYCLE_INIT,
};

asciichat_error_t named_init(void) {
  if (!lifecycle_init(&g_named_registry.lifecycle, "named_registry")) {
    return ASCIICHAT_OK;
  }
  lifecycle_init_commit(&g_named_registry.lifecycle);
  return ASCIICHAT_OK;
}

void named_destroy(void) {
  if (!lifecycle_shutdown(&g_named_registry.lifecycle)) {
    return;
  }

  named_entry_t *entry = atomic_ptr_load(&g_named_registry.entries_head);
  while (entry) {
    named_entry_t *next = entry->next;
    free(entry->name);
    if (entry->type) SAFE_FREE(entry->type);
    if (entry->format_spec) SAFE_FREE(entry->format_spec);
    if (entry->file) SAFE_FREE(entry->file);
    if (entry->func) SAFE_FREE(entry->func);
    free(entry);
    entry = next;
  }
}

/**
 * @brief Lock-free atomic counters for unique naming
 */
static atomic_t mutex_counter = {0};
static atomic_t rwlock_counter = {0};
static atomic_t cond_counter = {0};
static atomic_t atomic_counter = {0};

const char *named_register(uintptr_t key, const char *base_name, const char *type, const char *format_spec,
                           const char *file, int line, const char *func) {
  if (!base_name || !type || !format_spec) {
    return "?";
  }

  if (!lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    return base_name;
  }

  // Generate unique name using atomic counters (no locks needed)
  uint64_t counter = 0;
  if (strcmp(type, "mutex") == 0) {
    counter = atomic_fetch_add_u64(&mutex_counter, 1);
  } else if (strcmp(type, "rwlock") == 0) {
    counter = atomic_fetch_add_u64(&rwlock_counter, 1);
  } else if (strcmp(type, "cond") == 0) {
    counter = atomic_fetch_add_u64(&cond_counter, 1);
  } else if (strcmp(type, "atomic") == 0) {
    counter = atomic_fetch_add_u64(&atomic_counter, 1);
  }

  char name_buffer[256];
  snprintf(name_buffer, sizeof(name_buffer), "%s.%"PRIu64, base_name, counter);

  // Lock-free registration: prepend to linked list using CAS
  named_entry_t *entry = malloc(sizeof(named_entry_t));
  if (!entry) return base_name;

  entry->key = key;
  entry->name = safe_strdup(name_buffer);
  entry->type = safe_strdup(type);
  entry->format_spec = safe_strdup(format_spec);
  entry->file = NULL;
  entry->line = 0;
  entry->func = NULL;

  // CAS loop to prepend atomically
  named_entry_t *head = atomic_ptr_load(&g_named_registry.entries_head);
  do {
    entry->next = head;
  } while (!atomic_ptr_cas(&g_named_registry.entries_head, &head, entry));

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

  va_list args;
  va_start(args, fmt);
  char *full_name = NULL;
  int ret = vasprintf(&full_name, fmt, args);
  va_end(args);

  if (ret < 0) {
    return "?";
  }

  // Lock-free registration
  named_entry_t *entry = malloc(sizeof(named_entry_t));
  if (!entry) {
    free(full_name);
    return "?";
  }

  entry->key = key;
  entry->name = full_name;
  entry->type = safe_strdup(type);
  entry->format_spec = safe_strdup(format_spec);
  entry->file = file ? safe_strdup(file) : NULL;
  entry->line = line;
  entry->func = func ? safe_strdup(func) : NULL;

  named_entry_t *head = atomic_ptr_load(&g_named_registry.entries_head);
  do {
    entry->next = head;
  } while (!atomic_ptr_cas(&g_named_registry.entries_head, &head, entry));

  return entry->name;
}

void named_unregister(uintptr_t key) {
  // Not implemented - entries stay in list until shutdown
  (void)key;
}

const char *named_update_name(uintptr_t key, const char *new_base_name) {
  (void)key;
  (void)new_base_name;
  return NULL;
}

const char *named_get(uintptr_t key) {
  named_entry_t *entry = atomic_ptr_load(&g_named_registry.entries_head);
  while (entry) {
    if (entry->key == key) return entry->name;
    entry = entry->next;
  }
  return NULL;
}

const char *named_get_type(uintptr_t key) {
  named_entry_t *entry = atomic_ptr_load(&g_named_registry.entries_head);
  while (entry) {
    if (entry->key == key) return entry->type;
    entry = entry->next;
  }
  return NULL;
}

const char *named_get_format_spec(uintptr_t key) {
  named_entry_t *entry = atomic_ptr_load(&g_named_registry.entries_head);
  while (entry) {
    if (entry->key == key) return entry->format_spec;
    entry = entry->next;
  }
  return NULL;
}

const char *named_describe(uintptr_t key, const char *type_hint) {
  if (!type_hint) type_hint = "object";

  static _Thread_local char buffer[DESCRIBE_BUFFER_SIZE];

  named_entry_t *entry = atomic_ptr_load(&g_named_registry.entries_head);
  while (entry) {
    if (entry->key == key) {
      const char *type = entry->type ? entry->type : type_hint;
      const char *name = entry->name ? entry->name : "unknown";
      if (entry->file && entry->func && entry->line > 0) {
        snprintf(buffer, sizeof(buffer), "%s/%s (0x%tx) @ %s:%d:%s()", type, name, (ptrdiff_t)key,
                 entry->file, entry->line, entry->func);
      } else {
        snprintf(buffer, sizeof(buffer), "%s/%s (0x%tx)", type, name, (ptrdiff_t)key);
      }
      return buffer;
    }
    entry = entry->next;
  }

  snprintf(buffer, sizeof(buffer), "%s (0x%tx)", type_hint ? type_hint : "unknown", (ptrdiff_t)key);
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

  // Snapshot entries
  named_iter_entry_t entries[256];
  int count = 0;

  named_entry_t *entry = atomic_ptr_load(&g_named_registry.entries_head);
  while (entry && count < 256) {
    entries[count].key = entry->key;
    strncpy(entries[count].name, entry->name ? entry->name : "?", MAX_NAME_LEN - 1);
    entries[count].name[MAX_NAME_LEN - 1] = '\0';
    count++;
    entry = entry->next;
  }

  // Call callback outside the snapshot loop
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
 *
 * File descriptors are small integers (typically 0-1024), so we use a high bit
 * to distinguish them from actual pointers. We use UINTPTR_MAX - fd to ensure
 * no collision with real pointers (which are typically small on 64-bit systems).
 */
static inline uintptr_t encode_fd_key(int fd) {
  return ((uintptr_t)UINTPTR_MAX - (unsigned int)fd);
}

/**
 * @brief Encode packet type into a unique key namespace
 */
static inline uintptr_t encode_pkt_type_key(int pkt_type) {
  return ((uintptr_t)UINTPTR_MAX - 100000U - (unsigned int)pkt_type);
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

  // Lock-free registration
  named_entry_t *entry = malloc(sizeof(named_entry_t));
  if (!entry) return "?";

  uintptr_t key = encode_fd_key(fd);
  entry->key = key;
  entry->name = safe_strdup(name_buffer);
  entry->type = safe_strdup("fd");
  entry->format_spec = safe_strdup("%d");
  entry->file = file ? safe_strdup(file) : NULL;
  entry->line = line;
  entry->func = func ? safe_strdup(func) : NULL;

  named_entry_t *head = atomic_ptr_load(&g_named_registry.entries_head);
  do {
    entry->next = head;
  } while (!atomic_ptr_cas(&g_named_registry.entries_head, &head, entry));

  return entry->name;
}

const char *named_get_fd(int fd) {
  if (fd < 0) return NULL;

  uintptr_t key = encode_fd_key(fd);
  named_entry_t *entry = atomic_ptr_load(&g_named_registry.entries_head);
  while (entry) {
    if (entry->key == key) return entry->name;
    entry = entry->next;
  }
  return NULL;
}

const char *named_get_fd_format_spec(int fd) {
  if (fd < 0) return NULL;

  uintptr_t key = encode_fd_key(fd);
  named_entry_t *entry = atomic_ptr_load(&g_named_registry.entries_head);
  while (entry) {
    if (entry->key == key) return entry->format_spec;
    entry = entry->next;
  }
  return NULL;
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

  // Lock-free registration
  named_entry_t *entry = malloc(sizeof(named_entry_t));
  if (!entry) return "?";

  uintptr_t key = encode_pkt_type_key(pkt_type);
  entry->key = key;
  entry->name = safe_strdup(name_buffer);
  entry->type = safe_strdup("packet_type");
  entry->format_spec = safe_strdup("%d");
  entry->file = file ? safe_strdup(file) : NULL;
  entry->line = line;
  entry->func = func ? safe_strdup(func) : NULL;

  named_entry_t *head = atomic_ptr_load(&g_named_registry.entries_head);
  do {
    entry->next = head;
  } while (!atomic_ptr_cas(&g_named_registry.entries_head, &head, entry));

  return entry->name;
}

const char *named_get_packet_type(int pkt_type) {
  if (pkt_type < 0) return NULL;

  uintptr_t key = encode_pkt_type_key(pkt_type);
  named_entry_t *entry = atomic_ptr_load(&g_named_registry.entries_head);
  while (entry) {
    if (entry->key == key) return entry->name;
    entry = entry->next;
  }
  return NULL;
}

const char *named_get_packet_type_format_spec(int pkt_type) {
  if (pkt_type < 0) return NULL;

  uintptr_t key = encode_pkt_type_key(pkt_type);
  named_entry_t *entry = atomic_ptr_load(&g_named_registry.entries_head);
  while (entry) {
    if (entry->key == key) return entry->format_spec;
    entry = entry->next;
  }
  return NULL;
}

void named_registry_register_packet_types(void) {
  // No-op
}

#else // Release builds

int named_init(void) { return 0; }
void named_destroy(void) {}
const char *named_register(uintptr_t key, const char *base_name, const char *type, const char *format_spec,
                           const char *file, int line, const char *func) {
  (void)key; (void)type; (void)format_spec; (void)file; (void)line; (void)func;
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
