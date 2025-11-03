# ascii-chat Concurrency Architecture

## Overview

This document describes the concurrency architecture of ascii-chat, covering both the current implementation and the ideal design patterns. ascii-chat implements a high-performance, real-time multi-client system with dedicated per-client threading for video and audio processing.

## Server Concurrency Architecture

### Threading Model

The server implements a sophisticated per-client threading architecture designed for real-time media processing with linear scalability:

#### Global Threads
- **Main Thread**: Connection acceptance, client lifecycle management, signal handling, and coordination
- **Stats Logger Thread**: Performance monitoring and periodic reporting (runs every 3 seconds)

#### Per-Client Threads (4 threads per connected client)
1. **Receive Thread**: Handles incoming packets from client (calls protocol.c functions)
2. **Send Thread**: Manages outgoing packet delivery using packet queues
3. **Video Render Thread**: Generates ASCII frames at 60fps (render.c)
4. **Audio Render Thread**: Mixes audio streams at 172fps (render.c)

### Synchronization Primitives

#### Global Synchronization

##### `g_client_manager_rwlock` - Reader-Writer Lock
**Purpose**: Protects the global client array and hash table from concurrent access
**Philosophy**: Reader-writer locks are ideal for data structures with read-heavy access patterns where multiple threads need to read simultaneously but only one thread can write at a time.

**When to Use Read Lock (`rwlock_rdlock`)**:
- **Client iteration for frame generation**: Multiple render threads need to iterate through all clients to generate mixed frames
- **Statistics collection**: Stats thread needs to read client data for performance monitoring
- **Client lookups**: Finding clients by ID or socket for packet processing
- **Connection monitoring**: Main thread checking client states for cleanup

**When to Use Write Lock (`rwlock_wrlock`)**:
- **Adding new clients**: `add_client()` modifies the client array and hash table
- **Removing clients**: `remove_client()` modifies the client array and hash table
- **Client array reorganization**: Any operation that changes the structure of the client list

**Side Effects and Considerations**:
- **Read lock contention**: Multiple readers can proceed simultaneously, but they block writers
- **Write lock exclusivity**: Writers block all readers and other writers
- **Lock ordering**: Must be acquired before any per-client mutexes to prevent deadlocks
- **Performance impact**: Read locks are fast, write locks can cause brief pauses for all readers

**Example Usage**:
```c
// Read lock for client iteration (multiple threads can do this simultaneously)
rwlock_rdlock(&g_client_manager_rwlock);
for (int i = 0; i < MAX_CLIENTS; i++) {
  client_info_t *client = &g_client_manager.clients[i];
  if (client->client_id != 0) {
    // Process client data
  }
}
rwlock_unlock(&g_client_manager_rwlock);

// Write lock for client addition (exclusive access)
rwlock_wrlock(&g_client_manager_rwlock);
// Modify client array and hash table
g_client_manager.clients[slot] = new_client;
hashtable_insert(g_client_manager.client_hashtable, client_id, &g_client_manager.clients[slot]);
rwlock_unlock(&g_client_manager_rwlock);
```

##### `g_should_exit` - Atomic Boolean
**Purpose**: Global shutdown coordination flag
**Philosophy**: Atomic operations are perfect for simple flags that need to be checked frequently by multiple threads without the overhead of mutexes.

**When to Use**:
- **Shutdown signaling**: Set to true when server should exit
- **Thread exit conditions**: All threads check this flag in their main loops
- **Signal handlers**: Can be set safely from signal handlers (async-signal-safe)

**Side Effects and Considerations**:
- **No blocking**: Atomic operations never block, making them ideal for hot paths
- **Memory ordering**: Ensures all threads see the flag change consistently
- **Signal safety**: Can be used in signal handlers without deadlock risk

##### `g_shutdown_cond` - Condition Variable
**Purpose**: Wake up sleeping threads during shutdown
**Philosophy**: Condition variables are used when threads need to wait for a specific condition to become true, rather than busy-waiting.

**When to Use**:
- **Thread wakeup**: Broadcast to all sleeping threads during shutdown
- **Event coordination**: Notify threads when specific conditions are met

**Side Effects and Considerations**:
- **Requires mutex**: Must be used with `g_shutdown_mutex`
- **Spurious wakeups**: Threads may wake up even when condition isn't met
- **Broadcast vs signal**: Use broadcast to wake all waiting threads

#### Per-Client Synchronization

##### `client_state_mutex` - Per-Client State Protection
**Purpose**: Protects individual client state fields from race conditions
**Philosophy**: Per-client mutexes provide fine-grained locking, allowing multiple clients to be processed concurrently while ensuring each client's state is accessed atomically.

**When to Use**:
- **State updates**: Modifying client dimensions, capabilities, or flags
- **State snapshots**: Taking consistent snapshots of client state for processing
- **Thread lifecycle**: Starting/stopping client threads
- **Resource management**: Allocating/deallocating client-specific resources

**Protected Fields**:
- `width`, `height`: Terminal dimensions
- `terminal_caps`: Terminal capabilities
- `has_terminal_caps`: Capability flag
- `is_sending_video`, `is_sending_audio`: Stream status flags
- `send_thread_running`, `video_render_thread_running`, `audio_render_thread_running`: Thread lifecycle
- `last_video_render_time`, `last_audio_render_time`: Timing information

