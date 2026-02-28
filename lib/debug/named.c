/**
 * @file lib/debug/named.c
 * @brief Named object registry implementation for debugging
 * @ingroup debug_named
 *
 * Provides a centralized registry for naming any addressable resource.
 * Uses uthash for O(1) lookup by uintptr_t key.
 */

#include "ascii-chat/debug/named.h"
#include "ascii-chat/platform/rwlock.h"
#include <ascii-chat/uthash.h>
#include "ascii-chat/log/logging.h"
#include "ascii-chat/util/path.h"
#include "ascii-chat/util/lifecycle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <threads.h>

#ifndef NDEBUG

// ============================================================================
// Constants
// ============================================================================

#define MAX_NAME_LEN 256
#define DESCRIBE_BUFFER_SIZE 768 // Increased for file:line:func info

// ============================================================================
// Types
// ============================================================================

/**
 * @brief Per-name counter entry
 */
typedef struct name_counter_entry {
  char *base_name;          // The base name (e.g., "media_pause")
  _Atomic uint64_t counter; // Per-name counter
  UT_hash_handle hh;        // uthash handle
} name_counter_entry_t;

/**
 * @brief Registry entry for a named object
 */
typedef struct named_entry {
  uintptr_t key;     // Registry lookup key
  char *name;        // Allocated name string (e.g., "recv.7")
  char *type;        // Data type label (e.g., "mutex", "socket") (allocated)
  char *format_spec; // Format specifier for printing (e.g., "0x%tx") (allocated)
  char *file;        // Source file where registered (allocated)
  int line;          // Source line where registered
  char *func;        // Function where registered (allocated)
  UT_hash_handle hh; // uthash handle
} named_entry_t;

/**
 * @brief Global named object registry state
 */
typedef struct {
  named_entry_t *entries;              // uthash hash table (NULL = empty)
  name_counter_entry_t *name_counters; // Per-name counter registry
  rwlock_t entries_lock;               // Thread-safe access to uthash
  lifecycle_t lifecycle;               // Init/shutdown state machine
} named_registry_t;

// ============================================================================
// Global State
// ============================================================================

static named_registry_t g_named_registry = {
    .entries = NULL,
    .name_counters = NULL,
    .entries_lock = {0},
    .lifecycle = LIFECYCLE_INIT,
};

// ============================================================================
// Public API Implementation
// ============================================================================

int named_init(void) {
  if (!lifecycle_init(&g_named_registry.lifecycle, "named_registry")) {
    return 0; // Already initialized
  }

  int err = rwlock_init_impl(&g_named_registry.entries_lock);
  if (err != 0) {
    log_error("named_init: rwlock_init_impl failed: %d", err);
    lifecycle_init_abort(&g_named_registry.lifecycle);
    return err;
  }

  lifecycle_init_commit(&g_named_registry.lifecycle);
  return 0;
}

void named_destroy(void) {
  if (!lifecycle_shutdown(&g_named_registry.lifecycle)) {
    return;
  }

  rwlock_wrlock_impl(&g_named_registry.entries_lock);

  // Free all entries
  named_entry_t *entry, *tmp;
  HASH_ITER(hh, g_named_registry.entries, entry, tmp) {
    HASH_DEL(g_named_registry.entries, entry);
    free(entry->name);
    free(entry->type);
    free(entry->format_spec);
    free(entry->file);
    free(entry->func);
    free(entry);
  }

  // Free all per-name counters
  name_counter_entry_t *counter_entry, *counter_tmp;
  HASH_ITER(hh, g_named_registry.name_counters, counter_entry, counter_tmp) {
    HASH_DEL(g_named_registry.name_counters, counter_entry);
    free(counter_entry->base_name);
    free(counter_entry);
  }

  rwlock_wrunlock_impl(&g_named_registry.entries_lock);
  rwlock_destroy_impl(&g_named_registry.entries_lock);
}

