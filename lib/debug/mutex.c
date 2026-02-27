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
#include <ascii-chat/platform/mutex.h>
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

// Global registry of all threads that have used mutexes
#define MAX_THREADS 256
typedef struct {
  pthread_t thread_id;
  thread_lock_stack_t *stack; // Separate allocation per thread (not thread-local)
} thread_registry_entry_t;

static thread_registry_entry_t g_thread_registry[MAX_THREADS] = {0};
static _Atomic(int) g_thread_registry_count = 0;

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
 * @brief Register current thread in the global registry if not already registered
 * Lock-free implementation using atomic operations - completely avoids recursion
 * This is safe from concurrent access because:
 * 1. Each thread gets a unique index via atomic compare-exchange
 * 2. Writes to the registry are ordered with atomic release semantics
 * 3. Reads from the debug thread use acquire semantics
 */
static void register_thread_if_needed(void) {
  static __thread bool registered = false;

  // Once registered, skip immediately
  if (registered) {
    return;
  }

  thread_lock_stack_t *local_stack = get_thread_local_stack();
  if (!local_stack) {
    return;
  }

  pthread_t current_thread = pthread_self();

  // Check if thread is already in registry (without lock)
  int current_count = atomic_load_explicit(&g_thread_registry_count, memory_order_acquire);
  for (int i = 0; i < current_count; i++) {
    if (pthread_equal(g_thread_registry[i].thread_id, current_thread)) {
      registered = true;
      return; // Already registered
    }
  }

  // Try to claim a slot in the registry atomically
  int slot;
  while (1) {
    int old_count = atomic_load_explicit(&g_thread_registry_count, memory_order_acquire);
    if (old_count >= MAX_THREADS) {
      registered = true; // Registry is full, give up
      return;
    }

    // Try to atomically increment the count and claim a slot
    if (atomic_compare_exchange_strong_explicit(&g_thread_registry_count, &old_count, old_count + 1,
                                                memory_order_release, memory_order_acquire)) {
      slot = old_count;
      break; // Successfully claimed slot
    }
    // If compare-exchange failed, loop and try again
  }

  // Write thread data to claimed slot (release semantics for visibility)
  g_thread_registry[slot].thread_id = current_thread;
  g_thread_registry[slot].stack = local_stack;

  // Memory barrier to ensure writes are visible to other threads
  atomic_thread_fence(memory_order_release);

  registered = true;
}

// ============================================================================
// Public Stack Operations
// ============================================================================

void mutex_stack_push_pending(uintptr_t mutex_key, const char *mutex_name) {
  thread_lock_stack_t *stack = get_thread_local_stack();
  if (!stack || stack->depth >= MUTEX_STACK_MAX_DEPTH) {
    return;
  }

  // Register this thread in the global registry on first mutex use
  register_thread_if_needed();

  stack->stack[stack->depth].mutex_key = mutex_key;
  stack->stack[stack->depth].mutex_name = mutex_name;
  stack->stack[stack->depth].state = MUTEX_STACK_STATE_PENDING;
  stack->stack[stack->depth].timestamp_ns = time_get_ns();
  stack->depth++;
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

  // Thread-local only. Registry is populated on-demand by mutex_stack_get_all_threads()
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

  // Thread-local only. Registry is populated on-demand by mutex_stack_get_all_threads()
}

int mutex_stack_get_current(mutex_stack_entry_t *out_entries, int max_entries) {
  thread_lock_stack_t *stack = get_thread_local_stack();
  if (!stack || !out_entries) {
    return 0;
  }

  int count = (stack->depth < max_entries) ? stack->depth : max_entries;
  memcpy(out_entries, stack->stack, count * sizeof(mutex_stack_entry_t));
  return stack->depth; // Return actual depth even if truncated
}

// ============================================================================
// Global Thread Stack Access
// ============================================================================

int mutex_stack_get_all_threads(mutex_stack_entry_t ***out_stacks, int **out_stack_counts, int *out_thread_count) {

  if (!out_stacks || !out_stack_counts || !out_thread_count) {
    return -1;
  }

  // Read the thread count with acquire semantics (lock-free, no mutex needed)
  // Use memory_order_seq_cst to ensure we get a consistent snapshot
  int thread_count = atomic_load_explicit(&g_thread_registry_count, memory_order_seq_cst);

  // Allocate arrays for threads in the registry
  // Note: SAFE_MALLOC takes bytes as first parameter, not count
  *out_stacks = SAFE_MALLOC(thread_count * sizeof(mutex_stack_entry_t *), mutex_stack_entry_t **);
  *out_stack_counts = SAFE_MALLOC(thread_count * sizeof(int), int *);

  if (!*out_stacks || !*out_stack_counts) {
    SAFE_FREE(*out_stacks);
    SAFE_FREE(*out_stack_counts);
    return -1;
  }

  // Copy each thread's stack from the registry
  // Note: the thread count can change concurrently, so we only process entries
  // that existed at the time we allocated our arrays
  int actual_count = 0;
  for (int i = 0; i < thread_count; i++) {
    thread_lock_stack_t *src = g_thread_registry[i].stack;
    if (!src) {
      // Stack not yet initialized for this thread, skip it
      continue;
    }

    int depth = src->depth;

    (*out_stack_counts)[i] = depth;
    (*out_stacks)[i] = SAFE_MALLOC(depth * sizeof(mutex_stack_entry_t), mutex_stack_entry_t *);

    if ((*out_stacks)[i]) {
      memcpy((*out_stacks)[i], src->stack, depth * sizeof(mutex_stack_entry_t));
    }
    actual_count++;
  }

  *out_thread_count = actual_count;
  return 0;
}

