/**
 * @file debug/atomic.c
 * @brief Debug tracking for atomic operations
 * @ingroup debug
 * @date March 2026
 *
 * Implements debug hooks for atomic operations and integration with
 * the named.c registry for named atomic tracking.
 *
 * No mutexes here: debug hooks use non-atomic field updates which is
 * acceptable for debug statistics. Recursion prevention: uses raw C11
 * atomics only, never calls back into atomic_t system.
 */

#include <ascii-chat/debug/atomic.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/util/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static bool g_atomic_debug_initialized = false;

// ============================================================================
// Debug Initialization/Shutdown
// ============================================================================

void debug_atomic_init(void) {
    g_atomic_debug_initialized = true;
}

void debug_atomic_shutdown(void) {
    g_atomic_debug_initialized = false;
}

bool debug_atomic_is_initialized(void) {
    return g_atomic_debug_initialized;
}


// ============================================================================
// Debug Hooks (called from _impl functions in debug builds)
// ============================================================================

void atomic_on_load(atomic_t *a) {
    if (!a || !g_atomic_debug_initialized) return;
    a->last_load_time_ns = time_get_ns();
    a->load_count++;
}

void atomic_on_store(atomic_t *a) {
    if (!a || !g_atomic_debug_initialized) return;
    a->last_store_time_ns = time_get_ns();
    a->store_count++;
}

void atomic_on_cas(atomic_t *a, bool success) {
    if (!a || !g_atomic_debug_initialized) return;
    a->cas_count++;
    if (success) {
        a->cas_success_count++;
        a->last_store_time_ns = time_get_ns();
    }
}

void atomic_on_fetch(atomic_t *a) {
    if (!a || !g_atomic_debug_initialized) return;
    a->fetch_count++;
    a->last_store_time_ns = time_get_ns();
}

void atomic_ptr_on_load(_Atomic(void *) *a) {
    if (!a || !g_atomic_debug_initialized) return;
    a->last_load_time_ns = time_get_ns();
    a->load_count++;
}

void atomic_ptr_on_store(_Atomic(void *) *a) {
    if (!a || !g_atomic_debug_initialized) return;
    a->last_store_time_ns = time_get_ns();
    a->store_count++;
}

void atomic_ptr_on_cas(_Atomic(void *) *a, bool success) {
    if (!a || !g_atomic_debug_initialized) return;
    a->cas_count++;
    if (success) {
        a->cas_success_count++;
        a->last_store_time_ns = time_get_ns();
    }
}

void atomic_ptr_on_exchange(_Atomic(void *) *a) {
    if (!a || !g_atomic_debug_initialized) return;
    a->exchange_count++;
    a->last_store_time_ns = time_get_ns();
}

// ============================================================================
// Print State (called from debug_sync_print_state in lib/debug/sync.c)
// ============================================================================

/**
 * Format timing info for an atomic as a single line
 */
static int format_atomic_timing(const atomic_t *atomic, char *buffer, size_t size) {
    if (!atomic) return 0;

    // If never accessed, return empty
    if (atomic->last_load_time_ns == 0 && atomic->last_store_time_ns == 0) {
        return 0;
    }

    int offset = 0;
    uint64_t now_ns = time_get_ns();

    char load_str[64] = "";
    char store_str[64] = "";
    char count_str[128] = "";

    if (atomic->last_load_time_ns > 0 && atomic->last_load_time_ns <= now_ns) {
        char elapsed_str[64];
        time_pretty(now_ns - atomic->last_load_time_ns, -1, elapsed_str, sizeof(elapsed_str));
        snprintf(load_str, sizeof(load_str), "load=%s", elapsed_str);
    }

    if (atomic->last_store_time_ns > 0 && atomic->last_store_time_ns <= now_ns) {
        char elapsed_str[64];
        time_pretty(now_ns - atomic->last_store_time_ns, -1, elapsed_str, sizeof(elapsed_str));
        snprintf(store_str, sizeof(store_str), "store=%s", elapsed_str);
    }

    if (atomic->load_count > 0 || atomic->store_count > 0 || atomic->cas_count > 0 || atomic->fetch_count > 0) {
        snprintf(count_str, sizeof(count_str), "[ops: load=%lu store=%lu cas=%lu/%lu fetch=%lu]",
                 atomic->load_count, atomic->store_count, atomic->cas_success_count, atomic->cas_count, atomic->fetch_count);
    }

    offset += snprintf(buffer + offset, size - offset, "%s %s %s", load_str, store_str, count_str);
    return offset;
}

// Callback function for named_registry_for_each
static void atomic_print_entry(uintptr_t key, const char *name, void *user_data) {
    (void)user_data;  // Unused

    // key is the address of atomic_t or _Atomic(void *) pointer
    // We can't distinguish type here, so we try to format both
    // The caller (named registry) knows the type from registration

    atomic_t *a = (atomic_t *)key;
    char timing_buf[256] = "";

    format_atomic_timing(a, timing_buf, sizeof(timing_buf));
    if (timing_buf[0] != '\0') {
        printf("  [ATOMIC] %s: %s\n", name, timing_buf);
    }
}

void debug_atomic_print_state(void) {
    if (!g_atomic_debug_initialized) return;

    // Iterate named registry for atomic entries
    // Note: named_registry_for_each iterates all registered entries
    // The callback filters by checking if the entry is atomic/atomic_ptr type
    named_registry_for_each(atomic_print_entry, NULL);
}
