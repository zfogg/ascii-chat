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
#include <ascii-chat/debug/backtrace.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/platform/rwlock.h>
#include <ascii-chat/platform/cond.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/log/logging.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Format elapsed time string (uses time_pretty for consistent formatting)
 * @param elapsed_ns Elapsed time in nanoseconds
 * @param buffer Output buffer
 * @param size Buffer size
 */
static void format_elapsed(uint64_t elapsed_ns, char *buffer, size_t size) {
    time_pretty(elapsed_ns, -1, buffer, size);
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

    if (mutex->last_lock_time_ns > 0 && mutex->last_lock_time_ns <= now_ns) {
        char elapsed_str[64];
        format_elapsed(now_ns - mutex->last_lock_time_ns, elapsed_str, sizeof(elapsed_str));
        offset += snprintf(buffer + offset, size - offset, "    Last lock: %s ago\n", elapsed_str);
    }

    if (mutex->last_unlock_time_ns > 0 && mutex->last_unlock_time_ns <= now_ns) {
        char elapsed_str[64];
        format_elapsed(now_ns - mutex->last_unlock_time_ns, elapsed_str, sizeof(elapsed_str));
        offset += snprintf(buffer + offset, size - offset, "    Last unlock: %s ago\n", elapsed_str);
    }

    if (mutex->currently_held_by_tid != 0) {
        offset += snprintf(buffer + offset, size - offset, "    *** HELD by thread %lu ***\n", mutex->currently_held_by_tid);
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

    if (rwlock->last_rdlock_time_ns > 0 && rwlock->last_rdlock_time_ns <= now_ns) {
        char elapsed_str[64];
        format_elapsed(now_ns - rwlock->last_rdlock_time_ns, elapsed_str, sizeof(elapsed_str));
        offset += snprintf(buffer + offset, size - offset, "    Last rdlock: %s ago\n", elapsed_str);
    }

    if (rwlock->last_wrlock_time_ns > 0 && rwlock->last_wrlock_time_ns <= now_ns) {
        char elapsed_str[64];
        format_elapsed(now_ns - rwlock->last_wrlock_time_ns, elapsed_str, sizeof(elapsed_str));
        offset += snprintf(buffer + offset, size - offset, "    Last wrlock: %s ago\n", elapsed_str);
    }

    if (rwlock->last_unlock_time_ns > 0 && rwlock->last_unlock_time_ns <= now_ns) {
        char elapsed_str[64];
        format_elapsed(now_ns - rwlock->last_unlock_time_ns, elapsed_str, sizeof(elapsed_str));
        offset += snprintf(buffer + offset, size - offset, "    Last unlock: %s ago\n", elapsed_str);
    }

    if (rwlock->write_held_by_tid != 0) {
        offset += snprintf(buffer + offset, size - offset, "    *** WRITE HELD by thread %lu ***\n", rwlock->write_held_by_tid);
    }

    if (rwlock->read_lock_count > 0) {
        offset += snprintf(buffer + offset, size - offset, "    *** READ HELD by %lu thread(s) ***\n", rwlock->read_lock_count);
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

    if (cond->last_wait_time_ns > 0 && cond->last_wait_time_ns <= now_ns) {
        char elapsed_str[64];
        format_elapsed(now_ns - cond->last_wait_time_ns, elapsed_str, sizeof(elapsed_str));
        offset += snprintf(buffer + offset, size - offset, "    Last wait: %s ago\n", elapsed_str);
    }

    if (cond->last_signal_time_ns > 0 && cond->last_signal_time_ns <= now_ns) {
        char elapsed_str[64];
        format_elapsed(now_ns - cond->last_signal_time_ns, elapsed_str, sizeof(elapsed_str));
        offset += snprintf(buffer + offset, size - offset, "    Last signal: %s ago\n", elapsed_str);
    }

    if (cond->last_broadcast_time_ns > 0 && cond->last_broadcast_time_ns <= now_ns) {
        char elapsed_str[64];
        format_elapsed(now_ns - cond->last_broadcast_time_ns, elapsed_str, sizeof(elapsed_str));
        offset += snprintf(buffer + offset, size - offset, "    Last broadcast: %s ago\n", elapsed_str);
    }

    if (cond->waiting_count > 0) {
        offset += snprintf(buffer + offset, size - offset, "    *** %lu thread(s) WAITING (most recent: thread %lu) ***\n",
                          cond->waiting_count, cond->last_waiting_tid);
    }

    return offset;
}

// ============================================================================
// Iterator Callbacks for named.c
// ============================================================================

static void mutex_iter_callback(uintptr_t key, const char *name, void *user_data) {
    (void)user_data; // Unused

    const char *type = named_get_type(key);
    if (!type || strcmp(type, "mutex") != 0) {
        return;
    }

    const mutex_t *mutex = (const mutex_t *)key;
    char timing_str[256] = {0};
    format_mutex_timing(mutex, timing_str, sizeof(timing_str));

    // Always print mutex info, even if timing_str is empty (shows "never used")
    if (timing_str[0]) {
        log_info("Mutex %s (0x%tx):\n%s", name, (ptrdiff_t)key, timing_str);
    } else {
        log_info("Mutex %s (0x%tx): [never locked]", name, (ptrdiff_t)key);
    }
}

static void rwlock_iter_callback(uintptr_t key, const char *name, void *user_data) {
    (void)user_data; // Unused

    const char *type = named_get_type(key);
    if (!type || strcmp(type, "rwlock") != 0) {
        return;
    }

    const rwlock_t *rwlock = (const rwlock_t *)key;
    char timing_str[512] = {0};
    format_rwlock_timing(rwlock, timing_str, sizeof(timing_str));

    // Always print rwlock info, even if timing_str is empty (shows "never used")
    if (timing_str[0]) {
        log_info("RWLock %s (0x%tx):\n%s", name, (ptrdiff_t)key, timing_str);
    } else {
        log_info("RWLock %s (0x%tx): [never locked]", name, (ptrdiff_t)key);
    }
}

static void cond_iter_callback(uintptr_t key, const char *name, void *user_data) {
    (void)user_data; // Unused

    const char *type = named_get_type(key);
    if (!type || strcmp(type, "cond") != 0) {
        return;
    }

    const cond_t *cond = (const cond_t *)key;
    char timing_str[512] = {0};
    format_cond_timing(cond, timing_str, sizeof(timing_str));

    // Always print condition variable info, even if timing_str is empty (shows "never used")
    if (timing_str[0]) {
        log_info("Cond %s (0x%tx):\n%s", name, (ptrdiff_t)key, timing_str);
    } else {
        log_info("Cond %s (0x%tx): [never signaled]", name, (ptrdiff_t)key);
    }
}

// ============================================================================
// Public API Implementation
// ============================================================================

void debug_sync_print_mutex_state(void) {
    log_info("=== Mutex Timing State ===");
    // TODO: Debug why mutexes aren't being registered. In theory, every mutex
    // created with mutex_init(mutex, "name") should call NAMED_REGISTER(mutex, "name", "mutex")
    // which should add it to the named registry for inspection.
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
// Scheduled Debug State Printing (runs on separate thread)
// ============================================================================

typedef enum {
    DEBUG_REQUEST_STATE,      // Print sync state
    DEBUG_REQUEST_BACKTRACE,  // Print backtrace
} debug_request_type_t;

typedef struct {
    debug_request_type_t request_type;  // What to print
    uint64_t delay_ns;
    volatile bool should_run;
    volatile bool should_exit;
    volatile bool signal_triggered;  // Flag set by SIGUSR1 handler
    mutex_t mutex;                   // Protects access to flags
    cond_t cond;                     // Wakes thread when signal arrives
    bool initialized;                // Tracks if mutex/cond are initialized
} debug_state_request_t;

static debug_state_request_t g_debug_state_request = {DEBUG_REQUEST_STATE, 0, false, false, false, {0}, {0}, false};
static asciichat_thread_t g_debug_thread;

/**
 * @brief Thread function for scheduled debug state printing
 *
 * Handles both delayed printing and signal-triggered printing (via SIGUSR1).
 * Logging is performed on this thread, not in signal handler context,
 * avoiding potential deadlocks with logging mutexes.
 *
 * Uses a condition variable to wake up immediately when SIGUSR1 is received,
 * without polling or busy-waiting.
 */
static void *debug_print_thread_fn(void *arg) {
    (void)arg;

    while (!g_debug_state_request.should_exit) {
        // Handle delayed printing
        if (g_debug_state_request.should_run && g_debug_state_request.delay_ns > 0) {
            platform_sleep_ns(g_debug_state_request.delay_ns);
            g_debug_state_request.delay_ns = 0;
        }

        // Handle both scheduled and signal-triggered printing
        mutex_lock(&g_debug_state_request.mutex);
        if ((g_debug_state_request.should_run || g_debug_state_request.signal_triggered)
            && !g_debug_state_request.should_exit) {
            debug_request_type_t request_type = g_debug_state_request.request_type;
            mutex_unlock(&g_debug_state_request.mutex);

            // Print based on request type
            if (request_type == DEBUG_REQUEST_STATE) {
                debug_sync_print_state();
            } else if (request_type == DEBUG_REQUEST_BACKTRACE) {
                backtrace_t bt;
                backtrace_capture_and_symbolize(&bt);
                backtrace_print("Backtrace", &bt, 0, 0, NULL);
                backtrace_t_free(&bt);
            }

            mutex_lock(&g_debug_state_request.mutex);
            g_debug_state_request.should_run = false;
            g_debug_state_request.signal_triggered = false;
        }

        // Wait for work or signal, with 100ms timeout to check should_exit
        if (!g_debug_state_request.should_exit) {
            cond_timedwait(&g_debug_state_request.cond, &g_debug_state_request.mutex, 100000000);  // 100ms
        }
        mutex_unlock(&g_debug_state_request.mutex);
    }

    return NULL;
}

/**
 * @brief Schedule delayed debug state printing on debug thread
 * @param delay_ns Nanoseconds to sleep before printing
 */
void debug_sync_print_state_delayed(uint64_t delay_ns) {
    g_debug_state_request.request_type = DEBUG_REQUEST_STATE;
    g_debug_state_request.delay_ns = delay_ns;
    g_debug_state_request.should_run = true;
    cond_signal(&g_debug_state_request.cond);
}

/**
 * @brief Schedule delayed backtrace printing on debug thread
 * @param delay_ns Nanoseconds to sleep before printing
 */
void debug_sync_print_backtrace_delayed(uint64_t delay_ns) {
    g_debug_state_request.request_type = DEBUG_REQUEST_BACKTRACE;
    g_debug_state_request.delay_ns = delay_ns;
    g_debug_state_request.should_run = true;
    cond_signal(&g_debug_state_request.cond);
}

// ============================================================================
// Debug Sync API - Thread management
// ============================================================================

int debug_sync_init(void) {
    return 0;
}

int debug_sync_start_thread(void) {
    // Initialize mutex and condition variable for signal wakeup
    if (!g_debug_state_request.initialized) {
        mutex_init(&g_debug_state_request.mutex, "debug_sync_state");
        cond_init(&g_debug_state_request.cond, "debug_sync_signal");
        g_debug_state_request.initialized = true;
    }

    g_debug_state_request.should_exit = false;
    int err = asciichat_thread_create(&g_debug_thread, debug_print_thread_fn, NULL);
    return err;
}

void debug_sync_destroy(void) {
}

void debug_sync_cleanup_thread(void) {
    // Signal the thread to wake up immediately instead of waiting for 100ms timeout
    mutex_lock(&g_debug_state_request.mutex);
    g_debug_state_request.should_exit = true;
    cond_signal(&g_debug_state_request.cond);
    mutex_unlock(&g_debug_state_request.mutex);
    asciichat_thread_join(&g_debug_thread, NULL);
}

void debug_sync_trigger_print(void) {
    // Set flag to trigger printing on debug thread (from SIGUSR1 handler).
    // We don't call debug_sync_print_state() directly here to avoid logging
    // in signal handler context, which could deadlock with logging mutexes.
    //
    // Signal the condition variable to wake up the debug thread immediately
    // (without waiting for the 100ms timeout).
    g_debug_state_request.signal_triggered = true;
    cond_signal(&g_debug_state_request.cond);
}

void debug_sync_get_stats(uint64_t *total_acquired, uint64_t *total_released, uint32_t *currently_held) {
    if (total_acquired) *total_acquired = 0;
    if (total_released) *total_released = 0;
    if (currently_held) *currently_held = 0;
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

bool debug_sync_is_initialized(void) {
    return true;
}