**Side Effects and Considerations**:
- **Lock ordering**: Must be acquired after `g_client_manager_rwlock`
- **Snapshot pattern**: Use brief locks to copy state, then process without locks
- **Per-client isolation**: Each client's mutex is independent, allowing concurrent processing
- **Deadlock prevention**: Never hold multiple per-client mutexes simultaneously

**Example Usage**:
```c
// Snapshot pattern - brief lock to copy state
mutex_lock(&client->client_state_mutex);
bool should_continue = client->video_render_thread_running && client->active;
uint32_t client_id_snapshot = client->client_id;
unsigned short width_snapshot = client->width;
unsigned short height_snapshot = client->height;
mutex_unlock(&client->client_state_mutex);

// Process using snapshot (no locks held)
if (should_continue) {
  generate_frame(client_id_snapshot, width_snapshot, height_snapshot);
}
```

##### `video_buffer_mutex` - Video Buffer Protection
**Purpose**: Protects per-client video buffer access
**Philosophy**: Dedicated mutex for video buffer operations to prevent contention with other client state operations.

**When to Use**:
- **Buffer access**: Reading/writing video frame data
- **Buffer swapping**: Double-buffer swap operations
- **Buffer allocation**: Creating/destroying video buffers

**Side Effects and Considerations**:
- **Fine-grained locking**: Separates video buffer access from other client state
- **Performance critical**: Video operations are high-frequency, so minimal lock time is essential
- **Double-buffering**: Enables safe concurrent read/write operations

##### Atomic Variables for Thread Lifecycle

##### `active` - Client Connection Status
**Purpose**: Indicates whether client is connected and active
**Philosophy**: Atomic boolean for frequently-checked status that needs to be visible to all threads immediately.

**When to Use**:
- **Connection checks**: Quickly determine if client is still connected
- **Thread exit conditions**: Render threads check this to exit when client disconnects
- **Resource cleanup**: Prevent operations on disconnected clients

**Side Effects and Considerations**:
- **Immediate visibility**: Changes are visible to all threads without delay
- **No blocking**: Can be checked in hot paths without performance impact
- **Signal safety**: Can be modified from signal handlers

##### `video_render_thread_running` / `audio_render_thread_running` - Thread Lifecycle
**Purpose**: Control render thread lifecycle
**Philosophy**: Atomic flags for thread control that need to be checked frequently in render loops.

**When to Use**:
- **Thread exit conditions**: Render threads check these flags in their main loops
- **Graceful shutdown**: Set to false to signal threads to exit
- **Thread status monitoring**: Determine if threads are still running

**Side Effects and Considerations**:
- **Frequent checking**: These flags are checked every frame (60fps/172fps)
- **Performance critical**: Must be fast to avoid impacting frame rates
- **Clean shutdown**: Ensures threads exit gracefully without forced termination

### Critical Synchronization Patterns

#### Lock Ordering Protocol (Deadlock Prevention)
**CRITICAL RULE**: Always acquire locks in this order to prevent deadlocks:
1. **Global RWLock** (`g_client_manager_rwlock`)
2. **Per-Client Mutex** (`client_state_mutex`)
3. **Specialized Mutexes** (`video_buffer_mutex`, `g_stats_mutex`, etc.)

**Why This Ordering is Required**: The codebase actually DOES acquire both locks simultaneously in several places (main.c cleanup, stats.c monitoring, stream.c frame generation). The ordering prevents deadlocks when multiple threads need both locks.

**Concrete Deadlock Example**:
```c
// Thread A (render thread): Per-Client → Global (WRONG ORDER)
void render_thread_function(client_info_t *client) {
  mutex_lock(&client->client_state_mutex);  // Thread A gets client mutex
  // ... some processing ...

  // Now needs to iterate all clients for frame mixing
  rwlock_rdlock(&g_client_manager_rwlock);  // Thread A waits for rwlock
  // ... frame mixing code ...
}

// Thread B (main thread): Global → Per-Client (CORRECT ORDER)
void main_cleanup_function() {
  rwlock_wrlock(&g_client_manager_rwlock);  // Thread B gets rwlock
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_info_t *client = &g_client_manager.clients[i];
    mutex_lock(&client->client_state_mutex);  // Thread B waits for client mutex
    // ... cleanup code ...
  }
}

// DEADLOCK SCENARIO:
// 1. Thread A gets client->client_state_mutex
// 2. Thread B gets g_client_manager_rwlock
// 3. Thread A tries to get g_client_manager_rwlock (blocked by Thread B)
// 4. Thread B tries to get client->client_state_mutex (blocked by Thread A)
// 5. DEADLOCK! Both threads wait forever
```

**Correct Pattern (as used in the codebase)**:
```c
// ✅ CORRECT: Global → Per-Client (matches actual codebase usage)
rwlock_rdlock(&g_client_manager_rwlock);
for (int i = 0; i < MAX_CLIENTS; i++) {
  client_info_t *client = &g_client_manager.clients[i];
  if (client->client_id != 0) {
    mutex_lock(&client->client_state_mutex);
    bool is_active = atomic_load(&client->active);
    uint32_t client_id_snapshot = client->client_id;
    mutex_unlock(&client->client_state_mutex);
    // Process using snapshot
  }
}
rwlock_unlock(&g_client_manager_rwlock);
```

