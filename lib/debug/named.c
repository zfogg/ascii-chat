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
#include "ascii-chat/uthash/uthash.h"
#include "ascii-chat/log/logging.h"

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
#define DESCRIBE_BUFFER_SIZE 512

// ============================================================================
// Types
// ============================================================================

/**
 * @brief Registry entry for a named object
 */
typedef struct named_entry {
    uintptr_t key;                      // Registry lookup key
    char *name;                         // Allocated name string (e.g., "recv_mutex.7")
    UT_hash_handle hh;                  // uthash handle
} named_entry_t;

/**
 * @brief Global named object registry state
 */
typedef struct {
    named_entry_t *entries;             // uthash hash table (NULL = empty)
    rwlock_t entries_lock;              // Thread-safe access to uthash
    _Atomic uint64_t name_counter;      // Auto-incrementing suffix for uniqueness
    _Atomic bool initialized;           // Initialization flag
} named_registry_t;

// ============================================================================
// Global State
// ============================================================================

static named_registry_t g_named_registry = {
    .entries = NULL,
    .name_counter = 0,
    .initialized = false,
};

// ============================================================================
// Public API Implementation
// ============================================================================

int named_init(void) {
    if (atomic_load(&g_named_registry.initialized)) {
        return 0; // Already initialized
    }

    int err = rwlock_init_impl(&g_named_registry.entries_lock);
    if (err != 0) {
        log_error("named_init: rwlock_init_impl failed: %d", err);
        return err;
    }

    atomic_store(&g_named_registry.initialized, true);
    return 0;
}

void named_destroy(void) {
    if (!atomic_load(&g_named_registry.initialized)) {
        return;
    }

    rwlock_wrlock_impl(&g_named_registry.entries_lock);

    // Free all entries
    named_entry_t *entry, *tmp;
    HASH_ITER(hh, g_named_registry.entries, entry, tmp) {
        HASH_DEL(g_named_registry.entries, entry);
        free(entry->name);
        free(entry);
    }

    rwlock_wrunlock_impl(&g_named_registry.entries_lock);
    rwlock_destroy_impl(&g_named_registry.entries_lock);

    atomic_store(&g_named_registry.initialized, false);
}

const char *named_register(uintptr_t key, const char *base_name) {
    if (!base_name) {
        return "?";
    }

    if (!atomic_load(&g_named_registry.initialized)) {
        return base_name;
    }

    // Generate suffixed name: "base_name.counter"
    uint64_t counter = atomic_fetch_add(&g_named_registry.name_counter, 1);
    char *full_name = NULL;
    int ret = asprintf(&full_name, "%s.%" PRIu64, base_name, counter);
    if (ret < 0) {
        log_error("named_register: asprintf failed for key=0x%tx", (ptrdiff_t)key);
        return base_name;
    }

    // Lock and insert/update in registry
    rwlock_wrlock_impl(&g_named_registry.entries_lock);

    named_entry_t *entry;
    HASH_FIND(hh, g_named_registry.entries, &key, sizeof(key), entry);

    if (entry) {
        // Update existing entry
        free(entry->name);
        entry->name = full_name;
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
        HASH_ADD(hh, g_named_registry.entries, key, sizeof(key), entry);
    }

    rwlock_wrunlock_impl(&g_named_registry.entries_lock);

    return entry->name;
}

const char *named_register_fmt(uintptr_t key, const char *fmt, ...) {
    if (!fmt) {
        return "?";
    }

    if (!atomic_load(&g_named_registry.initialized)) {
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

    // Lock and insert/update in registry
    rwlock_wrlock_impl(&g_named_registry.entries_lock);

    named_entry_t *entry;
    HASH_FIND(hh, g_named_registry.entries, &key, sizeof(key), entry);

    if (entry) {
        // Update existing entry
        free(entry->name);
        entry->name = full_name;
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
        HASH_ADD(hh, g_named_registry.entries, key, sizeof(key), entry);
    }

    rwlock_wrunlock_impl(&g_named_registry.entries_lock);

    return entry->name;
}

void named_unregister(uintptr_t key) {
    if (!atomic_load(&g_named_registry.initialized)) {
        return;
    }

    rwlock_wrlock_impl(&g_named_registry.entries_lock);

    named_entry_t *entry;
    HASH_FIND(hh, g_named_registry.entries, &key, sizeof(key), entry);

    if (entry) {
        HASH_DEL(g_named_registry.entries, entry);
        free(entry->name);
        free(entry);
    }

    rwlock_wrunlock_impl(&g_named_registry.entries_lock);
}

const char *named_get(uintptr_t key) {
    if (!atomic_load(&g_named_registry.initialized)) {
        return NULL;
    }

    rwlock_rdlock_impl(&g_named_registry.entries_lock);

    named_entry_t *entry;
    HASH_FIND(hh, g_named_registry.entries, &key, sizeof(key), entry);

    const char *result = entry ? entry->name : NULL;

    rwlock_rdunlock_impl(&g_named_registry.entries_lock);

    return result;
}

const char *named_describe(uintptr_t key, const char *type_hint) {
    if (!type_hint) {
        type_hint = "object";
    }

    if (!atomic_load(&g_named_registry.initialized)) {
        return type_hint;
    }

    // Use per-thread static buffer to avoid allocations in hot path
    static _Thread_local char buffer[DESCRIBE_BUFFER_SIZE];

    rwlock_rdlock_impl(&g_named_registry.entries_lock);

    named_entry_t *entry;
    HASH_FIND(hh, g_named_registry.entries, &key, sizeof(key), entry);

    if (entry) {
        snprintf(buffer, sizeof(buffer), "%s: %s (0x%tx)",
                 type_hint, entry->name, (ptrdiff_t)key);
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

#else // NDEBUG (Release builds - stubs)

int named_init(void) {
    return 0;
}

void named_destroy(void) {
}

const char *named_register(uintptr_t key, const char *base_name) {
    (void)key;
    return base_name ? base_name : "?";
}

const char *named_register_fmt(uintptr_t key, const char *fmt, ...) {
    (void)key;
    return fmt ? fmt : "?";
}

void named_unregister(uintptr_t key) {
    (void)key;
}

const char *named_get(uintptr_t key) {
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

#endif // !NDEBUG
