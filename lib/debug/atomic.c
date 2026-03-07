/**
 * @file debug/atomic.c
 * @brief Debug formatting and state tracking for atomic operations
 * @ingroup debug
 * @date March 2026
 *
 * Implements debug-specific functions: initialization/shutdown, and
 * state formatting for display via --sync-state.
 *
 * Hook implementations (atomic_on_load, atomic_on_store, etc.) are now
 * in lib/atomic.c alongside the wrapper functions they support.
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
// Print State (called from debug_sync_print_state in lib/debug/sync.c)
// ============================================================================

/**
 * @brief Format timing info for an atomic and write to buffer
 * @param atomic Pointer to atomic_t
 * @param buffer Output buffer
 * @param size Buffer size
 * @return Number of bytes written to buffer
 */
int debug_atomic_format_timing(const atomic_t *atomic, char *buffer, size_t size) {
    if (!atomic || !buffer || size == 0) return 0;

    // If never accessed, return empty (so caller can skip output)
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
        snprintf(count_str, sizeof(count_str), "[ops: load=%llu store=%llu cas=%llu/%llu fetch=%llu]",
                 (unsigned long long)atomic->load_count, (unsigned long long)atomic->store_count,
                 (unsigned long long)atomic->cas_success_count, (unsigned long long)atomic->cas_count,
                 (unsigned long long)atomic->fetch_count);
    }

    offset += snprintf(buffer + offset, size - offset, "%s %s %s", load_str, store_str, count_str);
    return offset;
}

/**
 * @brief Format timing info for an atomic_ptr_t and write to buffer
 * @param atomic Pointer to atomic_ptr_t
 * @param buffer Output buffer
 * @param size Buffer size
 * @return Number of bytes written to buffer
 */
int debug_atomic_ptr_format_timing(const atomic_ptr_t *atomic, char *buffer, size_t size) {
    if (!atomic || !buffer || size == 0) return 0;

    // If never accessed, return empty (so caller can skip output)
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

    if (atomic->load_count > 0 || atomic->store_count > 0 || atomic->cas_count > 0 || atomic->exchange_count > 0) {
        snprintf(count_str, sizeof(count_str), "[ops: load=%llu store=%llu cas=%llu/%llu exchange=%llu]",
                 (unsigned long long)atomic->load_count, (unsigned long long)atomic->store_count,
                 (unsigned long long)atomic->cas_success_count, (unsigned long long)atomic->cas_count,
                 (unsigned long long)atomic->exchange_count);
    }

    offset += snprintf(buffer + offset, size - offset, "%s %s %s", load_str, store_str, count_str);
    return offset;
}

void debug_atomic_print_state(void) {
    if (!g_atomic_debug_initialized) return;

    // Iterate named registry for atomic entries
    // Note: this is now kept for backward compatibility
    // The preferred method is to use sync.c's atomic_iter_callback
    // which integrates atomics into the synchronized output buffer
}
