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
#include <ascii-chat/debug/named.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/platform/api.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/platform/cond.h>
#include <ascii-chat/platform/thread.h>
#include <string.h>

// ============================================================================
// Per-thread Lock Stack Storage
// ============================================================================

#define MUTEX_STACK_MAX_DEPTH 64

typedef struct {
  mutex_stack_entry_t stack[MUTEX_STACK_MAX_DEPTH];
  int depth;
  atomic_t guard;
} thread_lock_stack_t;

static void stack_lock(thread_lock_stack_t *stack) {
  int expected = 0;
  while (!atomic_cas_int(&stack->guard, &expected, 1))
    expected = 0;
}

static void stack_unlock(thread_lock_stack_t *stack) {
  atomic_store_int(&stack->guard, 0);
}

// Global registry of all threads that have used mutexes
#define MAX_THREADS 256
typedef struct {
  thread_id_t thread_id;
  thread_lock_stack_t *stack; // Heap-allocated stack per thread
} thread_registry_entry_t;

static thread_registry_entry_t g_thread_registry[MAX_THREADS] = {0};
static atomic_t g_thread_registry_count = {0};
static atomic_t g_registry_guard = {0};

static void registry_lock(void) {
  int expected = 0;
  while (!atomic_cas_int(&g_registry_guard, &expected, 1))
    expected = 0;
}

static void registry_unlock(void) {
  atomic_store_int(&g_registry_guard, 0);
}

// Thread-local storage key for per-thread lock stack
// Destructor automatically frees memory when thread exits
static tls_key_t g_tls_mutex_stack = 0;
static atomic_t g_tls_initialized = {0};

// Flag set during shutdown to prevent new stack allocations
// (threads still running at shutdown time won't leak new stacks)
static atomic_t g_shutting_down = {0};

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

/** Release a thread's stack and remove it from diagnostic snapshots. */
static void tls_mutex_stack_destructor(void *arg) {
  if (!arg)
    return;
  registry_lock();
  int count = atomic_load_int(&g_thread_registry_count);
  for (int i = 0; i < count; ++i) {
    // Windows FLS deletion can invoke this on the thread deleting the key.
    // Identify the allocation, rather than assuming the current thread owns it.
    if (g_thread_registry[i].stack == arg) {
      g_thread_registry[i].stack = NULL;
      break;
    }
  }
  registry_unlock();
  // Matches the untracked allocation used to avoid instrumenting this tracker.
  free(arg);
}

/**
 * @brief Initialize the TLS key for mutex stacks (called once at first use)
 * Uses atomic flag to ensure one-time initialization
 */
static void ensure_tls_initialized(void) {
  if (!atomic_load_bool(&g_tls_initialized)) {
    registry_lock();
    // Try to initialize TLS key with destructor
    if (!atomic_load_bool(&g_tls_initialized) &&
        ascii_tls_key_create(&g_tls_mutex_stack, tls_mutex_stack_destructor) == 0) {
      atomic_store_bool(&g_tls_initialized, true);
    }
    registry_unlock();
  }
}

// Forward declaration
static void register_thread_if_needed(void);

/**
 * @brief Get the thread-local stack for fast per-thread operations
 * Lazily allocates on first access. Destructor automatically frees on thread exit.
 * Returns NULL during shutdown to prevent new allocations from threads still running.
 */
static thread_lock_stack_t *get_thread_local_stack(void) {
  // Skip allocation during shutdown (prevents leaks from threads killed mid-shutdown)
  if (atomic_load_bool(&g_shutting_down)) {
    return NULL;
  }

  // Ensure TLS key is initialized
  ensure_tls_initialized();

  // Get current thread's stack from TLS
  thread_lock_stack_t *stack = (thread_lock_stack_t *)ascii_tls_get(g_tls_mutex_stack);

  if (stack == NULL) {
    // Allocate stack on heap for this thread
    // Use raw malloc() to avoid recursive mutex locking in debug subsystem
    stack = (thread_lock_stack_t *)malloc(sizeof(thread_lock_stack_t));
    if (stack) {
      memset(stack, 0, sizeof(thread_lock_stack_t));
      // Store in TLS (destructor will free it when thread exits)
      ascii_tls_set(g_tls_mutex_stack, stack);
      register_thread_if_needed();
    }
  }
  return stack;
}