const char *named_register(uintptr_t key, const char *base_name, const char *type, const char *format_spec,
                           const char *file, int line, const char *func) {
  if (!base_name || !type || !format_spec) {
    return "?";
  }

  if (!lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    return base_name;
  }

  // Get or create per-name counter
  rwlock_wrlock_impl(&g_named_registry.entries_lock);

  name_counter_entry_t *counter_entry;
  HASH_FIND_STR(g_named_registry.name_counters, base_name, counter_entry);

  if (!counter_entry) {
    // Create new per-name counter entry
    counter_entry = malloc(sizeof(name_counter_entry_t));
    if (!counter_entry) {
      log_error("named_register: malloc failed for counter_entry");
      rwlock_wrunlock_impl(&g_named_registry.entries_lock);
      return base_name;
    }
    counter_entry->base_name = strdup(base_name);
    if (!counter_entry->base_name) {
      log_error("named_register: strdup failed for base_name");
      free(counter_entry);
      rwlock_wrunlock_impl(&g_named_registry.entries_lock);
      return base_name;
    }
    atomic_init(&counter_entry->counter, 0);
    HASH_ADD_KEYPTR(hh, g_named_registry.name_counters, counter_entry->base_name, strlen(counter_entry->base_name),
                    counter_entry);
  }

  // Increment and get counter for this name
  uint64_t counter = atomic_fetch_add(&counter_entry->counter, 1);
  rwlock_wrunlock_impl(&g_named_registry.entries_lock);

  // Generate suffixed name: "base_name.counter"
  char *full_name = NULL;
  int ret = asprintf(&full_name, "%s.%lu", base_name, counter);
  if (ret < 0) {
    log_error("named_register: asprintf failed for key=0x%tx", (ptrdiff_t)key);
    return base_name;
  }

  // Make file path relative to project root
  const char *relative_file = extract_project_relative_path(file ? file : "unknown");

  // Lock and insert/update in registry
  rwlock_wrlock_impl(&g_named_registry.entries_lock);

  named_entry_t *entry;
  HASH_FIND(hh, g_named_registry.entries, &key, sizeof(key), entry);

  if (entry) {
    // Update existing entry
    free(entry->name);
    free(entry->type);
    free(entry->format_spec);
    free(entry->file);
    free(entry->func);
    entry->name = full_name;
    entry->type = type ? strdup(type) : NULL;
    entry->format_spec = format_spec ? strdup(format_spec) : NULL;
    entry->file = file ? strdup(relative_file) : NULL;
    entry->line = line;
    entry->func = func ? strdup(func) : NULL;
  } else {
    // Create new entry
    entry = malloc(sizeof(named_entry_t));
    if (!entry) {
      log_error("named_register: malloc failed for entry");
      rwlock_wrunlock_impl(&g_named_registry.entries_lock);
      free(full_name);
      return base_name;
    }
    entry->key = key;
    entry->name = full_name;
    entry->type = type ? strdup(type) : NULL;
    entry->format_spec = format_spec ? strdup(format_spec) : NULL;
    entry->file = file ? strdup(relative_file) : NULL;
    entry->line = line;
    entry->func = func ? strdup(func) : NULL;
    HASH_ADD(hh, g_named_registry.entries, key, sizeof(key), entry);
  }

  rwlock_wrunlock_impl(&g_named_registry.entries_lock);

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

  // Format the name
  char *full_name = NULL;
  va_list args;
  va_start(args, fmt);
  int ret = vasprintf(&full_name, fmt, args);
  va_end(args);

  if (ret < 0) {
    log_error("named_register_fmt: vasprintf failed for key=0x%tx", (ptrdiff_t)key);
    return "?";
  }

  // Check for duplicate names
  rwlock_wrlock_impl(&g_named_registry.entries_lock);

  named_entry_t *existing, *tmp;
  HASH_ITER(hh, g_named_registry.entries, existing, tmp) {
    if (existing->name && strcmp(existing->name, full_name) == 0) {
      // Name already exists
      SET_ERRNO(ERROR_INVALID_STATE, "Name '%s' already registered (key=0x%tx, existing_key=0x%tx)", full_name,
                (ptrdiff_t)key, (ptrdiff_t)existing->key);
      rwlock_wrunlock_impl(&g_named_registry.entries_lock);
      free(full_name);
      return "?";
    }
  }

  // Make file path relative to project root
  const char *relative_file = extract_project_relative_path(file ? file : "unknown");

  named_entry_t *entry;
  HASH_FIND(hh, g_named_registry.entries, &key, sizeof(key), entry);

  if (entry) {
    // Update existing entry
    free(entry->name);
    free(entry->type);
    free(entry->format_spec);
    free(entry->file);
    free(entry->func);
    entry->name = full_name;
    entry->type = type ? strdup(type) : NULL;
    entry->format_spec = format_spec ? strdup(format_spec) : NULL;
    entry->file = file ? strdup(relative_file) : NULL;
    entry->line = line;
    entry->func = func ? strdup(func) : NULL;
  } else {
    // Create new entry
    entry = malloc(sizeof(named_entry_t));
    if (!entry) {
      log_error("named_register_fmt: malloc failed for entry");
      rwlock_wrunlock_impl(&g_named_registry.entries_lock);
      free(full_name);
      return "?";
    }
    entry->key = key;
    entry->name = full_name;
    entry->type = type ? strdup(type) : NULL;
    entry->format_spec = format_spec ? strdup(format_spec) : NULL;
    entry->file = file ? strdup(relative_file) : NULL;
    entry->line = line;
    entry->func = func ? strdup(func) : NULL;
    HASH_ADD(hh, g_named_registry.entries, key, sizeof(key), entry);
  }

  rwlock_wrunlock_impl(&g_named_registry.entries_lock);

  return entry->name;
}

