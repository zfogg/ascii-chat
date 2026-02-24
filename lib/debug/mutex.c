/**
 * @file debug/mutex.c
 * @ingroup debug_sync
 * @brief Per-thread mutex lock stack for deadlock detection
 * @date February 2026
 *
 * Tracks which mutexes are held and pending per thread.
 * Detects circular wait patterns (classic deadlock condition).
 */

#include <ascii-chat/debug/mutex.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/platform/api.h>
#include <string.h>
#include <pthread.h>

// ============================================================================
// Per-thread Lock Stack Storage
// ============================================================================

#define MUTEX_STACK_MAX_DEPTH 64

typedef struct {
    mutex_stack_entry_t stack[MUTEX_STACK_MAX_DEPTH];
    int depth;
} thread_lock_stack_t;

// Thread-local storage for fast per-thread access
static __thread thread_lock_stack_t g_thread_lock_stack = {0};

// Global lock to protect thread registry
static pthread_mutex_t g_thread_registry_lock = PTHREAD_MUTEX_INITIALIZER;

// Global registry of all threads that have used mutexes
#define MAX_THREADS 256
typedef struct {
    pthread_t thread_id;
    thread_lock_stack_t *stack;  // Separate allocation per thread (not thread-local)
} thread_registry_entry_t;

static thread_registry_entry_t g_thread_registry[MAX_THREADS] = {0};
static int g_thread_registry_count = 0;

// ============================================================================
// Thread Registry Management
// ============================================================================

/**
 * @brief Get the thread-local stack for fast per-thread operations
 */
static thread_lock_stack_t *get_thread_local_stack(void) {
    return &g_thread_lock_stack;
}

/**
 * @brief Get or create thread registry entry for current thread
 */
static thread_lock_stack_t *get_or_create_registry_stack(void) {
    pthread_t current = pthread_self();

    // Try to find existing entry
    pthread_mutex_lock(&g_thread_registry_lock);
    for (int i = 0; i < g_thread_registry_count; i++) {
        if (pthread_equal(g_thread_registry[i].thread_id, current)) {
            pthread_mutex_unlock(&g_thread_registry_lock);
            return g_thread_registry[i].stack;
        }
    }

    // Create new entry with separate allocation
    if (g_thread_registry_count >= MAX_THREADS) {
        pthread_mutex_unlock(&g_thread_registry_lock);
        return NULL;  // Registry full
    }

    // Allocate new stack for this thread
    thread_lock_stack_t *new_stack = SAFE_CALLOC(1, sizeof(thread_lock_stack_t), thread_lock_stack_t *);
    if (!new_stack) {
        pthread_mutex_unlock(&g_thread_registry_lock);
        return NULL;
    }

    g_thread_registry[g_thread_registry_count].thread_id = current;
    g_thread_registry[g_thread_registry_count].stack = new_stack;
    g_thread_registry_count++;

    pthread_mutex_unlock(&g_thread_registry_lock);
    return new_stack;
}

// ============================================================================
// Public Stack Operations
// ============================================================================

void mutex_stack_push_pending(uintptr_t mutex_key, const char *mutex_name) {
    thread_lock_stack_t *stack = get_thread_local_stack();
    if (!stack || stack->depth >= MUTEX_STACK_MAX_DEPTH) {
        return;
    }

    stack->stack[stack->depth].mutex_key = mutex_key;
    stack->stack[stack->depth].mutex_name = mutex_name;
    stack->stack[stack->depth].state = MUTEX_STACK_STATE_PENDING;
    stack->stack[stack->depth].timestamp_ns = time_get_ns();
    stack->depth++;

    // Also update registry copy
    thread_lock_stack_t *registry_stack = get_or_create_registry_stack();
    if (registry_stack && registry_stack != stack) {
        memcpy(registry_stack, stack, sizeof(thread_lock_stack_t));
    }
}