/** Register a thread's stack, reusing retired slots and recycled thread IDs. */
static void register_thread_if_needed(void) {
  static _Thread_local bool registered = false;
  if (registered)
    return;
  thread_lock_stack_t *local_stack = get_thread_local_stack();
  if (!local_stack)
    return;
  thread_id_t current_thread = asciichat_thread_self();
  registry_lock();
  int count = atomic_load_int(&g_thread_registry_count);
  int slot = -1;
  for (int i = 0; i < count; ++i) {
    if (asciichat_thread_equal(g_thread_registry[i].thread_id, current_thread)) {
      slot = i;
      break;
    }
    if (!g_thread_registry[i].stack && slot < 0)
      slot = i;
  }
  if (slot < 0 && count < MAX_THREADS)
    slot = count++;
  if (slot >= 0) {
    g_thread_registry[slot].thread_id = current_thread;
    g_thread_registry[slot].stack = local_stack;
    atomic_store_int(&g_thread_registry_count, count);
  }
  registry_unlock();
  registered = true;
}

// ============================================================================
// Public Stack Operations (Debug builds only)
// ============================================================================

#ifndef NDEBUG

static void mutex_stack_push(uintptr_t mutex_key, const char *mutex_name, mutex_stack_state_t state) {
  thread_lock_stack_t *stack = get_thread_local_stack();
  if (!stack) {
    return;
  }

  // Register this thread in the global registry on first mutex use
  register_thread_if_needed();

  stack_lock(stack);
  if (stack->depth >= MUTEX_STACK_MAX_DEPTH) {
    stack_unlock(stack);
    return;
  }
  stack->stack[stack->depth].mutex_key = mutex_key;
  stack->stack[stack->depth].mutex_name = mutex_name;
  stack->stack[stack->depth].state = state;
  stack->stack[stack->depth].timestamp_ns = time_get_ns();
  stack->depth++;
  stack_unlock(stack);
}

void mutex_stack_push_pending(uintptr_t mutex_key, const char *mutex_name) {
  mutex_stack_push(mutex_key, mutex_name, MUTEX_STACK_STATE_PENDING);
}

void mutex_stack_push_locked(uintptr_t mutex_key, const char *mutex_name) {
  mutex_stack_push(mutex_key, mutex_name, MUTEX_STACK_STATE_LOCKED);
}

void mutex_stack_mark_locked(uintptr_t mutex_key) {
  thread_lock_stack_t *stack = get_thread_local_stack();
  if (!stack) {
    return;
  }

  // Mark the top of the stack as locked
  stack_lock(stack);
  if (stack->depth == 0) {
    stack_unlock(stack);
    return;
  }
  int top = stack->depth - 1;
  if (stack->stack[top].mutex_key == mutex_key) {
    stack->stack[top].state = MUTEX_STACK_STATE_LOCKED;
    stack->stack[top].timestamp_ns = time_get_ns();
  }
  stack_unlock(stack);

  // Thread-local only. Registry is populated on-demand by mutex_stack_get_all_threads()
}

void mutex_stack_pop(uintptr_t mutex_key) {
  thread_lock_stack_t *stack = get_thread_local_stack();
  if (!stack) {
    return;
  }

  // Validate top matches
  stack_lock(stack);
  if (stack->depth == 0) {
    stack_unlock(stack);
    return;
  }
  // Locks may be released out of acquisition order. Remove the most recent
  // matching acquisition, preserving any recursive acquisitions below it.
  for (int i = stack->depth - 1; i >= 0; --i) {
    if (stack->stack[i].mutex_key == mutex_key) {
      memmove(&stack->stack[i], &stack->stack[i + 1],
              (size_t)(stack->depth - i - 1) * sizeof(stack->stack[0]));
      --stack->depth;
      break;
    }
  }
  stack_unlock(stack);

  // Thread-local only. Registry is populated on-demand by mutex_stack_get_all_threads()
}