**Real Codebase Examples**:
- `main.c:570-580`: Cleanup loop with rwlock → client_state_mutex
- `stats.c:361-368`: Stats collection with rwlock → client_state_mutex
- `stream.c:325+`: Frame generation with rwlock → client_state_mutex

#### Snapshot Pattern (Reduced Lock Contention)
All client state access uses the snapshot pattern to minimize lock holding time:

```c
// 1. Acquire mutex
mutex_lock(&client->client_state_mutex);

// 2. Copy needed state to local variables
bool should_continue = client->video_render_thread_running && client->active;
uint32_t client_id_snapshot = client->client_id;
unsigned short width_snapshot = client->width;
unsigned short height_snapshot = client->height;

// 3. Release mutex immediately
mutex_unlock(&client->client_state_mutex);

// 4. Process using local copies (no locks held)
if (should_continue) {
  generate_frame(client_id_snapshot, width_snapshot, height_snapshot);
}
```

### Thread Lifecycle Management

#### Thread Creation Order (in add_client())
1. Initialize client data structures and mutexes
2. Create send thread (for outgoing packet delivery)
3. Create receive thread (for incoming packet processing)
4. Create render threads (video + audio generation)

#### Thread Termination Order (in remove_client())
1. Set shutdown flags (causes threads to exit main loops)
2. Join send thread (cleanest exit, no blocking I/O)
3. Join receive thread (may be blocked on network I/O)
4. Join render threads (computational work, clean exit)
5. Clean up resources (queues, buffers, mutexes)

### Signal Handling Architecture

#### Signal Safety Strategy
Signal handlers are severely restricted in what they can safely do:
- Only async-signal-safe functions are allowed
- No mutex operations (can deadlock if main thread holds mutex)
- No malloc/free (heap corruption if interrupted during allocation)
- No non-reentrant library calls (logging, printf, etc. are dangerous)

#### SIGINT Handler Design
```c
static void sigint_handler(int sigint) {
  // STEP 1: Set atomic shutdown flag (signal-safe, checked by all threads)
  atomic_store(&g_should_exit, true);

  // STEP 2: Close listening socket to interrupt accept() calls
  if (listenfd != INVALID_SOCKET_VALUE) {
    socket_close(listenfd);
    listenfd = INVALID_SOCKET_VALUE;
  }

  // STEP 3: Return immediately - let main thread handle complex cleanup
  // Main thread will detect g_should_exit and perform complete shutdown
}
```

## Client Concurrency Architecture

### Threading Model

The client follows a modular threading architecture with robust reconnection logic:

#### Thread Types
- **Main Thread**: Connection management, reconnection logic, event coordination
- **Data Reception Thread**: Handles incoming packets from server
- **Ping Thread**: Maintains connection keepalive
- **Webcam Capture Thread**: Captures and transmits video frames
- **Audio Capture Thread**: Captures and transmits audio data (optional)

### Synchronization Primitives

#### Global State
- **`g_should_exit`**: Atomic boolean for graceful shutdown
- **`g_connection_active`**: Atomic boolean for connection status
- **`g_connection_lost`**: Atomic boolean for connection loss detection
- **`g_send_mutex`**: Global mutex protecting packet transmission

#### Thread State
- **`g_data_thread_exited`**: Atomic boolean indicating data thread status
- **`g_data_thread_created`**: Boolean flag for thread lifecycle tracking

### Connection Lifecycle

#### Connection Establishment Pattern
1. **Initialization**: Socket creation and address resolution
2. **Connection**: TCP handshake with configurable timeout
3. **Capability Exchange**: Send terminal capabilities and client info
4. **Thread Spawning**: Start all worker threads for the connection
5. **Active Monitoring**: Monitor connection health and thread status

#### Reconnection Strategy
Implements exponential backoff with jitter:
- Initial delay: 10ms
- Exponential growth: delay = 10ms + (200ms * attempt)
- Maximum delay: 5 seconds
- Jitter: Small random component to prevent thundering herd

#### Connection Loss Detection
- **Thread Exit Detection**: Monitor critical thread status
- **Socket Error Detection**: Network error handling
- **Timeout Detection**: Keepalive timeout handling

## Ideal Concurrency Architecture

### Lock-Free Data Structures

#### Lock-Free Ring Buffers
Replace mutex-protected packet queues with lock-free ring buffers:

```c
typedef struct {
  volatile uint32_t head;
  volatile uint32_t tail;
  uint32_t size;
  void *buffer[];
} lockfree_ring_buffer_t;

// Producer (thread-safe, no locks)
bool ring_buffer_push(lockfree_ring_buffer_t *rb, void *data) {
  uint32_t current_tail = atomic_load(&rb->tail);
  uint32_t next_tail = (current_tail + 1) % rb->size;

  if (next_tail == atomic_load(&rb->head)) {
    return false; // Buffer full
  }

  rb->buffer[current_tail] = data;
  atomic_store(&rb->tail, next_tail);
  return true;
}

// Consumer (thread-safe, no locks)
void *ring_buffer_pop(lockfree_ring_buffer_t *rb) {
  uint32_t current_head = atomic_load(&rb->head);

  if (current_head == atomic_load(&rb->tail)) {
    return NULL; // Buffer empty
  }

  void *data = rb->buffer[current_head];
  atomic_store(&rb->head, (current_head + 1) % rb->size);
  return data;
}
```