void named_unregister(uintptr_t key) {
  if (!lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    return;
  }

  rwlock_wrlock_impl(&g_named_registry.entries_lock);

  named_entry_t *entry;
  HASH_FIND(hh, g_named_registry.entries, &key, sizeof(key), entry);

  if (entry) {
    HASH_DEL(g_named_registry.entries, entry);
    free(entry->name);
    free(entry->type);
    free(entry->format_spec);
    free(entry->file);
    free(entry->func);
    free(entry);
  }

  rwlock_wrunlock_impl(&g_named_registry.entries_lock);
}

const char *named_update_name(uintptr_t key, const char *new_base_name) {
  if (!new_base_name) {
    return NULL;
  }

  if (!lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    return new_base_name;
  }

  rwlock_wrlock_impl(&g_named_registry.entries_lock);

  named_entry_t *entry;
  HASH_FIND(hh, g_named_registry.entries, &key, sizeof(key), entry);

  if (!entry) {
    rwlock_wrunlock_impl(&g_named_registry.entries_lock);
    return NULL; // Key not found
  }

  // Get or create per-name counter for the new name
  name_counter_entry_t *counter_entry;
  HASH_FIND_STR(g_named_registry.name_counters, new_base_name, counter_entry);

  if (!counter_entry) {
    // Create new per-name counter entry
    counter_entry = malloc(sizeof(name_counter_entry_t));
    if (!counter_entry) {
      log_error("named_update_name: malloc failed for counter_entry");
      rwlock_wrunlock_impl(&g_named_registry.entries_lock);
      return entry->name;
    }
    counter_entry->base_name = strdup(new_base_name);
    if (!counter_entry->base_name) {
      log_error("named_update_name: strdup failed for base_name");
      free(counter_entry);
      rwlock_wrunlock_impl(&g_named_registry.entries_lock);
      return entry->name;
    }
    atomic_init(&counter_entry->counter, 0);
    HASH_ADD_KEYPTR(hh, g_named_registry.name_counters, counter_entry->base_name, strlen(counter_entry->base_name),
                    counter_entry);
  }

  // Get next counter for this new name
  uint64_t counter = atomic_fetch_add(&counter_entry->counter, 1);

  // Generate new suffixed name
  char *new_full_name = NULL;
  int ret = asprintf(&new_full_name, "%s.%lu", new_base_name, counter);
  if (ret < 0) {
    log_error("named_update_name: asprintf failed for key=0x%tx", (ptrdiff_t)key);
    rwlock_wrunlock_impl(&g_named_registry.entries_lock);
    return entry->name;
  }

  // Update the entry with new name
  free(entry->name);
  entry->name = new_full_name;

  rwlock_wrunlock_impl(&g_named_registry.entries_lock);

  return entry->name;
}

