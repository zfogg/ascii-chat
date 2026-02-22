/**
 * @file sync.c
 * @ingroup debug_sync
 * @brief 🔒 Synchronization primitive debugging with dynamic state inspection
 * @date February 2026
 *
 * Clean architecture: queries named.c registry for current state.
 * No internal collection overhead - inspects lock structs directly.
 */

#include <ascii-chat/debug/sync.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/platform/rwlock.h>
#include <ascii-chat/platform/cond.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/log/logging.h>
#include <stdio.h>
#include <string.h>

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Format elapsed time string
 * @param elapsed_ns Elapsed time in nanoseconds
 * @param buffer Output buffer
 * @param size Buffer size
 */
static void format_elapsed(uint64_t elapsed_ns, char *buffer, size_t size) {
    if (elapsed_ns < 1000) {
        snprintf(buffer, size, "%lu ns", (unsigned long)elapsed_ns);
    } else if (elapsed_ns < 1000000) {
        snprintf(buffer, size, "%.2f µs", (double)elapsed_ns / 1000.0);
    } else if (elapsed_ns < 1000000000) {
        snprintf(buffer, size, "%.2f ms", (double)elapsed_ns / 1000000.0);
    } else {
        snprintf(buffer, size, "%.2f s", (double)elapsed_ns / 1000000000.0);
    }
}

/**
 * @brief Check if pointer might be a valid mutex
 * @param ptr Pointer to inspect
 * @return true if it might be a mutex_t
 *
 * Tries to safely read from the pointer to determine if it looks like a mutex.
 * Note: This is heuristic-based and may have false positives.
 */
static bool looks_like_mutex(const void *ptr) {
    if (!ptr) return false;
    // Try to read first field (impl) - if it crashes, we catch it elsewhere
    // For now, assume any pointer might be valid
    return true;
}

/**
 * @brief Check if pointer might be a valid rwlock
 * @param ptr Pointer to inspect
 * @return true if it might be an rwlock_t
 */
static bool looks_like_rwlock(const void *ptr) {
    if (!ptr) return false;
    return true;
}

/**
 * @brief Check if pointer might be a valid cond
 * @param ptr Pointer to inspect
 * @return true if it might be a cond_t
 */
static bool looks_like_cond(const void *ptr) {
    if (!ptr) return false;
    return true;
}

/**
 * @brief Extract timing info from a mutex_t
 * @param mutex Pointer to mutex_t
 * @param buffer Output buffer for formatted info
 * @param size Buffer size
 * @return Number of bytes written
 */
static int format_mutex_timing(const mutex_t *mutex, char *buffer, size_t size) {
    if (!mutex) return 0;

    int offset = 0;
    uint64_t now_ns = time_get_ns();

    if (mutex->last_lock_time_ns > 0) {
        char elapsed_str[64];
        format_elapsed(now_ns - mutex->last_lock_time_ns, elapsed_str, sizeof(elapsed_str));
        offset += snprintf(buffer + offset, size - offset, "    Last lock: %s ago\n", elapsed_str);
    }

    if (mutex->last_unlock_time_ns > 0) {
        char elapsed_str[64];
        format_elapsed(now_ns - mutex->last_unlock_time_ns, elapsed_str, sizeof(elapsed_str));
        offset += snprintf(buffer + offset, size - offset, "    Last unlock: %s ago\n", elapsed_str);
    }

    return offset;
}

/**
 * @brief Extract timing info from an rwlock_t
 * @param rwlock Pointer to rwlock_t
 * @param buffer Output buffer for formatted info
 * @param size Buffer size
 * @return Number of bytes written
 */