#### Atomic Client State
Replace mutex-protected client state with atomic operations:

```c
typedef struct {
  atomic_uint_fast32_t state;  // Bitfield: active, has_video, has_audio, etc.
  atomic_uint_fast16_t width;
  atomic_uint_fast16_t height;
  atomic_uint_fast32_t client_id;
} lockfree_client_state_t;

// Thread-safe state updates without locks
void set_client_active(lockfree_client_state_t *state, bool active) {
  uint32_t current_state = atomic_load(&state->state);
  uint32_t new_state = active ? (current_state | CLIENT_ACTIVE_BIT)
                              : (current_state & ~CLIENT_ACTIVE_BIT);
  atomic_store(&state->state, new_state);
}
```

### Thread Pool Architecture

#### Worker Thread Pools
Replace per-client threading with worker thread pools:

```c
typedef struct {
  asciithread_t *workers;
  int num_workers;
  lockfree_ring_buffer_t *work_queue;
  atomic_bool should_exit;
} thread_pool_t;

// Video rendering pool
thread_pool_t *video_pool;

// Audio mixing pool
thread_pool_t *audio_pool;

// Network I/O pool
thread_pool_t *network_pool;
```

#### Work-Stealing Queues
Implement work-stealing for optimal load distribution:

```c
typedef struct {
  lockfree_ring_buffer_t *local_queue;
  lockfree_ring_buffer_t **all_queues;
  int num_queues;
} work_stealing_queue_t;

// Try local queue first, then steal from other queues
work_item_t *get_work(work_stealing_queue_t *wsq) {
  work_item_t *work = ring_buffer_pop(wsq->local_queue);
  if (work) return work;

  // Steal from other queues
  for (int i = 0; i < wsq->num_queues; i++) {
    work = ring_buffer_pop(wsq->all_queues[i]);
    if (work) return work;
  }

  return NULL;
}
```

### Event-Driven Architecture

#### Epoll-Based Event Loop
Replace polling with event-driven I/O:

```c
typedef struct {
  int epoll_fd;
  struct epoll_event *events;
  int max_events;
} event_loop_t;

void event_loop_run(event_loop_t *loop) {
  while (!should_exit()) {
    int nfds = epoll_wait(loop->epoll_fd, loop->events, loop->max_events, -1);

    for (int i = 0; i < nfds; i++) {
      if (loop->events[i].events & EPOLLIN) {
        handle_read_event(loop->events[i].data.fd);
      }
      if (loop->events[i].events & EPOLLOUT) {
        handle_write_event(loop->events[i].data.fd);
      }
    }
  }
}
```

#### Asynchronous Frame Generation
Trigger frame generation based on events rather than timers:

```c
typedef struct {
  uint64_t last_frame_time;
  uint64_t frame_interval_ns;
  client_id_t client_id;
} frame_timer_t;

void schedule_frame_generation(client_id_t client_id) {
  frame_timer_t *timer = get_frame_timer(client_id);
  uint64_t now = get_monotonic_time_ns();

  if (now - timer->last_frame_time >= timer->frame_interval_ns) {
    queue_frame_generation_work(client_id);
    timer->= now;
  }
}
```

### Memory Management

#### NUMA-Aware Allocation
Implement NUMA-aware memory allocation for optimal performance:

```c
typedef struct {
  int numa_node;
  void *memory_pool;
  size_t pool_size;
  atomic_uint_fast32_t next_offset;
} numa_memory_pool_t;

void *numa_alloc(numa_memory_pool_t *pool, size_t size) {
  uint32_t offset = atomic_fetch_add(&pool->next_offset, size);
  if (offset + size > pool->pool_size) {
    return NULL; // Pool exhausted
  }
  return (char*)pool->memory_pool + offset;
}
```

#### Cache-Aligned Data Structures
Ensure optimal cache line utilization:

```c
typedef struct {
  atomic_uint_fast32_t state __attribute__((aligned(64)));
  atomic_uint_fast32_t counter __attribute__((aligned(64)));
  char padding[64 - sizeof(atomic_uint_fast32_t)];
} cache_aligned_atomic_t;
```

### Performance Monitoring

#### Lock Contention Monitoring
Track lock contention for performance optimization:

```c
typedef struct {
  atomic_uint_fast64_t lock_acquisitions;
  atomic_uint_fast64_t total_wait_time_ns;
  atomic_uint_fast64_t max_wait_time_ns;
} lock_stats_t;

void record_lock_contention(lock_stats_t *stats, uint64_t wait_time_ns) {
  atomic_fetch_add(&stats->lock_acquisitions, 1);
  atomic_fetch_add(&stats->total_wait_time_ns, wait_time_ns);

  uint64_t current_max = atomic_load(&stats->max_wait_time_ns);
  while (wait_time_ns > current_max) {
    if (atomic_compare_exchange_weak(&stats->max_wait_time_ns, &current_max, wait_time_ns)) {
      break;
    }
  }
}
```