const char *named_get(uintptr_t key) {
  if (!lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    return NULL;
  }

  rwlock_rdlock_impl(&g_named_registry.entries_lock);

  named_entry_t *entry;
  HASH_FIND(hh, g_named_registry.entries, &key, sizeof(key), entry);

  const char *result = entry ? entry->name : NULL;

  rwlock_rdunlock_impl(&g_named_registry.entries_lock);

  return result;
}

const char *named_get_type(uintptr_t key) {
  if (!lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    return NULL;
  }

  rwlock_rdlock_impl(&g_named_registry.entries_lock);

  named_entry_t *entry;
  HASH_FIND(hh, g_named_registry.entries, &key, sizeof(key), entry);

  const char *result = entry ? entry->type : NULL;

  rwlock_rdunlock_impl(&g_named_registry.entries_lock);

  return result;
}

const char *named_get_format_spec(uintptr_t key) {
  if (!lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    return NULL;
  }

  rwlock_rdlock_impl(&g_named_registry.entries_lock);

  named_entry_t *entry;
  HASH_FIND(hh, g_named_registry.entries, &key, sizeof(key), entry);

  const char *result = entry ? entry->format_spec : NULL;

  rwlock_rdunlock_impl(&g_named_registry.entries_lock);

  return result;
}

const char *named_describe(uintptr_t key, const char *type_hint) {
  if (!type_hint) {
    type_hint = "object";
  }

  if (!lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    return type_hint;
  }

  // Use per-thread static buffer to avoid allocations in hot path
  static _Thread_local char buffer[DESCRIBE_BUFFER_SIZE];

  rwlock_rdlock_impl(&g_named_registry.entries_lock);

  named_entry_t *entry;
  HASH_FIND(hh, g_named_registry.entries, &key, sizeof(key), entry);

  if (entry) {
    // Use stored type if available, otherwise use type_hint
    const char *type = entry->type ? entry->type : type_hint;

    if (entry->file && entry->func) {
      snprintf(buffer, sizeof(buffer), "%s/%s (0x%tx) @ %s:%d:%s()", type, entry->name, (ptrdiff_t)key, entry->file,
               entry->line, entry->func);
    } else {
      snprintf(buffer, sizeof(buffer), "%s/%s (0x%tx)", type, entry->name, (ptrdiff_t)key);
    }
  } else {
    snprintf(buffer, sizeof(buffer), "%s (0x%tx)", type_hint, (ptrdiff_t)key);
  }

  rwlock_rdunlock_impl(&g_named_registry.entries_lock);

  return buffer;
}

const char *named_describe_thread(void *thread) {
  uintptr_t key = asciichat_thread_to_key((asciichat_thread_t)thread);
  return named_describe(key, "thread");
}

/**
 * @brief Temporary entry for iteration (stack-allocated to avoid allocations)
 */
typedef struct {
  uintptr_t key;
  char name[MAX_NAME_LEN];
} named_iter_entry_t;

void named_registry_for_each(named_iter_callback_t callback, void *user_data) {
  if (!callback || !lifecycle_is_initialized(&g_named_registry.lifecycle)) {
    return;
  }

  // Collect entries while holding read lock
  named_iter_entry_t entries[256];
  int count = 0;

  rwlock_rdlock_impl(&g_named_registry.entries_lock);

  named_entry_t *entry, *tmp;
  HASH_ITER(hh, g_named_registry.entries, entry, tmp) {
    if (count < 256) {
      entries[count].key = entry->key;
      strncpy(entries[count].name, entry->name ? entry->name : "?", MAX_NAME_LEN - 1);
      entries[count].name[MAX_NAME_LEN - 1] = '\0';
      count++;
    }
  }

  rwlock_rdunlock_impl(&g_named_registry.entries_lock);

  // Call callback for each entry (without holding lock)
  for (int i = 0; i < count; i++) {
    callback(entries[i].key, entries[i].name, user_data);
  }
}