static int format_rwlock_timing(const rwlock_t *rwlock, char *buffer, size_t size) {
    if (!rwlock) return 0;

    int offset = 0;
    uint64_t now_ns = time_get_ns();

    if (rwlock->last_rdlock_time_ns > 0) {
        char elapsed_str[64];
        format_elapsed(now_ns - rwlock->last_rdlock_time_ns, elapsed_str, sizeof(elapsed_str));
        offset += snprintf(buffer + offset, size - offset, "    Last rdlock: %s ago\n", elapsed_str);
    }

    if (rwlock->last_wrlock_time_ns > 0) {
        char elapsed_str[64];
        format_elapsed(now_ns - rwlock->last_wrlock_time_ns, elapsed_str, sizeof(elapsed_str));
        offset += snprintf(buffer + offset, size - offset, "    Last wrlock: %s ago\n", elapsed_str);
    }

    if (rwlock->last_unlock_time_ns > 0) {
        char elapsed_str[64];
        format_elapsed(now_ns - rwlock->last_unlock_time_ns, elapsed_str, sizeof(elapsed_str));
        offset += snprintf(buffer + offset, size - offset, "    Last unlock: %s ago\n", elapsed_str);
    }

    return offset;
}

/**
 * @brief Extract timing info from a cond_t
 * @param cond Pointer to cond_t
 * @param buffer Output buffer for formatted info
 * @param size Buffer size
 * @return Number of bytes written
 */
static int format_cond_timing(const cond_t *cond, char *buffer, size_t size) {
    if (!cond) return 0;

    int offset = 0;
    uint64_t now_ns = time_get_ns();

    if (cond->last_wait_time_ns > 0) {
        char elapsed_str[64];
        format_elapsed(now_ns - cond->last_wait_time_ns, elapsed_str, sizeof(elapsed_str));
        offset += snprintf(buffer + offset, size - offset, "    Last wait: %s ago\n", elapsed_str);
    }

    if (cond->last_signal_time_ns > 0) {
        char elapsed_str[64];
        format_elapsed(now_ns - cond->last_signal_time_ns, elapsed_str, sizeof(elapsed_str));
        offset += snprintf(buffer + offset, size - offset, "    Last signal: %s ago\n", elapsed_str);
    }

    if (cond->last_broadcast_time_ns > 0) {
        char elapsed_str[64];
        format_elapsed(now_ns - cond->last_broadcast_time_ns, elapsed_str, sizeof(elapsed_str));
        offset += snprintf(buffer + offset, size - offset, "    Last broadcast: %s ago\n", elapsed_str);
    }

    return offset;
}

// ============================================================================
// Iterator Callbacks for named.c
// ============================================================================

static void mutex_iter_callback(uintptr_t key, const char *name, void *user_data) {
    (void)user_data; // Unused

    if (!looks_like_mutex((const void *)key)) {
        return;
    }

    const mutex_t *mutex = (const mutex_t *)key;
    char timing_str[256] = {0};
    format_mutex_timing(mutex, timing_str, sizeof(timing_str));

    if (timing_str[0]) {
        log_info("Mutex %s (0x%tx):\n%s", name, (ptrdiff_t)key, timing_str);
    }
}

static void rwlock_iter_callback(uintptr_t key, const char *name, void *user_data) {
    (void)user_data; // Unused

    if (!looks_like_rwlock((const void *)key)) {
        return;
    }

    const rwlock_t *rwlock = (const rwlock_t *)key;
    char timing_str[512] = {0};
    format_rwlock_timing(rwlock, timing_str, sizeof(timing_str));

    if (timing_str[0]) {
        log_info("RWLock %s (0x%tx):\n%s", name, (ptrdiff_t)key, timing_str);
    }
}

static void cond_iter_callback(uintptr_t key, const char *name, void *user_data) {
    (void)user_data; // Unused

    if (!looks_like_cond((const void *)key)) {
        return;
    }

    const cond_t *cond = (const cond_t *)key;
    char timing_str[512] = {0};
    format_cond_timing(cond, timing_str, sizeof(timing_str));

    if (timing_str[0]) {
        log_info("Cond %s (0x%tx):\n%s", name, (ptrdiff_t)key, timing_str);
    }
}

// ============================================================================
// Public API Implementation
// ============================================================================

void debug_sync_print_mutex_state(void) {
    log_info("=== Mutex Timing State ===");
    named_registry_for_each(mutex_iter_callback, NULL);
    log_info("=== End Mutex Timing State ===");
}

void debug_sync_print_rwlock_state(void) {
    log_info("=== RWLock Timing State ===");
    named_registry_for_each(rwlock_iter_callback, NULL);
    log_info("=== End RWLock Timing State ===");
}