void mutex_stack_free_all_threads(mutex_stack_entry_t **stacks, int *stack_counts, int thread_count) {

  if (!stacks || !stack_counts)
    return;

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
    if (stack->stack[i].mutex_key == mutex_key && stack->stack[i].state == MUTEX_STACK_STATE_LOCKED) {
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

  // mutex_stack_get_all_threads already holds the lock, so we access the registry
  // directly. But we still need to ensure the lock is initialized for any internal
  // use. This function just reads the registry without acquiring the lock again.

  // Check each thread for deadlock conditions
  for (int i = 0; i < thread_count && i < g_thread_registry_count; i++) {
    thread_lock_stack_t *stack_a = g_thread_registry[i].stack;
    uintptr_t waiting_for = thread_waiting_for_mutex(stack_a);

    if (waiting_for == 0)
      continue; // Thread A not waiting

    // Same-thread deadlock: thread trying to acquire a mutex it already holds
    if (thread_holds_mutex(stack_a, waiting_for)) {
      const char *mutex_name = named_describe(waiting_for, "mutex");
      log_error("\x1b[1;31m╔═══════════════════════════════════════════════════════════╗\x1b[0m");
      log_error("\x1b[1;31m║  ⚠️  DEADLOCK DETECTED: Same-thread Recursive Lock  ⚠️  ║\x1b[0m");
      log_error("\x1b[1;31m╚═══════════════════════════════════════════════════════════╝\x1b[0m");
      log_error("  Thread Address:        0x%lx", (unsigned long)g_thread_registry[i].thread_id);
      log_error("  Mutex Address:         0x%lx", waiting_for);
      log_error("  Mutex Name:            %s", mutex_name ? mutex_name : "unknown");
      log_error("  Issue:                 Thread attempts recursive lock on non-recursive mutex");
      continue;
    }

    // Multi-thread circular wait
    for (int j = 0; j < thread_count && j < g_thread_registry_count; j++) {
      if (i == j)
        continue;

      thread_lock_stack_t *stack_b = g_thread_registry[j].stack;
      if (!thread_holds_mutex(stack_b, waiting_for))
        continue; // B doesn't hold it

      // Check if B is waiting for something A holds
      uintptr_t b_waiting_for = thread_waiting_for_mutex(stack_b);
      if (b_waiting_for == 0)
        continue; // B not waiting

      if (thread_holds_mutex(stack_a, b_waiting_for)) {
        // Circular wait detected!
        char mutex_a_name[256] = "unknown";
        char mutex_b_name[256] = "unknown";

        const char *a_name = named_describe(waiting_for, "mutex");
        const char *b_name = named_describe(b_waiting_for, "mutex");

        if (a_name)
          snprintf(mutex_a_name, sizeof(mutex_a_name), "%s", a_name);
        if (b_name)
          snprintf(mutex_b_name, sizeof(mutex_b_name), "%s", b_name);

        log_error("\x1b[1;31m╔═══════════════════════════════════════════════════════════╗\x1b[0m");
        log_error("\x1b[1;31m║      ⚠️  DEADLOCK DETECTED: Circular Wait Cycle  ⚠️     ║\x1b[0m");
        log_error("\x1b[1;31m╚═══════════════════════════════════════════════════════════╝\x1b[0m");
        log_error("");
        log_error("  \x1b[1;31mThread 1:\x1b[0m");
        log_error("    Address:           0x%lx", (unsigned long)g_thread_registry[i].thread_id);
        log_error("    Holds Mutex:       0x%lx (%s)", b_waiting_for, mutex_b_name);
        log_error("    Waiting for:       0x%lx (%s)", waiting_for, mutex_a_name);
        log_error("");
        log_error("  \x1b[1;31mThread 2:\x1b[0m");
        log_error("    Address:           0x%lx", (unsigned long)g_thread_registry[j].thread_id);
        log_error("    Holds Mutex:       0x%lx (%s)", waiting_for, mutex_a_name);
        log_error("    Waiting for:       0x%lx (%s)", b_waiting_for, mutex_b_name);
        log_error("");
        log_error("  Deadlock Cycle:    T1 holds %s, waits for %s", mutex_b_name, mutex_a_name);
        log_error("                     T2 holds %s, waits for %s", mutex_a_name, mutex_b_name);
      }
    }
  }

  mutex_stack_free_all_threads(all_stacks, stack_counts, thread_count);
}

// ============================================================================
// Initialization
// ============================================================================

int mutex_stack_init(void) {
  // No initialization needed - registry uses lock-free atomic operations
  return 0;
}

void mutex_stack_cleanup(void) {
  // Clear registry on cleanup using atomic operations
  atomic_store_explicit(&g_thread_registry_count, 0, memory_order_release);
}