void named_registry_register_packet_types(void) {
  // Import packet type enum to register all values
  // This ensures all packet types are programmatically registered and discoverable

  // Packet type enum values from include/ascii-chat/network/packet.h
  // Format: key="PACKET_TYPE=%d", name="PACKET_TYPE_%d"

#define REGISTER_PKT_TYPE(value, name_suffix)                                                                          \
  do {                                                                                                                 \
    char key_buf[64], name_buf[64];                                                                                    \
    snprintf(key_buf, sizeof(key_buf), "PACKET_TYPE=%d", (value));                                                     \
    snprintf(name_buf, sizeof(name_buf), "%s", (name_suffix));                                                         \
    named_register((uintptr_t)(value), key_buf, "packet_type", "%d", __FILE__, __LINE__, __func__);                    \
  } while (0)

  // Register all packet types from the enum
  REGISTER_PKT_TYPE(1, "PROTOCOL_VERSION");
  REGISTER_PKT_TYPE(1000, "CRYPTO_CLIENT_HELLO");
  REGISTER_PKT_TYPE(1100, "CRYPTO_CAPABILITIES");
  REGISTER_PKT_TYPE(1101, "CRYPTO_PARAMETERS");
  REGISTER_PKT_TYPE(1102, "CRYPTO_KEY_EXCHANGE_INIT");
  REGISTER_PKT_TYPE(1103, "CRYPTO_KEY_EXCHANGE_RESP");
  REGISTER_PKT_TYPE(1104, "CRYPTO_AUTH_CHALLENGE");
  REGISTER_PKT_TYPE(1105, "CRYPTO_AUTH_RESPONSE");
  REGISTER_PKT_TYPE(1106, "CRYPTO_AUTH_FAILED");
  REGISTER_PKT_TYPE(1107, "CRYPTO_SERVER_AUTH_RESP");
  REGISTER_PKT_TYPE(1108, "CRYPTO_HANDSHAKE_COMPLETE");
  REGISTER_PKT_TYPE(1109, "CRYPTO_NO_ENCRYPTION");
  REGISTER_PKT_TYPE(1200, "ENCRYPTED");
  REGISTER_PKT_TYPE(1201, "CRYPTO_REKEY_REQUEST");
  REGISTER_PKT_TYPE(1202, "CRYPTO_REKEY_RESPONSE");
  REGISTER_PKT_TYPE(1203, "CRYPTO_REKEY_COMPLETE");
  REGISTER_PKT_TYPE(2000, "SIZE_MESSAGE");
  REGISTER_PKT_TYPE(2001, "AUDIO_MESSAGE");
  REGISTER_PKT_TYPE(2002, "TEXT_MESSAGE");
  REGISTER_PKT_TYPE(2003, "ERROR_MESSAGE");
  REGISTER_PKT_TYPE(2004, "REMOTE_LOG");
  REGISTER_PKT_TYPE(3000, "ASCII_FRAME");
  REGISTER_PKT_TYPE(3001, "IMAGE_FRAME");
  REGISTER_PKT_TYPE(4000, "AUDIO_BATCH");
  REGISTER_PKT_TYPE(4001, "AUDIO_OPUS_BATCH");
  REGISTER_PKT_TYPE(5000, "CLIENT_CAPABILITIES");
  REGISTER_PKT_TYPE(5001, "PING");
  REGISTER_PKT_TYPE(5002, "PONG");
  REGISTER_PKT_TYPE(5003, "CLIENT_JOIN");
  REGISTER_PKT_TYPE(5004, "CLIENT_LEAVE");
  REGISTER_PKT_TYPE(5005, "STREAM_START");
  REGISTER_PKT_TYPE(5006, "STREAM_STOP");
  REGISTER_PKT_TYPE(5007, "CLEAR_CONSOLE");
  REGISTER_PKT_TYPE(5008, "SERVER_STATE");
  REGISTER_PKT_TYPE(6000, "ACIP_SESSION_CREATE");
  REGISTER_PKT_TYPE(6001, "ACIP_SESSION_CREATED");
  REGISTER_PKT_TYPE(6002, "ACIP_SESSION_LOOKUP");
  REGISTER_PKT_TYPE(6003, "ACIP_SESSION_INFO");
  REGISTER_PKT_TYPE(6004, "ACIP_SESSION_JOIN");
  REGISTER_PKT_TYPE(6005, "ACIP_SESSION_JOINED");
  REGISTER_PKT_TYPE(6006, "ACIP_SESSION_LEAVE");
  REGISTER_PKT_TYPE(6007, "ACIP_SESSION_END");
  REGISTER_PKT_TYPE(6008, "ACIP_SESSION_RECONNECT");
  REGISTER_PKT_TYPE(6009, "ACIP_WEBRTC_SDP");
  REGISTER_PKT_TYPE(6010, "ACIP_WEBRTC_ICE");
  REGISTER_PKT_TYPE(6020, "ACIP_STRING_RESERVE");
  REGISTER_PKT_TYPE(6021, "ACIP_STRING_RESERVED");
  REGISTER_PKT_TYPE(6022, "ACIP_STRING_RENEW");
  REGISTER_PKT_TYPE(6023, "ACIP_STRING_RELEASE");
  REGISTER_PKT_TYPE(6050, "ACIP_PARTICIPANT_LIST");
  REGISTER_PKT_TYPE(6051, "ACIP_RING_COLLECT");
  REGISTER_PKT_TYPE(6060, "ACIP_NETWORK_QUALITY");
  REGISTER_PKT_TYPE(6061, "ACIP_HOST_ANNOUNCEMENT");
  REGISTER_PKT_TYPE(6062, "ACIP_HOST_DESIGNATED");
  REGISTER_PKT_TYPE(6063, "ACIP_SETTINGS_SYNC");
  REGISTER_PKT_TYPE(6064, "ACIP_SETTINGS_ACK");
  REGISTER_PKT_TYPE(6065, "ACIP_HOST_LOST");
  REGISTER_PKT_TYPE(6066, "ACIP_FUTURE_HOST_ELECTED");
  REGISTER_PKT_TYPE(6067, "ACIP_PARTICIPANT_JOINED");
  REGISTER_PKT_TYPE(6068, "ACIP_PARTICIPANT_LEFT");
  REGISTER_PKT_TYPE(6070, "ACIP_BANDWIDTH_TEST");
  REGISTER_PKT_TYPE(6071, "ACIP_BANDWIDTH_RESULT");
  REGISTER_PKT_TYPE(6075, "ACIP_BROADCAST_ACK");
  REGISTER_PKT_TYPE(6100, "RING_MEMBERS");
  REGISTER_PKT_TYPE(6101, "STATS_COLLECTION_START");
  REGISTER_PKT_TYPE(6102, "STATS_UPDATE");
  REGISTER_PKT_TYPE(6103, "RING_ELECTION_RESULT");
  REGISTER_PKT_TYPE(6104, "STATS_ACK");
  REGISTER_PKT_TYPE(6190, "ACIP_DISCOVERY_PING");
  REGISTER_PKT_TYPE(6199, "ACIP_ERROR");

#undef REGISTER_PKT_TYPE

  log_debug("Registered %d packet types in named registry", 68); // Count of all packet types
}