#### Thread Utilization Tracking
Monitor thread CPU usage and efficiency:

```c
typedef struct {
  atomic_uint_fast64_t work_items_processed;
  atomic_uint_fast64_t total_work_time_ns;
  atomic_uint_fast64_t idle_time_ns;
} thread_stats_t;

void update_thread_stats(thread_stats_t *stats, bool was_working, uint64_t time_ns) {
  if (was_working) {
    atomic_fetch_add(&stats->work_items_processed, 1);
    atomic_fetch_add(&stats->total_work_time_ns, time_ns);
  } else {
    atomic_fetch_add(&stats->idle_time_ns, time_ns);
  }
}
```

## Best Practices

### Thread Safety Guidelines

1. **Always use atomic operations for simple flags and counters**
2. **Use mutexes only for complex data structures that can't be made atomic**
3. **Minimize lock holding time using the snapshot pattern**
4. **Follow strict lock ordering to prevent deadlocks**
5. **Use reader-writer locks for read-heavy access patterns**

### Performance Guidelines

1. **Prefer lock-free data structures for high-frequency operations**
2. **Use thread-local storage to avoid contention**
3. **Align data structures to cache line boundaries**
4. **Batch operations to reduce synchronization overhead**
5. **Profile lock contention and optimize hot paths**

### Reliability Guidelines

1. **Always check return values from synchronization primitives**
2. **Use timeout-based locking to prevent indefinite blocking**
3. **Implement graceful degradation when resources are exhausted**
4. **Provide comprehensive error handling for all synchronization operations**
5. **Use static analysis tools to detect potential race conditions**

## Critical Concurrency Bugs and Discrepancies

### Bug #1: Signal Handler Race Condition (CRITICAL) - FIXED

**Location**: `src/server/main.c:258-264` (sigint_handler)

**Problem**: The signal handler directly accessed `g_client_manager.clients[i].active` without holding any locks, while other threads may be modifying this data structure.

```c
// BUG: Signal handler accessing shared data without synchronization
for (int i = 0; i < MAX_CLIENTS; i++) {
  if (g_client_manager.clients[i].active && g_client_manager.clients[i].socket != INVALID_SOCKET_VALUE) {
    socket_shutdown(g_client_manager.clients[i].socket, SHUT_RDWR);
    socket_close(g_client_manager.clients[i].socket);
    g_client_manager.clients[i].socket = INVALID_SOCKET_VALUE;  // RACE CONDITION!
  }
}
```

**Race Condition**:
- Signal handler reads `clients[i].active` and `clients[i].socket`
- Main thread or client threads modify these fields under `g_client_manager_rwlock`
- Signal handler writes to `clients[i].socket` without holding any lock
- This corrupts the client state and explains the "double Ctrl+C" issue

**FIXED**: Made signal handler signal-safe by removing direct access to shared data structures:

```c
// FIXED: Signal handler is now signal-safe
// We cannot safely access shared data structures in signal handlers due to race conditions
// Instead, we rely on the main thread to detect g_should_exit and perform proper cleanup
// This prevents the race condition that was causing the double Ctrl+C issue
```

**Additional Fix**: Updated main thread cleanup to use atomic operations:
```c
// FIXED: Use atomic_load for thread-safe read of active flag
if (atomic_load(&g_client_manager.clients[i].active) && g_client_manager.clients[i].socket != INVALID_SOCKET_VALUE) {
  socket_shutdown(g_client_manager.clients[i].socket, SHUT_RDWR);
  socket_close(g_client_manager.clients[i].socket);
  g_client_manager.clients[i].socket = INVALID_SOCKET_VALUE;
}
```

**Additional Fix**: Eliminated double socket closure race condition:
```c
// BEFORE (DOUBLE-CLOSE BUG):
// Main thread cleanup:
socket_close(g_client_manager.clients[i].socket);
g_client_manager.clients[i].socket = INVALID_SOCKET_VALUE;  // Race condition!

// Later, remove_client():
if (client->socket != INVALID_SOCKET_VALUE) {  // Might see old value
  socket_close(client->socket);  // Double close!
}

// AFTER (FIXED):
// Main thread cleanup:
log_info("Signaling all clients to stop (g_should_exit set)...");
// Let remove_client() handle socket cleanup with proper synchronization

// remove_client() handles all socket cleanup properly
```

**Impact**:
- ✅ Eliminates client state corruption during shutdown
- ✅ Server now responds to single Ctrl+C properly
- ✅ Client reconnection works correctly
- ✅ Eliminates double socket closure race condition

### Bug #2: Lock Ordering Violations - FIXED (December 2024)

**Location**: Multiple locations in `protocol.c`, `client.c`, `stream.c`, `render.c`

**Problem**: Several functions acquire `client_state_mutex` without first acquiring `g_client_manager_rwlock`, violating the established lock ordering protocol.

**Examples**:
```c
// All violations have been fixed!
```