void mutex_stack_mark_locked(uintptr_t mutex_key) {
    thread_lock_stack_t *stack = get_thread_local_stack();
    if (!stack || stack->depth == 0) {
        return;
    }

    // Mark the top of the stack as locked
    int top = stack->depth - 1;
    if (stack->stack[top].mutex_key == mutex_key) {
        stack->stack[top].state = MUTEX_STACK_STATE_LOCKED;
        stack->stack[top].timestamp_ns = time_get_ns();
    }

    // Also update registry copy
    thread_lock_stack_t *registry_stack = get_or_create_registry_stack();
    if (registry_stack && registry_stack != stack) {
        memcpy(registry_stack, stack, sizeof(thread_lock_stack_t));
    }
}

void mutex_stack_pop(uintptr_t mutex_key) {
    thread_lock_stack_t *stack = get_thread_local_stack();
    if (!stack || stack->depth == 0) {
        return;
    }

    // Validate top matches
    int top = stack->depth - 1;
    if (stack->stack[top].mutex_key == mutex_key) {
        stack->depth--;
    }

    // Also update registry copy
    thread_lock_stack_t *registry_stack = get_or_create_registry_stack();
    if (registry_stack && registry_stack != stack) {
        memcpy(registry_stack, stack, sizeof(thread_lock_stack_t));
    }
}

int mutex_stack_get_current(mutex_stack_entry_t *out_entries, int max_entries) {
    thread_lock_stack_t *stack = get_thread_local_stack();
    if (!stack || !out_entries) {
        return 0;
    }

    int count = (stack->depth < max_entries) ? stack->depth : max_entries;
    memcpy(out_entries, stack->stack, count * sizeof(mutex_stack_entry_t));
    return stack->depth;  // Return actual depth even if truncated
}

// ============================================================================
// Global Thread Stack Access
// ============================================================================

int mutex_stack_get_all_threads(
    mutex_stack_entry_t ***out_stacks,
    int **out_stack_counts,
    int *out_thread_count) {

    if (!out_stacks || !out_stack_counts || !out_thread_count) {
        return -1;
    }

    pthread_mutex_lock(&g_thread_registry_lock);
    int thread_count = g_thread_registry_count;

    // Allocate arrays
    *out_stacks = SAFE_MALLOC(thread_count, mutex_stack_entry_t **);
    *out_stack_counts = SAFE_MALLOC(thread_count, int *);

    if (!*out_stacks || !*out_stack_counts) {
        SAFE_FREE(*out_stacks);
        SAFE_FREE(*out_stack_counts);
        pthread_mutex_unlock(&g_thread_registry_lock);
        return -1;
    }

    // Copy each thread's stack
    for (int i = 0; i < thread_count; i++) {
        thread_lock_stack_t *src = g_thread_registry[i].stack;
        int depth = src->depth;

        (*out_stack_counts)[i] = depth;
        (*out_stacks)[i] = SAFE_MALLOC(depth, mutex_stack_entry_t *);

        if ((*out_stacks)[i]) {
            memcpy((*out_stacks)[i], src->stack, depth * sizeof(mutex_stack_entry_t));
        }
    }

    *out_thread_count = thread_count;
    pthread_mutex_unlock(&g_thread_registry_lock);
    return 0;
}

void mutex_stack_free_all_threads(
    mutex_stack_entry_t **stacks,
    int *stack_counts,
    int thread_count) {

    if (!stacks || !stack_counts) return;

    for (int i = 0; i < thread_count; i++) {
        SAFE_FREE(stacks[i]);
    }

    SAFE_FREE(stacks);
    SAFE_FREE(stack_counts);
}

// ============================================================================
// Deadlock Detection
// ============================================================================

/**
 * @brief Check if a mutex is held by the given thread
 */