void debug_sync_print_cond_state(void) {
    log_info("=== Condition Variable Timing State ===");
    named_registry_for_each(cond_iter_callback, NULL);
    log_info("=== End Condition Variable Timing State ===");
}

void debug_sync_print_state(void) {
    log_info("=== Synchronization Primitive State ===");
    debug_sync_print_mutex_state();
    debug_sync_print_rwlock_state();
    debug_sync_print_cond_state();
    log_info("=== End Synchronization Primitive State ===");
}

// ============================================================================
// Legacy API - stubs for backward compatibility
// ============================================================================

int lock_debug_init(void) {
    return 0;
}

int lock_debug_start_thread(void) {
    return 0;
}

void lock_debug_destroy(void) {
}

void lock_debug_cleanup_thread(void) {
}

void lock_debug_trigger_print(void) {
    debug_sync_print_state();
}

void lock_debug_get_stats(uint64_t *total_acquired, uint64_t *total_released, uint32_t *currently_held) {
    if (total_acquired) *total_acquired = 0;
    if (total_released) *total_released = 0;
    if (currently_held) *currently_held = 0;
}

void lock_debug_print_state(void) {
    debug_sync_print_state();
}

// ============================================================================
// Debug Lock Operation Stubs - pass-through to implementations
// ============================================================================

int debug_sync_mutex_lock(mutex_t *mutex, const char *file_name, int line_number, const char *function_name) {
    (void)file_name;
    (void)line_number;
    (void)function_name;
    return mutex_lock_impl(mutex);
}

int debug_sync_mutex_trylock(mutex_t *mutex, const char *file_name, int line_number, const char *function_name) {
    (void)file_name;
    (void)line_number;
    (void)function_name;
    return mutex_trylock_impl(mutex);
}

int debug_sync_mutex_unlock(mutex_t *mutex, const char *file_name, int line_number, const char *function_name) {
    (void)file_name;
    (void)line_number;
    (void)function_name;
    return mutex_unlock_impl(mutex);
}

int debug_sync_rwlock_rdlock(rwlock_t *lock, const char *file_name, int line_number, const char *function_name) {
    (void)file_name;
    (void)line_number;
    (void)function_name;
    return rwlock_rdlock_impl(lock);
}

int debug_sync_rwlock_rdunlock(rwlock_t *lock, const char *file_name, int line_number, const char *function_name) {
    (void)file_name;
    (void)line_number;
    (void)function_name;
    return rwlock_rdunlock_impl(lock);
}

int debug_sync_rwlock_wrlock(rwlock_t *lock, const char *file_name, int line_number, const char *function_name) {
    (void)file_name;
    (void)line_number;
    (void)function_name;
    return rwlock_wrlock_impl(lock);
}

int debug_sync_rwlock_wrunlock(rwlock_t *lock, const char *file_name, int line_number, const char *function_name) {
    (void)file_name;
    (void)line_number;
    (void)function_name;
    return rwlock_wrunlock_impl(lock);
}

int debug_sync_cond_wait(cond_t *cond, mutex_t *mutex, const char *file_name, int line_number, const char *function_name) {
    (void)file_name;
    (void)line_number;
    (void)function_name;
    return cond_wait(cond, mutex);
}

int debug_sync_cond_timedwait(cond_t *cond, mutex_t *mutex, uint64_t timeout_ns, const char *file_name, int line_number, const char *function_name) {
    (void)file_name;
    (void)line_number;
    (void)function_name;
    return cond_timedwait(cond, mutex, timeout_ns);
}

int debug_sync_cond_signal(cond_t *cond, const char *file_name, int line_number, const char *function_name) {
    (void)file_name;
    (void)line_number;
    (void)function_name;
    return cond_signal(cond);
}

int debug_sync_cond_broadcast(cond_t *cond, const char *file_name, int line_number, const char *function_name) {
    (void)file_name;
    (void)line_number;
    (void)function_name;
    return cond_broadcast(cond);
}

bool lock_debug_is_initialized(void) {
    return true;
}