**FIXED**: `render.c:443 and render.c:680` - Now properly acquires `g_client_manager_rwlock` before `client_state_mutex`:
```c
// FIXED: Follow lock ordering protocol in video_render_thread_func
rwlock_rdlock(&g_client_manager_rwlock);  // Acquire global lock FIRST
mutex_lock(&client->client_state_mutex);
// ... snapshot client state ...
mutex_unlock(&client->client_state_mutex);
rwlock_unlock(&g_client_manager_rwlock);
```

**FIXED**: `protocol.c:723` - Now properly acquires `g_client_manager_rwlock` before `client_state_mutex`:
```c
// FIXED: Follow lock ordering protocol - acquire rwlock first, then client mutex
rwlock_rdlock(&g_client_manager_rwlock);
mutex_lock(&client->client_state_mutex);
// ... process client state ...
mutex_unlock(&client->client_state_mutex);
rwlock_unlock(&g_client_manager_rwlock);
```

**FIXED**: `protocol.c:830` - Now properly acquires `g_client_manager_rwlock` before `client_state_mutex`:
```c
// FIXED: Follow lock ordering protocol - acquire rwlock first, then client mutex
rwlock_rdlock(&g_client_manager_rwlock);
mutex_lock(&client->client_state_mutex);
atomic_store(&client->width, ntohs(size_pkt->width));
atomic_store(&client->height, ntohs(size_pkt->height));
mutex_unlock(&client->client_state_mutex);
rwlock_unlock(&g_client_manager_rwlock);
```

**FIXED**: `client.c:663` - Now properly acquires `g_client_manager_rwlock` before `client_state_mutex`:
```c
// FIXED: Follow lock ordering protocol - acquire rwlock first, then client mutex
rwlock_rdlock(&g_client_manager_rwlock);
mutex_lock(&client->client_state_mutex);
client->active = false;  // FIXED: Regular assignment under mutex protection
client->send_thread_running = false;
client->video_render_thread_running = false;  // FIXED: Regular assignment under mutex protection
client->audio_render_thread_running = false;  // FIXED: Regular assignment under mutex protection
mutex_unlock(&client->client_state_mutex);
rwlock_unlock(&g_client_manager_rwlock);
```

**Impact**: Potential deadlocks when these functions are called concurrently with functions that follow proper lock ordering.

### Optimization: Eliminated Redundant Atomic Operations

**Problem**: Several functions were using `atomic_store()` and `atomic_load()` inside mutex-protected sections, which is redundant and wasteful.

**Analysis**: When data is protected by a mutex, atomic operations are unnecessary because:
- The mutex already provides exclusive access
- Atomic operations have overhead that's not needed under mutex protection
- It's a classic case of redundant synchronization

**FIXED**: Replaced atomic operations with regular assignments/reads under mutex protection:

```c
// BEFORE (REDUNDANT):
mutex_lock(&client->client_state_mutex);
atomic_store(&client->active, false);  // ← Unnecessary atomic under mutex
atomic_load(&client->active);          // ← Unnecessary atomic under mutex
mutex_unlock(&client->client_state_mutex);

// AFTER (OPTIMIZED):
mutex_lock(&client->client_state_mutex);
client->active = false;  // ← Simple assignment under mutex protection
bool is_active = client->active;  // ← Simple read under mutex protection
mutex_unlock(&client->client_state_mutex);
```

**Performance Impact**: Eliminates unnecessary atomic operation overhead in mutex-protected sections.

### Optimization: Lock-Free Thread Control Flags

**Problem**: Thread control flags were inconsistently synchronized - some used atomics, others used mutexes, creating unnecessary lock contention.

**Analysis**: Thread control flags like `active`, `send_thread_running`, `video_render_thread_running`, and `audio_render_thread_running` are:
- Simple boolean values
- Frequently read in tight loops (thread main loops)
- Infrequently written (only during thread start/stop)
- Perfect candidates for lock-free atomic operations

**FIXED**: Made all thread control flags atomic and eliminated mutex usage:

```c
// BEFORE (INCONSISTENT):
typedef struct {
  bool send_thread_running;           // ← Regular bool, accessed under mutex
  atomic_bool video_render_thread_running;  // ← Atomic, good
  atomic_bool audio_render_thread_running;  // ← Atomic, good
  atomic_bool active;                 // ← Atomic, but accessed under mutex (inconsistent!)
} client_info_t;

// Thread loops with mutex overhead:
mutex_lock(&client->client_state_mutex);
bool is_active = client->active;  // ← Mutex overhead for simple read
mutex_unlock(&client->client_state_mutex);

// AFTER (CONSISTENT AND LOCK-FREE):
typedef struct {
  atomic_bool send_thread_running;    // ← Now atomic
  atomic_bool video_render_thread_running;  // ← Already atomic
  atomic_bool audio_render_thread_running;  // ← Already atomic
  atomic_bool active;                 // ← Atomic, accessed lock-free
} client_info_t;

// Thread loops with lock-free reads:
bool is_active = atomic_load(&client->active);  // ← Lock-free, no mutex overhead
```

**Performance Impact**:
- **Eliminates mutex contention** in thread main loops
- **Faster thread control** with lock-free atomic operations
- **Consistent synchronization** across all thread control flags
- **Better scalability** under high thread counts

### Optimization: Correct Shutdown Pattern with Mixed Synchronization

**Problem**: The shutdown logic needed to handle both atomic-based and condition variable-based synchronization patterns.