#endif

int mutex_stack_get_current(mutex_stack_entry_t *out_entries, int max_entries) {
  thread_lock_stack_t *stack = get_thread_local_stack();
  if (!stack || !out_entries) {
    return 0;
  }

  stack_lock(stack);
  int depth = stack->depth;
  int count = (depth < max_entries) ? depth : max_entries;
  memcpy(out_entries, stack->stack, count * sizeof(mutex_stack_entry_t));
  stack_unlock(stack);
  return depth; // Return actual depth even if truncated
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
  int thread_count = atomic_load_int(&g_thread_registry_count);

  // Allocate arrays for threads in the registry
  // Note: SAFE_MALLOC takes bytes as first parameter, not count
  *out_stacks = SAFE_CALLOC(thread_count, sizeof(mutex_stack_entry_t *), mutex_stack_entry_t **);
  *out_stack_counts = SAFE_CALLOC(thread_count, sizeof(int), int *);

  if (!*out_stacks || !*out_stack_counts) {
    SAFE_FREE(*out_stacks);
    SAFE_FREE(*out_stack_counts);
    return -1;
  }

  // Copy each thread's stack from the registry
  // Note: the thread count can change concurrently, so we re-read it in the loop
  // to avoid accessing out-of-bounds memory if threads exit during iteration
  for (int i = 0; i < thread_count; i++) {
    // Re-check thread count in case registry shrank
    int current_registry_count = atomic_load_int(&g_thread_registry_count);
    if (i >= current_registry_count) {
      break;
    }

    // Allocate before locking because tracked allocation updates the lock stack.
    (*out_stacks)[i] = SAFE_MALLOC(MUTEX_STACK_MAX_DEPTH * sizeof(mutex_stack_entry_t), mutex_stack_entry_t *);
    (*out_stack_counts)[i] = 0;
    registry_lock();
    thread_lock_stack_t *src = g_thread_registry[i].stack;
    if (src) {
      stack_lock(src);
      int depth = src->depth;
      (*out_stack_counts)[i] = depth;
      memcpy((*out_stacks)[i], src->stack, depth * sizeof(mutex_stack_entry_t));
      stack_unlock(src);
    }
    registry_unlock();
  }

  *out_thread_count = thread_count;
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
  if (!stack) {
    return false;
  }
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
  if (!stack) {
    return 0;
  }
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
static int find_thread_holding_mutex(thread_lock_stack_t *snapshots, int thread_count, uintptr_t mutex_key) {
  for (int i = 0; i < thread_count; i++) {
    thread_lock_stack_t *stack = &snapshots[i];
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
static int detect_cycle_dfs(thread_lock_stack_t *snapshots, int thread_count, int start_thread, int *cycle_path,
                            int *cycle_len) {
  int visited[MAX_THREADS];
  int path[MAX_CYCLE_LEN];
  int path_len = 0;

  // Initialize visited array
  for (int i = 0; i < MAX_THREADS; i++) {
    visited[i] = -1; // -1 = not visited, >= 0 = index in path
  }

  // DFS starting from start_thread
  int current = start_thread;
  while (path_len <= MAX_CYCLE_LEN && path_len <= thread_count) {
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

    if (path_len == MAX_CYCLE_LEN || path_len == thread_count)
      break;
    // Add current to path
    path[path_len++] = current;

    // Find next thread in the waits-for graph
    thread_lock_stack_t *stack = &snapshots[current];
    uintptr_t waiting_for = thread_waiting_for_mutex(stack);

    if (waiting_for == 0) {
      break; // No waiting, path ends
    }

    // Find who holds the mutex we're waiting for
    current = find_thread_holding_mutex(snapshots, thread_count, waiting_for);
  }

  *cycle_len = 0;
  return -1; // No cycle found
}

bool mutex_stack_try_snapshot(mutex_wait_snapshot_t *waits, size_t capacity, size_t *count, bool *limited) {
  // A single collector owns this scratch space. Never dereference names from
  // copied stacks: their registry entries may have been removed meanwhile.
  static thread_lock_stack_t snapshots[MAX_THREADS];
  static thread_id_t ids[MAX_THREADS];
  static atomic_flag sampling = ATOMIC_FLAG_INIT;
  if (atomic_flag_test_and_set(&sampling))
    return false;
  uint64_t expected = 0;
  if (!atomic_compare_exchange_strong(&g_registry_guard.impl, &expected, 1)) {
    atomic_flag_clear(&sampling);
    return false;
  }
  int threads = atomic_load_int_impl(&g_thread_registry_count);
  bool complete = true;
  for (int i = 0; i < threads; ++i) {
    thread_lock_stack_t *stack = g_thread_registry[i].stack;
    ids[i] = g_thread_registry[i].thread_id;
    snapshots[i].depth = 0;
    if (!stack)
      continue;
    expected = 0;
    if (!atomic_compare_exchange_strong(&stack->guard.impl, &expected, 1)) {
      complete = false;
      break;
    }
    snapshots[i].depth = stack->depth;
    memcpy(snapshots[i].stack, stack->stack, sizeof(stack->stack));
    atomic_store(&stack->guard.impl, 0);
  }
  atomic_store(&g_registry_guard.impl, 0);
  if (complete) {
    *count = 0;
    *limited = threads >= MAX_CYCLE_LEN;
    for (int i = 0; i < threads; ++i) {
      uintptr_t key = thread_waiting_for_mutex(&snapshots[i]);
      *limited |= snapshots[i].depth == MUTEX_STACK_MAX_DEPTH;
      if (!key)
        continue;
      if (*count == capacity) {
        *limited = true;
        continue;
      }
      int path[MAX_CYCLE_LEN], length = 0;
      bool cycle = thread_holds_mutex(&snapshots[i], key);
      if (detect_cycle_dfs(snapshots, threads, i, path, &length) >= 0)
        for (int j = 0; j < length; ++j)
          cycle |= path[j] == i;
      waits[(*count)++] = (mutex_wait_snapshot_t){key, (uintptr_t)ids[i],
                                                snapshots[i].stack[snapshots[i].depth - 1].timestamp_ns, cycle};
    }
  }
  atomic_flag_clear(&sampling);
  return complete;
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

  // Analyze copies instead of stacks changing concurrently on other threads.
  thread_lock_stack_t *snapshots = SAFE_CALLOC(thread_count, sizeof(thread_lock_stack_t), thread_lock_stack_t *);
  for (int i = 0; i < thread_count; i++) {
    snapshots[i].depth = stack_counts[i];
    if (stack_counts[i] > 0)
      memcpy(snapshots[i].stack, all_stacks[i], stack_counts[i] * sizeof(mutex_stack_entry_t));
  }

  // Check each thread for deadlock conditions
  for (int i = 0; i < thread_count; i++) {
    thread_lock_stack_t *stack_a = &snapshots[i];
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
    int cycle_start = detect_cycle_dfs(snapshots, thread_count, i, cycle_path, &cycle_len);

    if (cycle_start >= 0 && cycle_len > 1) {
      // Collect mutexes involved in this deadlock
      uintptr_t cycle_mutexes[MAX_CYCLE_MUTEXES];
      int mutex_count = 0;
      for (int k = 0; k < cycle_len && mutex_count < MAX_CYCLE_MUTEXES; k++) {
        int thread_idx = cycle_path[k];
        thread_lock_stack_t *stack = &snapshots[thread_idx];
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

        thread_lock_stack_t *current_stack = &snapshots[thread_idx];
        uintptr_t current_waiting = thread_waiting_for_mutex(current_stack);

        char thread_name[256], mutex_name[256], held_by_name[256];
        NAMED_GET_BY_PTR((uintptr_t)g_thread_registry[thread_idx].thread_id, thread_name, sizeof(thread_name));
        NAMED_GET_BY_PTR((uintptr_t)current_waiting, mutex_name, sizeof(mutex_name));
        NAMED_GET_BY_PTR((uintptr_t)g_thread_registry[next_thread_idx].thread_id, held_by_name, sizeof(held_by_name));

        msg_len += snprintf(cycle_msg + msg_len, sizeof(cycle_msg) - msg_len, "  T%d: %s waits for %s (held by %s)%s",
                            k + 1, thread_name, mutex_name, held_by_name, k < cycle_len - 1 ? "\n" : "");
      }

      // Log repeated deadlock detections (skip first call, throttle subsequent ones)
      if (!is_new_deadlock) {
        log_error_every(1000000, "%s", cycle_msg); // 1000000 µs = 1 second
      }
    }
  }

  SAFE_FREE(snapshots);
  mutex_stack_free_all_threads(all_stacks, stack_counts, thread_count);
}

// ============================================================================
// Condition Variable Deadlock Detection (Debug builds only)
// ============================================================================

#ifndef NDEBUG

#define COND_DEADLOCK_THRESHOLD_NS (5ULL * 1000000000ULL) // 5 seconds

typedef struct {
  uint64_t waiting_count;
  uint64_t last_wait_time_ns;
  uint64_t last_signal_time_ns;
  uintptr_t last_waiting_key;
  const char *last_wait_file;
  int last_wait_line;
  const char *last_wait_func;
  const mutex_t *last_wait_mutex;
} cond_deadlock_snapshot_t;

static void copy_cond_deadlock_state(uintptr_t key, void *data) {
  const cond_t *cond = (const cond_t *)key;
  cond_deadlock_snapshot_t *snapshot = data;
  snapshot->waiting_count = atomic_load_u64(&cond->waiting_count);
  snapshot->last_wait_time_ns = cond->last_wait_time_ns;
  snapshot->last_signal_time_ns = cond->last_signal_time_ns;
  snapshot->last_waiting_key = cond->last_waiting_key;
  snapshot->last_wait_file = cond->last_wait_file;
  snapshot->last_wait_line = cond->last_wait_line;
  snapshot->last_wait_func = cond->last_wait_func;
  snapshot->last_wait_mutex = cond->last_wait_mutex;
}

/**
 * @brief Callback for checking condition variable deadlocks
 * @param key Registry key of the primitive
 * @param name Human-readable name of the primitive
 * @param user_data Unused
 */
static void cond_deadlock_check_callback(uintptr_t key, const char *name, void *user_data) {
  (void)user_data; // Unused

  cond_deadlock_snapshot_t snapshot;
  if (!named_registry_read(key, "cond", copy_cond_deadlock_state, &snapshot)) {
    return;
  }

  const cond_deadlock_snapshot_t *cond = &snapshot;
  if (cond->waiting_count == 0) {
    return; // No threads waiting, nothing to check
  }

  // Skip deadlock checks for thread pool work queues - it's normal for worker threads
  // to wait idly when there's no work to process
  if (name && strstr(name, "task_available") != NULL) {
    return;
  }

  uint64_t now = time_get_ns();
  uint64_t stuck_ns = now - cond->last_wait_time_ns;
  bool no_signal_since_wait = (cond->last_signal_time_ns == 0 || cond->last_signal_time_ns < cond->last_wait_time_ns);

  if (stuck_ns < COND_DEADLOCK_THRESHOLD_NS || !no_signal_since_wait) {
    return; // Not stuck yet or was signaled recently
  }

  // Condition variable appears to be stuck - log detailed diagnostic info
  char stuck_str[64];
  time_pretty(stuck_ns, -1, stuck_str, sizeof(stuck_str));

  char cond_buf[1024] = {0};
  int cond_written =
      safe_snprintf(cond_buf, sizeof(cond_buf),
                    "Stuck cond '%s': %lu thread(s) waiting %s with no signal (most recent waiter: 0x%lx)\n", name,
                    cond->waiting_count, stuck_str, (unsigned long)cond->last_waiting_key);

  cond_written +=
      safe_snprintf(cond_buf + cond_written, sizeof(cond_buf) - cond_written, "  wait entered at %s:%d %s()\n",
                    extract_project_relative_path(cond->last_wait_file), cond->last_wait_line, cond->last_wait_func);

  if (cond_written >= 0 && cond->last_wait_mutex) {
    safe_snprintf(cond_buf + cond_written, sizeof(cond_buf) - cond_written,
                  "  associated mutex: %p (cannot safely inspect without lock ownership)",
                  (void *)cond->last_wait_mutex);
  }

  log_warn_every(500 * NS_PER_MS_INT, "%s", cond_buf);
}

// Forward declare function from sync.c to check cleanup status
extern bool debug_sync_is_cleanup_in_progress(void);

/**
 * @brief Check all condition variables for deadlocks
 *
 * Scans all registered condition variables and logs warnings for any that
 * have been waiting without signal for longer than COND_DEADLOCK_THRESHOLD_NS.
 * Called periodically by the debug thread (every 100ms).
 *
 * Skips checks during shutdown to avoid accessing freed memory.
 *
 * @ingroup debug_sync
 */
void debug_sync_check_cond_deadlocks(void) {
  if (debug_sync_is_cleanup_in_progress()) {
    return;
  }
  bool completed;
  (void)named_registry_for_each(cond_deadlock_check_callback, NULL, &completed);
}

#endif // NDEBUG

// ============================================================================
// Initialization
// ============================================================================

int mutex_stack_init(void) {
  // No initialization needed - registry uses lock-free atomic operations
  return 0;
}

void mutex_stack_cleanup_current_thread(void) {
  // Explicitly free the current thread's TLS stack
  // This is used to prevent leaks when TLS destructors might not run reliably
  // (e.g., debug threads exiting before mutex_stack_cleanup() deletes the TLS key)

  if (!atomic_load_bool(&g_tls_initialized)) {
    return; // TLS not initialized, nothing to clean up
  }

  thread_lock_stack_t *stack = (thread_lock_stack_t *)ascii_tls_get(g_tls_mutex_stack);
  if (!stack) {
    return; // No stack allocated for this thread
  }

  // Clear from TLS
  ascii_tls_set(g_tls_mutex_stack, NULL);

  // Update registry if this thread is registered
  thread_id_t current_thread = asciichat_thread_self();
  registry_lock();
  int count = atomic_load_int(&g_thread_registry_count);
  for (int i = 0; i < count; i++) {
    if (asciichat_thread_equal(g_thread_registry[i].thread_id, current_thread) && g_thread_registry[i].stack == stack) {
      g_thread_registry[i].stack = NULL; // Mark as freed in registry
      break;
    }
  }

  // Free the stack (raw free to match raw malloc above)
  free(stack);
  registry_unlock();
}

void mutex_stack_cleanup(void) {
  // Signal shutdown to prevent new allocations from threads still running
  atomic_store_bool(&g_shutting_down, true);

  // Call only after tracked workers stop. Windows FLS key deletion invokes
  // destructors; POSIX key deletion does not. Delete first so those callbacks
  // retire their entries before we free any remaining allocations.
  if (atomic_load_bool(&g_tls_initialized)) {
    ascii_tls_key_delete(g_tls_mutex_stack);
    atomic_store_bool(&g_tls_initialized, false);
  }
  registry_lock();
  int count = atomic_load_int(&g_thread_registry_count);
  for (int i = 0; i < count; ++i) {
    free(g_thread_registry[i].stack);
    g_thread_registry[i].stack = NULL;
  }
  registry_unlock();

  // Clear registry on cleanup using atomic operations
  atomic_store_int(&g_thread_registry_count, 0);
}

// ============================================================================
// Release Build Stubs (NDEBUG)
// ============================================================================
// These no-op implementations replace the debug implementations in release builds
// (when debug/mutex.c is not compiled into the library)

#ifdef NDEBUG

void mutex_stack_push_locked(uintptr_t mutex_key, const char *mutex_name) {
  (void)mutex_key;
  (void)mutex_name;
}

void mutex_stack_push_pending(uintptr_t mutex_key, const char *mutex_name) {
  (void)mutex_key;
  (void)mutex_name;
  // No-op in release builds
}

void mutex_stack_mark_locked(uintptr_t mutex_key) {
  (void)mutex_key;
  // No-op in release builds
}

void mutex_stack_pop(uintptr_t mutex_key) {
  (void)mutex_key;
  // No-op in release builds
}

#endif