// ============================================================================
// Type-specific registration and lookup with namespace encoding
// ============================================================================

/**
 * Key namespace prefixes to avoid collisions between different value types.
 * Each type uses a distinct high-order bits pattern.
 */
#define FD_KEY_PREFIX (0xFD00000000000000UL)
#define PACKET_TYPE_KEY_PREFIX (0xAC00000000000000UL)

static inline uintptr_t encode_fd_key(int fd) {
  return FD_KEY_PREFIX | (uintptr_t)fd;
}

static inline uintptr_t encode_packet_type_key(int pkt_type) {
  return PACKET_TYPE_KEY_PREFIX | (uintptr_t)pkt_type;
}

const char *named_register_fd(int fd, const char *file, int line, const char *func) {
  char fd_key[32];
  snprintf(fd_key, sizeof(fd_key), "fd=%d", fd);
  return named_register(encode_fd_key(fd), fd_key, "fd", "%d", file, line, func);
}

const char *named_get_fd(int fd) {
  return named_get(encode_fd_key(fd));
}

const char *named_get_fd_format_spec(int fd) {
  return named_get_format_spec(encode_fd_key(fd));
}

const char *named_register_packet_type(int pkt_type, const char *file, int line, const char *func) {
  char pkt_key[32];
  snprintf(pkt_key, sizeof(pkt_key), "PACKET_TYPE=%d", pkt_type);
  return named_register(encode_packet_type_key(pkt_type), pkt_key, "packet_type", "%d", file, line, func);
}