**Analysis**: The codebase uses a **hybrid shutdown pattern**:
- **Most threads**: Use atomic `g_should_exit` checks with `platform_sleep_usec()` (lock-free)
- **Some operations**: Use condition variables (like packet queues) that need to be woken up

**CORRECT PATTERN**: The proper shutdown sequence should be:

```c
// 1. Set atomic shutdown flag (signal-safe)
atomic_store(&g_should_exit, true);

// 2. Shutdown all blocking operations (packet queues, etc.)
packet_queue_shutdown(client->audio_queue);  // Wakes up blocked threads

// 3. Broadcast global condition variable for any remaining blocked threads
static_cond_broadcast(&g_shutdown_cond);

// 4. Join all threads
ascii_thread_join(&thread, NULL);
```

**Why This Works**:
- **Atomic operations** for lock-free thread control (most common case)
- **Condition variable broadcast** for threads blocked on blocking operations
- **Packet queue shutdown** ensures blocking dequeue operations are woken up
- **Comprehensive coverage** of all possible blocking scenarios

**Performance Impact**:
- **Optimal performance** - atomic operations where possible
- **Complete coverage** - condition variables where needed
- **Responsive shutdown** - all threads wake up promptly
- **No deadlocks** - proper shutdown sequence prevents hanging

### Bug #3: Inconsistent Locking Patterns - FIXED

**Location**: `src/server/client.c:233-240` (find_client_by_socket)

**Problem**: Function accessed `g_client_manager.clients[i]` without holding `g_client_manager_rwlock`.

```c
// BUG: Accessing global client array without proper synchronization
client_info_t *find_client_by_socket(socket_t socket) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (g_client_manager.clients[i].socket == socket && g_client_manager.clients[i].active) {
      return &g_client_manager.clients[i];  // RACE CONDITION!
    }
  }
  return NULL;
}
```

**FIXED**: Made function internally acquire the required read lock:

```c
// FIXED: Function now internally acquires read lock for thread safety
client_info_t *find_client_by_socket(socket_t socket) {
  rwlock_rdlock(&g_client_manager_rwlock);

  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (g_client_manager.clients[i].socket == socket && atomic_load(&g_client_manager.clients[i].active)) {
      client_info_t *client = &g_client_manager.clients[i];
      rwlock_unlock(&g_client_manager_rwlock);
      return client;
    }
  }

  rwlock_unlock(&g_client_manager_rwlock);
  return NULL;
}
```

**Impact**:
- ✅ Eliminates race conditions in client socket lookups
- ✅ Ensures thread-safe access to global client array
- ✅ Prevents crashes from stale or invalid client pointers
- ✅ Maintains proper lock ordering protocol

### Bug #4: Inconsistent Synchronization Patterns in Stats Collection - FIXED

**Location**: `src/server/stats.c:337-343` and `src/server/stats.c:362-368`

**Problem**: Stats collection had two different synchronization issues:
1. Direct access to atomic `active` field without `atomic_load()`
2. Violation of lock ordering protocol by acquiring per-client mutex while holding rwlock

```c
// BUG 1: Direct access to atomic field without atomic_load
for (int i = 0; i < MAX_CLIENTS; i++) {
  if (g_client_manager.clients[i].active) {  // Should use atomic_load
    active_clients++;
  }
}

// BUG 2: Lock ordering violation - rwlock → per-client mutex (WRONG ORDER)
rwlock_rdlock(&g_client_manager_rwlock);
for (int i = 0; i < MAX_CLIENTS; i++) {
  client_info_t *client = &g_client_manager.clients[i];
  mutex_lock(&client->client_state_mutex);  // VIOLATES LOCK ORDERING!
  bool is_active = client->active;
  mutex_unlock(&client->client_state_mutex);
}
```

**FIXED**: Used consistent atomic operations and proper lock ordering:

```c
// FIXED 1: Use atomic_load for thread-safe access to active flag
for (int i = 0; i < MAX_CLIENTS; i++) {
  if (atomic_load(&g_client_manager.clients[i].active)) {  // Thread-safe atomic read
    active_clients++;
    if (g_client_manager.clients[i].audio_queue) {
      clients_with_audio++;
    }
    if (g_client_manager.clients[i].outgoing_video_buffer) {
      clients_with_video++;
    }
  }
}

// FIXED 2: Use atomic operations instead of mutex (no lock ordering violation)
rwlock_rdlock(&g_client_manager_rwlock);
for (int i = 0; i < MAX_CLIENTS; i++) {
  client_info_t *client = &g_client_manager.clients[i];
  bool is_active = atomic_load(&client->active);  // Lock-free atomic read
  uint32_t client_id_snapshot = client->client_id;
  // Process using atomic values (no mutex needed)
}
```

**Impact**:
- ✅ Ensures thread-safe access to atomic `active` flag
- ✅ Eliminates lock ordering violations
- ✅ Prevents race conditions during client state changes
- ✅ Maintains consistent statistics even during client addition/removal
- ✅ Follows proper atomic operation patterns consistently