static bool thread_holds_mutex(thread_lock_stack_t *stack, uintptr_t mutex_key) {
    for (int i = 0; i < stack->depth; i++) {
        if (stack->stack[i].mutex_key == mutex_key &&
            stack->stack[i].state == MUTEX_STACK_STATE_LOCKED) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Get the mutex a thread is waiting for (if any)
 */
static uintptr_t thread_waiting_for_mutex(thread_lock_stack_t *stack) {
    if (stack->depth > 0) {
        int top = stack->depth - 1;
        if (stack->stack[top].state == MUTEX_STACK_STATE_PENDING) {
            return stack->stack[top].mutex_key;
        }
    }
    return 0;
}

/**
 * @brief Detect circular wait deadlocks
 *
 * Detects both same-thread and multi-thread deadlock patterns:
 *
 * Same-thread deadlock:
 * - Thread tries to acquire a mutex it already holds (recursive lock on non-recursive mutex)
 *
 * Multi-thread circular wait:
 * - Thread A holds mutex M1 and waits for M2
 * - Thread B holds M2 and waits for M1
 * This creates a cycle in the "waits-for" graph.
 */
void mutex_stack_detect_deadlocks(void) {
    mutex_stack_entry_t **all_stacks = NULL;
    int *stack_counts = NULL;
    int thread_count = 0;

    if (mutex_stack_get_all_threads(&all_stacks, &stack_counts, &thread_count) != 0) {
        return;
    }

    pthread_mutex_lock(&g_thread_registry_lock);

    // Check each thread for deadlock conditions
    for (int i = 0; i < thread_count && i < g_thread_registry_count; i++) {
        thread_lock_stack_t *stack_a = g_thread_registry[i].stack;
        uintptr_t waiting_for = thread_waiting_for_mutex(stack_a);

        if (waiting_for == 0) continue;  // Thread A not waiting

        // Same-thread deadlock: thread trying to acquire a mutex it already holds
        if (thread_holds_mutex(stack_a, waiting_for)) {
            const char *mutex_name = named_describe(waiting_for, "mutex");
            log_error("DEADLOCK DETECTED: Same-thread deadlock (recursive lock attempt)");
            log_error("  Thread %lu: already holds %s, attempting to acquire it again",
                     (unsigned long)g_thread_registry[i].thread_id,
                     mutex_name ? mutex_name : "unknown");
            continue;
        }

        // Multi-thread circular wait
        for (int j = 0; j < thread_count && j < g_thread_registry_count; j++) {
            if (i == j) continue;

            thread_lock_stack_t *stack_b = g_thread_registry[j].stack;
            if (!thread_holds_mutex(stack_b, waiting_for)) continue;  // B doesn't hold it

            // Check if B is waiting for something A holds
            uintptr_t b_waiting_for = thread_waiting_for_mutex(stack_b);
            if (b_waiting_for == 0) continue;  // B not waiting

            if (thread_holds_mutex(stack_a, b_waiting_for)) {
                // Circular wait detected!
                char mutex_a_name[256] = "unknown";
                char mutex_b_name[256] = "unknown";

                const char *a_name = named_describe(waiting_for, "mutex");
                const char *b_name = named_describe(b_waiting_for, "mutex");

                if (a_name) snprintf(mutex_a_name, sizeof(mutex_a_name), "%s", a_name);
                if (b_name) snprintf(mutex_b_name, sizeof(mutex_b_name), "%s", b_name);

                log_error("DEADLOCK DETECTED: Circular wait between threads");
                log_error("  Thread %lu: holds %s, waiting for %s",
                         (unsigned long)g_thread_registry[i].thread_id,
                         mutex_b_name, mutex_a_name);
                log_error("  Thread %lu: holds %s, waiting for %s",
                         (unsigned long)g_thread_registry[j].thread_id,
                         mutex_a_name, mutex_b_name);
            }
        }
    }

    pthread_mutex_unlock(&g_thread_registry_lock);
    mutex_stack_free_all_threads(all_stacks, stack_counts, thread_count);
}

// ============================================================================
// Initialization
// ============================================================================

int mutex_stack_init(void) {
    return 0;
}

void mutex_stack_cleanup(void) {
    // Clear registry on cleanup
    pthread_mutex_lock(&g_thread_registry_lock);
    g_thread_registry_count = 0;
    pthread_mutex_unlock(&g_thread_registry_lock);
}