const char *named_get_packet_type(int pkt_type) {
  return named_get(encode_packet_type_key(pkt_type));
}

const char *named_get_packet_type_format_spec(int pkt_type) {
  return named_get_format_spec(encode_packet_type_key(pkt_type));
}

#else // NDEBUG (Release builds - stubs)

int named_init(void) {
  return 0;
}

void named_destroy(void) {}

const char *named_register(uintptr_t key, const char *base_name, const char *type, const char *format_spec,
                           const char *file, int line, const char *func) {
  (void)key;
  (void)type;
  (void)format_spec;
  (void)file;
  (void)line;
  (void)func;
  return base_name ? base_name : "?";
}

const char *named_register_fmt(uintptr_t key, const char *type, const char *format_spec, const char *file, int line,
                               const char *func, const char *fmt, ...) {
  (void)key;
  (void)type;
  (void)format_spec;
  (void)file;
  (void)line;
  (void)func;
  return fmt ? fmt : "?";
}

void named_unregister(uintptr_t key) {
  (void)key;
}

const char *named_update_name(uintptr_t key, const char *new_base_name) {
  (void)key;
  (void)new_base_name;
  return NULL;
}

const char *named_get(uintptr_t key) {
  (void)key;
  return NULL;
}

const char *named_get_type(uintptr_t key) {
  (void)key;
  return NULL;
}

const char *named_get_format_spec(uintptr_t key) {
  (void)key;
  return NULL;
}

const char *named_describe(uintptr_t key, const char *type_hint) {
  (void)key;
  (void)type_hint;
  return "?";
}

const char *named_describe_thread(void *thread) {
  (void)thread;
  return "?";
}

const char *named_register_fd(int fd, const char *file, int line, const char *func) {
  (void)fd;
  (void)file;
  (void)line;
  (void)func;
  return "?";
}

const char *named_get_fd(int fd) {
  (void)fd;
  return NULL;
}

const char *named_get_fd_format_spec(int fd) {
  (void)fd;
  return NULL;
}

const char *named_register_packet_type(int pkt_type, const char *file, int line, const char *func) {
  (void)pkt_type;
  (void)file;
  (void)line;
  (void)func;
  return "?";
}

const char *named_get_packet_type(int pkt_type) {
  (void)pkt_type;
  return NULL;
}

const char *named_get_packet_type_format_spec(int pkt_type) {
  (void)pkt_type;
  return NULL;
}

void named_registry_for_each(named_iter_callback_t callback, void *user_data) {
  (void)callback;
  (void)user_data;
  // No-op in release builds
}

void named_registry_register_packet_types(void) {
  // No-op in release builds
}

#endif // !NDEBUG