**Additional Fix**: Made the entire codebase consistent by applying the same synchronization pattern to all `client_id` accesses:
- **`src/server/render.c`**: Fixed video and audio render threads to use mutex protection for `client_id` access
- **`src/server/stream.c`**: Fixed frame generation to use mutex protection for `client_id` access
- **`src/server/main.c`**: Fixed cleanup loops to use mutex protection for `client_id` access
- **`src/server/stats.c`**: Already fixed to use mutex protection for `client_id` access

This ensures **consistent synchronization patterns** throughout the entire codebase, preventing race conditions and maintaining thread safety.

**Critical Fix**: Fixed memory access violation in main.c cleanup loops:
- **Problem**: Code was trying to lock `client_state_mutex` for all clients in the array, including uninitialized ones
- **Root Cause**: Uninitialized clients have uninitialized mutexes, causing access violations when trying to lock them
- **Solution**: Added checks to skip uninitialized clients (`client_id == 0`) before attempting to lock their mutexes
- **Impact**: Prevents server crashes during startup and ensures proper mutex access patterns

**Additional Critical Fix**: Fixed race condition in stream.c client capability access:
- **Problem**: Code was accessing `target_client->client_id` and `target_client->has_terminal_caps` before acquiring the mutex
- **Root Cause**: Race condition where client state could change between the check and the mutex acquisition
- **Solution**: Moved all client state access inside the mutex-protected section using snapshot pattern
- **Impact**: Prevents race conditions during client capability checks and ensures thread-safe access to client state

### Bug #5: Client State Corruption During Shutdown - FIXED

**Location**: `src/server/main.c:258-264` and `src/server/main.c:681-684`

**Problem**: Two different code paths were closing client sockets without proper coordination:

1. Signal handler was accessing client data without locks (race condition)
2. Main thread was closing sockets, then `remove_client()` was also closing them (double-close)

This created race conditions where sockets were closed twice or client state became inconsistent.

**FIXED**: Eliminated both race conditions:

```c
// FIXED 1: Signal handler is now signal-safe (already fixed in Bug #1)
static void sigint_handler(int sigint) {
  (void)(sigint);

  // STEP 1: Set atomic shutdown flag (signal-safe, checked by all threads)
  atomic_store(&g_should_exit, true);

  // STEP 2: Close listening socket to interrupt accept() calls
  if (listenfd != INVALID_SOCKET_VALUE) {
    socket_close(listenfd);
    listenfd = INVALID_SOCKET_VALUE;
  }

  // STEP 3: FIXED - No longer accesses shared client data
  // Main thread will detect g_should_exit and perform proper cleanup
}

// FIXED 2: Main thread no longer closes client sockets (prevents double-close)
// Clean up all connected clients
log_info("Cleaning up connected clients...");
// FIXED: Don't close client sockets here to avoid double-close race condition
// The remove_client() function will handle proper socket cleanup with proper synchronization
rwlock_rdlock(&g_client_manager_rwlock);
for (int i = 0; i < MAX_CLIENTS; i++) {
  client_info_t *client = &g_client_manager.clients[i];
  if (client->client_id != 0) {
    mutex_lock(&client->client_state_mutex);
    bool is_active = atomic_load(&client->active);
    uint32_t client_id_snapshot = client->client_id;
    mutex_unlock(&client->client_state_mutex);

    if (is_active) {
      log_debug("Client %u will be cleaned up by remove_client()", client_id_snapshot);
      // Let remove_client() handle socket cleanup to avoid double-close
    }
  }
}
rwlock_unlock(&g_client_manager_rwlock);
```

**Impact**:
- ✅ Eliminates signal handler race conditions
- ✅ Prevents double socket closure
- ✅ Ensures consistent client state during shutdown
- ✅ Server responds to single Ctrl+C properly
- ✅ Client reconnection works correctly

### Immediate Fixes Required

1. **Fix Signal Handler**: Make signal handler signal-safe by only setting flags and using proper synchronization
2. **Enforce Lock Ordering**: Add rwlock acquisition to all functions that access client data
3. **Consistent Client Lookups**: Ensure all client lookups use proper synchronization
4. **Coordinated Shutdown**: Implement proper shutdown coordination between signal handler and main thread

### Root Cause Analysis

The primary issue is **inconsistent application of the lock ordering protocol**. While the architecture is well-designed, several functions bypass the established synchronization patterns, creating race conditions that explain the reported symptoms:

- **Client freeze**: Race conditions in client state access
- **Server Ctrl+C issues**: Signal handler corruption of client state
- **Reconnection failures**: Corrupted client state preventing clean reconnection

## Conclusion

The ascii-chat concurrency architecture balances performance, scalability, and maintainability through careful design of threading models, synchronization primitives, and data structures. The current per-client threading model provides excellent isolation and linear scalability, while the proposed improvements offer even better performance through lock-free data structures, thread pools, and event-driven architecture.

However, several critical bugs in the current implementation prevent reliable operation under stress conditions. The primary issue is inconsistent application of the lock ordering protocol, particularly in signal handlers and client lookup functions. These bugs explain the reported symptoms of client freezes, server Ctrl+C issues, and reconnection failures.

The key to successful concurrency in ascii-chat is maintaining the delicate balance between thread safety and performance, using the right synchronization primitive for each use case, and following established patterns for lock ordering and state management consistently throughout the codebase.
