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
#define DESCRIBE_BUFFER_SIZE 768  // Increased for file:line:func info

// ============================================================================
// Types
// ============================================================================

/**
 * @brief Per-name counter entry
 */
typedef struct name_counter_entry {
    char *base_name;                    // The base name (e.g., "media_pause")
    _Atomic uint64_t counter;           // Per-name counter
    UT_hash_handle hh;                  // uthash handle
} name_counter_entry_t;

/**
 * @brief Registry entry for a named object
 */
typedef struct named_entry {
    uintptr_t key;                      // Registry lookup key
    char *name;                         // Allocated name string (e.g., "recv.7")
    char *type;                         // Data type label (e.g., "mutex", "socket") (allocated)
    char *format_spec;                  // Format specifier for printing (e.g., "0x%tx") (allocated)
    char *file;                         // Source file where registered (allocated)
    int line;                           // Source line where registered
    char *func;                         // Function where registered (allocated)
    UT_hash_handle hh;                  // uthash handle
} named_entry_t;

/**
 * @brief Global named object registry state
 */
typedef struct {
    named_entry_t *entries;             // uthash hash table (NULL = empty)
    name_counter_entry_t *name_counters; // Per-name counter registry
    rwlock_t entries_lock;              // Thread-safe access to uthash
    lifecycle_t lifecycle;              // Init/shutdown state machine
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

const char *named_register(uintptr_t key, const char *base_name, const char *type,
                          const char *format_spec,
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
        HASH_ADD_KEYPTR(hh, g_named_registry.name_counters, counter_entry->base_name,
                       strlen(counter_entry->base_name), counter_entry);
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

const char *named_register_fmt(uintptr_t key, const char *type,
                              const char *format_spec,
                              const char *file, int line, const char *func,
                              const char *fmt, ...) {
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
            SET_ERRNO(ERROR_INVALID_STATE,
                     "Name '%s' already registered (key=0x%tx, existing_key=0x%tx)",
                     full_name, (ptrdiff_t)key, (ptrdiff_t)existing->key);
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
            snprintf(buffer, sizeof(buffer), "%s/%s (0x%tx) @ %s:%d:%s()",
                     type, entry->name, (ptrdiff_t)key,
                     entry->file, entry->line, entry->func);
        } else {
            snprintf(buffer, sizeof(buffer), "%s/%s (0x%tx)",
                     type, entry->name, (ptrdiff_t)key);
        }
    } else {
        snprintf(buffer, sizeof(buffer), "%s (0x%tx)",
                 type_hint, (ptrdiff_t)key);
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

#else // NDEBUG (Release builds - stubs)

int named_init(void) {
    return 0;
}

void named_destroy(void) {
}

const char *named_register(uintptr_t key, const char *base_name, const char *type,
                          const char *format_spec,
                          const char *file, int line, const char *func) {
    (void)key;
    (void)type;
    (void)format_spec;
    (void)file;
    (void)line;
    (void)func;
    return base_name ? base_name : "?";
}

const char *named_register_fmt(uintptr_t key, const char *type,
                              const char *format_spec,
                              const char *file, int line, const char *func,
                              const char *fmt, ...) {
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

void named_registry_for_each(named_iter_callback_t callback, void *user_data) {
    (void)callback;
    (void)user_data;
    // No-op in release builds
}

#endif // !NDEBUG
