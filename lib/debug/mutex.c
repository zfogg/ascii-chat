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
#include <ascii-chat/util/string.h>
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

// Global registry of all threads that have used mutexes
#define MAX_THREADS 256
typedef struct {
  pthread_t thread_id;
  thread_lock_stack_t *stack; // Heap-allocated stack per thread
} thread_registry_entry_t;

static thread_registry_entry_t g_thread_registry[MAX_THREADS] = {0};
static _Atomic(int) g_thread_registry_count = 0;

// Thread-local pointer to the heap-allocated stack for this thread
static __thread thread_lock_stack_t *g_thread_lock_stack = NULL;

// Track the mutexes involved in the last detected deadlock for throttling
#define MAX_CYCLE_MUTEXES 16
typedef struct {
  uintptr_t mutexes[MAX_CYCLE_MUTEXES];
  int count;
} deadlock_state_t;
static deadlock_state_t g_last_deadlock = {0};

// ============================================================================
// Thread Registry Management
// ============================================================================

// Forward declaration
static void register_thread_if_needed(void);

/**
 * @brief Get the thread-local stack for fast per-thread operations
 * Lazily allocates and registers the stack on first access
 */
static thread_lock_stack_t *get_thread_local_stack(void) {
  if (g_thread_lock_stack == NULL) {
    // Allocate stack on heap (stays valid after thread exits)
    g_thread_lock_stack = SAFE_CALLOC(1, sizeof(thread_lock_stack_t), thread_lock_stack_t *);
    if (g_thread_lock_stack) {
      register_thread_if_needed();
    }
  }
  return g_thread_lock_stack;
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
  // Note: the thread count can change concurrently, so we re-read it in the loop
  // to avoid accessing out-of-bounds memory if threads exit during iteration
  int actual_count = 0;
  for (int i = 0; i < thread_count; i++) {
    // Re-check thread count in case registry shrank
    int current_registry_count = atomic_load_explicit(&g_thread_registry_count, memory_order_seq_cst);
    if (i >= current_registry_count) {
      break;
    }

    thread_lock_stack_t *src = g_thread_registry[i].stack;
    if (!src) {
      // Stack not yet initialized for this thread, skip it
      (*out_stack_counts)[i] = 0;
      (*out_stacks)[i] = NULL;
      continue;
    }

    // Defensively get depth - it could change or be freed by another thread
    int depth = 0;
    if (src && src->depth >= 0 && src->depth < MUTEX_STACK_MAX_DEPTH) {
      depth = src->depth;
    }

    (*out_stack_counts)[i] = depth;

    if (depth > 0) {
      (*out_stacks)[i] = SAFE_MALLOC(depth * sizeof(mutex_stack_entry_t), mutex_stack_entry_t *);
      if ((*out_stacks)[i]) {
        memcpy((*out_stacks)[i], src->stack, depth * sizeof(mutex_stack_entry_t));
        actual_count++;
      }
    } else {
      (*out_stacks)[i] = NULL;
    }
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
 * @brief Find which thread holds a given mutex (-1 if none)
 */
static int find_thread_holding_mutex(int thread_count, uintptr_t mutex_key) {
  for (int i = 0; i < thread_count && i < g_thread_registry_count; i++) {
    thread_lock_stack_t *stack = g_thread_registry[i].stack;
    if (thread_holds_mutex(stack, mutex_key)) {
      return i;
    }
  }
  return -1;
}

/**
 * @brief DFS-based cycle detection in the waits-for graph
 * Returns cycle start index if found, -1 otherwise
 * Fills cycle_path with indices of threads in the cycle (if found)
 */
#define MAX_CYCLE_LEN 64
static int detect_cycle_dfs(int thread_count, int start_thread, int *cycle_path, int *cycle_len) {
  int visited[MAX_THREADS];
  int path[MAX_CYCLE_LEN];
  int path_len = 0;

  // Initialize visited array
  for (int i = 0; i < MAX_THREADS; i++) {
    visited[i] = -1; // -1 = not visited, >= 0 = index in path
  }

  // DFS starting from start_thread
  int current = start_thread;
  while (path_len < MAX_CYCLE_LEN && path_len < thread_count) {
    if (current < 0 || current >= thread_count) {
      break; // Invalid thread
    }

    // Check if current is already in path (cycle found!)
    for (int i = 0; i < path_len; i++) {
      if (path[i] == current) {
        // Found a cycle! Extract the cycle portion
        *cycle_len = path_len - i;
        for (int j = 0; j < *cycle_len; j++) {
          cycle_path[j] = path[i + j];
        }
        return i; // Return where the cycle starts
      }
    }

    // Add current to path
    path[path_len++] = current;

    // Find next thread in the waits-for graph
    thread_lock_stack_t *stack = g_thread_registry[current].stack;
    uintptr_t waiting_for = thread_waiting_for_mutex(stack);

    if (waiting_for == 0) {
      break; // No waiting, path ends
    }

    // Find who holds the mutex we're waiting for
    current = find_thread_holding_mutex(thread_count, waiting_for);
  }

  *cycle_len = 0;
  return -1; // No cycle found
}

/**
 * @brief Compare two sorted arrays of mutexes
 */
static int compare_uintptr(const void *a, const void *b) {
  uintptr_t ua = *(const uintptr_t *)a;
  uintptr_t ub = *(const uintptr_t *)b;
  if (ua < ub)
    return -1;
  if (ua > ub)
    return 1;
  return 0;
}

/**
 * @brief Check if deadlock involves different mutexes than the previous one
 * (order-independent comparison)
 */
static bool deadlock_mutexes_changed(const uintptr_t *current_mutexes, int count) {
  if (count != g_last_deadlock.count)
    return true;

  // Sort both arrays for order-independent comparison
  uintptr_t current_sorted[MAX_CYCLE_MUTEXES];
  uintptr_t last_sorted[MAX_CYCLE_MUTEXES];

  for (int i = 0; i < count; i++) {
    current_sorted[i] = current_mutexes[i];
    last_sorted[i] = g_last_deadlock.mutexes[i];
  }

  qsort(current_sorted, count, sizeof(uintptr_t), compare_uintptr);
  qsort(last_sorted, count, sizeof(uintptr_t), compare_uintptr);

  for (int i = 0; i < count; i++) {
    if (current_sorted[i] != last_sorted[i])
      return true;
  }
  return false;
}

/**
 * @brief Update tracked deadlock state
 */
static void update_deadlock_state(const uintptr_t *mutexes, int count) {
  g_last_deadlock.count = (count < MAX_CYCLE_MUTEXES) ? count : MAX_CYCLE_MUTEXES;
  for (int i = 0; i < g_last_deadlock.count; i++) {
    g_last_deadlock.mutexes[i] = mutexes[i];
  }
}

/**
 * @brief Detect circular wait deadlocks using DFS-based cycle detection
 *
 * Detects both same-thread and multi-thread deadlock patterns of any length:
 *
 * Same-thread deadlock:
 * - Thread tries to acquire a mutex it already holds (recursive lock on non-recursive mutex)
 *
 * Multi-thread circular wait (2-way, 3-way, N-way):
 * - Uses DFS to detect cycles in the "waits-for" graph
 * - Reports all threads involved in the cycle
 */
void mutex_stack_detect_deadlocks(void) {
  mutex_stack_entry_t **all_stacks = NULL;
  int *stack_counts = NULL;
  int thread_count = 0;

  if (mutex_stack_get_all_threads(&all_stacks, &stack_counts, &thread_count) != 0) {
    return;
  }

  // Check each thread for deadlock conditions
  for (int i = 0; i < thread_count && i < g_thread_registry_count; i++) {
    thread_lock_stack_t *stack_a = g_thread_registry[i].stack;
    uintptr_t waiting_for = thread_waiting_for_mutex(stack_a);

    if (waiting_for == 0)
      continue; // Thread not waiting

    // Same-thread deadlock: thread trying to acquire a mutex it already holds
    if (thread_holds_mutex(stack_a, waiting_for)) {
      log_error("%s", colored_string(LOG_COLOR_ERROR, "╔═══════════════════════════════════════════════════════════╗"));
      log_error("%s", colored_string(LOG_COLOR_ERROR, "║  ⚠️  DEADLOCK DETECTED: Same-thread Recursive Lock  ⚠️  ║"));
      log_error("%s", colored_string(LOG_COLOR_ERROR, "╚═══════════════════════════════════════════════════════════╝"));
      log_error("  Thread Address:        0x%lx", (unsigned long)g_thread_registry[i].thread_id);
      log_error("  Mutex:                 0x%lx", waiting_for);
      log_error("  Issue:                 Thread attempts recursive lock on non-recursive mutex");
      continue;
    }

    // Multi-thread circular wait: use DFS to detect cycles
    int cycle_path[MAX_CYCLE_LEN];
    int cycle_len = 0;
    int cycle_start = detect_cycle_dfs(thread_count, i, cycle_path, &cycle_len);

    if (cycle_start >= 0 && cycle_len > 1) {
      // Collect mutexes involved in this deadlock
      uintptr_t cycle_mutexes[MAX_CYCLE_MUTEXES];
      int mutex_count = 0;
      for (int k = 0; k < cycle_len && mutex_count < MAX_CYCLE_MUTEXES; k++) {
        int thread_idx = cycle_path[k];
        thread_lock_stack_t *stack = g_thread_registry[thread_idx].stack;
        uintptr_t waiting_for = thread_waiting_for_mutex(stack);
        if (waiting_for != 0) {
          cycle_mutexes[mutex_count++] = waiting_for;
        }
      }

      // Check if mutexes are different from last deadlock
      bool is_new_deadlock = deadlock_mutexes_changed(cycle_mutexes, mutex_count);
      if (is_new_deadlock) {
        update_deadlock_state(cycle_mutexes, mutex_count);
      }

      // Cycle detected! Build complete message in one string
      char cycle_msg[4096];
      int msg_len = 0;

      // Leading newline and header box
      msg_len += snprintf(cycle_msg + msg_len, sizeof(cycle_msg) - msg_len, "\n%s\n",
                          colored_string(LOG_COLOR_ERROR, "╔═════════════════════════════════╗"));
      msg_len += snprintf(cycle_msg + msg_len, sizeof(cycle_msg) - msg_len, "%s\n",
                          colored_string(LOG_COLOR_ERROR, "║  DEADLOCK: Circular Wait Cycle  ║"));
      msg_len += snprintf(cycle_msg + msg_len, sizeof(cycle_msg) - msg_len, "%s\n",
                          colored_string(LOG_COLOR_ERROR, "╚═════════════════════════════════╝"));

      // Print each thread in the cycle
      for (int k = 0; k < cycle_len; k++) {
        int thread_idx = cycle_path[k];
        int next_thread_idx = cycle_path[(k + 1) % cycle_len];

        thread_lock_stack_t *current_stack = g_thread_registry[thread_idx].stack;
        uintptr_t current_waiting = thread_waiting_for_mutex(current_stack);

        msg_len +=
            snprintf(cycle_msg + msg_len, sizeof(cycle_msg) - msg_len, "  T%d: 0x%lx waits for 0x%lx (held by 0x%lx)%s",
                     k + 1, (unsigned long)g_thread_registry[thread_idx].thread_id, current_waiting,
                     (unsigned long)g_thread_registry[next_thread_idx].thread_id, k < cycle_len - 1 ? "\n" : "");
      }

      // Log repeated deadlock detections (skip first call, throttle subsequent ones)
      if (!is_new_deadlock) {
        log_error_every(1000000, "%s", cycle_msg); // 1000000 µs = 1 second
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
