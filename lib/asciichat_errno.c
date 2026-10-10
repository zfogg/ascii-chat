/**
 * @file asciichat_errno.c
 * @brief Bounded, synchronized error stacks with allocation-free recording.
 */
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/debug/backtrace.h>
#include <ascii-chat/platform/backtrace.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/platform/errno.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/path.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#define ERRNO_BUCKETS 257
#define NO_FRAME (-1)
#define RESERVED_FRAMES (2 * ASCIICHAT_ERRNO_MAX_THREADS)
_Static_assert(ASCIICHAT_ERRNO_MAX_FRAMES > RESERVED_FRAMES, "Reserve root/latest slots for every thread");

typedef struct {
  asciichat_error_context_t context;
  int next;
  bool used;
} error_frame_t;

typedef struct error_thread {
  uint64_t id;
  uint64_t generation;
  int top;
  size_t depth;
  uint64_t cause_after_id;
  struct error_thread *next;
} error_thread_t;

// The error path must work before allocators, logging, and instrumented locks.
// A bounded pool avoids recursive allocation and is reclaimed/zeroed on pop.
// The guard uses the platform atomic implementation without diagnostic hooks.
// No allocation, logging, callback, or symbolization occurs while it is held.
static atomic_t g_guard = {0};
static error_thread_t *g_buckets[ERRNO_BUCKETS];
static error_thread_t g_threads[ASCIICHAT_ERRNO_MAX_THREADS];
static error_frame_t g_frames[ASCIICHAT_ERRNO_MAX_FRAMES];
static asciichat_errno_history_t g_history[ASCIICHAT_ERRNO_HISTORY_SIZE];
static size_t g_history_count;
static size_t g_history_next;
static asciichat_error_stats_t g_stats;
static uint64_t g_next_id;
static uint64_t g_generation;
static int g_free_head;
static bool g_initialized;
static bool g_shutdown;
static asciichat_error_t g_exit_code;
#ifndef EMSCRIPTEN_BUILD
static tls_key_t g_tls_key;
static bool g_tls_ready;
#endif
static PLATFORM_THREAD_LOCAL bool g_recording;
static PLATFORM_THREAD_LOCAL bool g_suppress_logging;

static void registry_lock(void) {
  int expected = 0;
  while (!atomic_cas_int_impl(&g_guard, &expected, 1))
    expected = 0;
}
static void registry_unlock(void) {
  atomic_store_int_impl(&g_guard, 0);
}

static size_t bucket_for(uint64_t id) {
  return (size_t)((id ^ (id >> 32)) % ERRNO_BUCKETS);
}

static error_thread_t *find_thread(uint64_t id) {
  for (error_thread_t *thread = g_buckets[bucket_for(id)]; thread; thread = thread->next)
    if (thread->id == id)
      return thread;
  return NULL;
}

static void initialize_locked(void) {
  if (g_initialized)
    return;
  for (int i = RESERVED_FRAMES; i < ASCIICHAT_ERRNO_MAX_FRAMES; ++i)
    g_frames[i].next = i + 1;
  g_frames[ASCIICHAT_ERRNO_MAX_FRAMES - 1].next = NO_FRAME;
  g_free_head = RESERVED_FRAMES;
  g_initialized = true;
}

static void remember_locked(const asciichat_error_context_t *context, asciichat_errno_outcome_t outcome, uint64_t now) {
  asciichat_errno_history_t *event = &g_history[g_history_next];
  event->context = *context;
  event->outcome = outcome;
  event->resolved_ns = now;
  g_history_next = (g_history_next + 1) % ASCIICHAT_ERRNO_HISTORY_SIZE;
  if (g_history_count < ASCIICHAT_ERRNO_HISTORY_SIZE)
    ++g_history_count;
  else
    ++g_stats.overwritten_history;
}

static void release_frame_locked(int index, asciichat_errno_outcome_t outcome, uint64_t now) {
  remember_locked(&g_frames[index].context, outcome, now);
  memset(&g_frames[index], 0, sizeof(g_frames[index]));
  if (index >= RESERVED_FRAMES) {
    g_frames[index].next = g_free_head;
    g_free_head = index;
  }
  --g_stats.pending_errors;
}

static void pop_locked(error_thread_t *thread, asciichat_errno_outcome_t outcome, uint64_t now) {
  int index = thread->top;
  if (index == NO_FRAME)
    return;
  thread->top = g_frames[index].next;
  --thread->depth;
  release_frame_locked(index, outcome, now);
}

static void unregister_locked(error_thread_t *thread, uint64_t now) {
  while (thread->depth)
    pop_locked(thread, ASCIICHAT_ERRNO_THREAD_EXIT, now);
  error_thread_t **link = &g_buckets[bucket_for(thread->id)];
  while (*link && *link != thread)
    link = &(*link)->next;
  if (*link == thread)
    *link = thread->next;
  memset(thread, 0, sizeof(*thread));
  --g_stats.registered_threads;
}

#ifndef EMSCRIPTEN_BUILD
static void error_thread_destructor(void *value) {
  registry_lock();
  uint64_t now = time_get_ns();
  error_thread_t *thread = value;
  if (thread && thread->generation && !g_shutdown)
    unregister_locked(thread, now);
  registry_unlock();
}
#endif

static error_thread_t *register_current_locked(uint64_t id) {
  error_thread_t *thread = find_thread(id);
  if (thread)
    return thread;
  if (g_shutdown)
    return NULL;
  initialize_locked();
#ifndef EMSCRIPTEN_BUILD
  // Platform TLS creation/setters do not log or allocate through SAFE_MALLOC.
  if (!g_tls_ready) {
    if (ascii_tls_key_create(&g_tls_key, error_thread_destructor) != 0)
      return NULL;
    g_tls_ready = true;
  }
#endif
  for (size_t i = 0; i < ASCIICHAT_ERRNO_MAX_THREADS; ++i) {
    if (g_threads[i].generation)
      continue;
    thread = &g_threads[i];
#ifndef EMSCRIPTEN_BUILD
    if (ascii_tls_set(g_tls_key, thread) != 0)
      return NULL;
#endif
    thread->id = id;
    thread->generation = ++g_generation;
    thread->top = NO_FRAME;
    size_t bucket = bucket_for(id);
    thread->next = g_buckets[bucket];
    g_buckets[bucket] = thread;
    ++g_stats.registered_threads;
    return thread;
  }
  return NULL;
}

static void record_stats_locked(asciichat_error_t code, uint64_t timestamp) {
  if (code == ASCIICHAT_OK)
    return;
  if ((unsigned)code < 256)
    ++g_stats.error_counts[code];
  ++g_stats.total_errors;
  g_stats.last_error_time = timestamp;
  g_stats.last_error_code = code;
}

static void push_context(asciichat_error_context_t *context) {
  registry_lock();
  if (g_shutdown) {
    registry_unlock();
    return;
  }
  record_stats_locked(context->code, context->timestamp);
  context->error_id = ++g_next_id;
  error_thread_t *thread = register_current_locked(context->thread_id);
  if (!thread) {
    ++g_stats.dropped_errors;
    remember_locked(context, ASCIICHAT_ERRNO_OVERFLOW, context->created_ns);
    registry_unlock();
    return;
  }
  context->generation = thread->generation;
  if (thread->top != NO_FRAME && g_frames[thread->top].context.error_id > thread->cause_after_id)
    context->cause_id = g_frames[thread->top].context.error_id;

  int reserved = (int)(thread - g_threads) * 2;
  bool has_reserved = !g_frames[reserved].used || !g_frames[reserved + 1].used;
  // Preserve the oldest/root frame and newest context by evicting the second
  // oldest frame when this thread reaches its depth limit or the pool fills.
  if ((thread->depth == ASCIICHAT_ERRNO_MAX_DEPTH || (!has_reserved && g_free_head == NO_FRAME)) && thread->depth > 1) {
    int previous = NO_FRAME;
    int victim = thread->top;
    while (g_frames[g_frames[victim].next].next != NO_FRAME) {
      previous = victim;
      victim = g_frames[victim].next;
    }
    if (previous == NO_FRAME)
      thread->top = g_frames[victim].next;
    else
      g_frames[previous].next = g_frames[victim].next;
    --thread->depth;
    ++g_stats.dropped_errors;
    release_frame_locked(victim, ASCIICHAT_ERRNO_OVERFLOW, context->created_ns);
  }
  int index;
  if (!g_frames[reserved].used)
    index = reserved;
  else if (!g_frames[reserved + 1].used)
    index = reserved + 1;
  else {
    index = g_free_head;
    g_free_head = g_frames[index].next;
  }
  g_frames[index].used = true;
  g_frames[index].context = *context;
  g_frames[index].next = thread->top;
  thread->top = index;
  ++thread->depth;
  ++g_stats.pending_errors;
  registry_unlock();
}

static asciichat_error_t set_formatted(asciichat_error_t code, const char *file, int line, const char *function,
                                       int sys_errno, bool system, int wsa, bool windows_socket, const char *format,
                                       va_list args) {
  if (code == ASCIICHAT_OK || g_recording)
    return code;
  registry_lock();
  bool shutdown = g_shutdown;
  registry_unlock();
  if (shutdown)
    return code;
  g_recording = true;
  asciichat_error_context_t context = {0};
  context.code = code;
  context.file = file;
  context.line = line;
  context.function = function;
  context.system_errno = sys_errno;
  context.has_system_error = system;
  context.wsa_error = wsa;
  context.has_wsa_error = windows_socket;
  context.thread_id = asciichat_thread_current_id();
  context.timestamp = time_ns_to_us(time_get_realtime_ns());
  context.created_ns = time_get_ns();
  int length = vsnprintf(context.context_message, sizeof(context.context_message), format ? format : "", args);
  context.message_truncated = length < 0 || (size_t)length >= sizeof(context.context_message);
#if !defined(NDEBUG) || defined(ENABLE_ERRNO_BACKTRACES)
  backtrace_capture(&context.backtrace);
#endif
  push_context(&context);
  if (!g_suppress_logging) {
    log_debug("%s (code: %d, meaning: %s)%s", context.context_message, code, asciichat_error_string(code),
              context.message_truncated ? " [message truncated]" : "");
  }
  g_recording = false;
  return code;
}

asciichat_error_t asciichat_set_errno_with_message(asciichat_error_t code, const char *file, int line,
                                                   const char *function, const char *format, ...) {
  va_list args;
  va_start(args, format);
  asciichat_error_t result = set_formatted(code, file, line, function, 0, false, 0, false, format, args);
  va_end(args);
  return result;
}

asciichat_error_t asciichat_set_errno(asciichat_error_t code, const char *file, int line, const char *function,
                                      const char *message) {
  return asciichat_set_errno_with_message(code, file, line, function, "%s", message ? message : "");
}

asciichat_error_t asciichat_set_errno_with_system_error_and_message(asciichat_error_t code, const char *file, int line,
                                                                    const char *function, int sys_errno,
                                                                    const char *format, ...) {
  va_list args;
  va_start(args, format);
  asciichat_error_t result = set_formatted(code, file, line, function, sys_errno, true, 0, false, format, args);
  va_end(args);
  return result;
}

asciichat_error_t asciichat_set_errno_with_system_error(asciichat_error_t code, const char *file, int line,
                                                        const char *function, int sys_errno) {
  return asciichat_set_errno_with_system_error_and_message(code, file, line, function, sys_errno,
                                                           "System operation failed (%d)", sys_errno);
}

// WSA metadata must be published together with the frame, never patched later.
static asciichat_error_t set_wsa(asciichat_error_t code, const char *file, int line, const char *function, int wsa,
                                 const char *format, ...) {
  va_list args;
  va_start(args, format);
  asciichat_error_t result = set_formatted(code, file, line, function, 0, false, wsa, true, format, args);
  va_end(args);
  return result;
}
asciichat_error_t asciichat_set_errno_with_wsa_error(asciichat_error_t code, const char *file, int line,
                                                     const char *function, int wsa_error) {
  return set_wsa(code, file, line, function, wsa_error, "Socket operation failed (%d)", wsa_error);
}

bool asciichat_has_thread_errno(uint64_t id, asciichat_error_context_t *context) {
  registry_lock();
  error_thread_t *thread = find_thread(id);
  bool found = thread && thread->depth;
  if (context) {
    if (found)
      *context = g_frames[thread->top].context;
    else
      memset(context, 0, sizeof(*context));
  }
  registry_unlock();
  return found;
}
bool asciichat_has_errno(asciichat_error_context_t *context) {
  return asciichat_has_thread_errno(asciichat_thread_current_id(), context);
}
asciichat_error_t asciichat_get_thread_error(uint64_t id) {
  registry_lock();
  error_thread_t *thread = find_thread(id);
  asciichat_error_t code = thread && thread->depth ? g_frames[thread->top].context.code : ASCIICHAT_OK;
  registry_unlock();
  return code;
}
asciichat_error_t asciichat_get_errno(void) {
  return asciichat_get_thread_error(asciichat_thread_current_id());
}
bool asciichat_has_wsa_error(void) {
  asciichat_error_context_t context;
  return HAS_ERRNO(&context) && context.has_wsa_error;
}

static bool has_code_locked(error_thread_t *thread, uint64_t after_id, asciichat_error_t code) {
  if (!thread || code == ASCIICHAT_OK)
    return false;
  for (int index = thread->top; index != NO_FRAME; index = g_frames[index].next) {
    const asciichat_error_context_t *context = &g_frames[index].context;
    if (context->error_id <= after_id)
      break;
    if (context->code == code)
      return true;
  }
  return false;
}
bool asciichat_has_errno_code_tid(uint64_t id, asciichat_error_t code) {
  registry_lock();
  bool found = has_code_locked(find_thread(id), 0, code);
  registry_unlock();
  return found;
}
bool asciichat_has_errno_code(asciichat_error_t code) {
  return asciichat_has_errno_code_tid(asciichat_thread_current_id(), code);
}
bool asciichat_has_errno_code_since(asciichat_errno_scope_t scope, asciichat_error_t code) {
  registry_lock();
  error_thread_t *thread = find_thread(scope.thread_id);
  bool found = thread && thread->generation == scope.generation && has_code_locked(thread, scope.after_id, code);
  registry_unlock();
  return found;
}

static void clear_thread(uint64_t id, bool all) {
  registry_lock();
  uint64_t now = time_get_ns();
  error_thread_t *thread = find_thread(id);
  if (thread) {
    do {
      pop_locked(thread, ASCIICHAT_ERRNO_HANDLED, now);
    } while (all && thread->depth);
  }
  registry_unlock();
}
void asciichat_clear_thread_error(uint64_t id) {
  clear_thread(id, false);
}
void asciichat_clear_thread_errors(uint64_t id) {
  clear_thread(id, true);
}
void asciichat_clear_errno(void) {
  clear_thread(asciichat_thread_current_id(), false);
  platform_clear_error_state();
}
void asciichat_clear_errno_all(void) {
  clear_thread(asciichat_thread_current_id(), true);
  platform_clear_error_state();
}
bool asciichat_clear_errno_if_top(uint64_t id, uint64_t generation, uint64_t error_id) {
  registry_lock();
  uint64_t now = time_get_ns();
  error_thread_t *thread = find_thread(id);
  bool matches =
      thread && thread->generation == generation && thread->depth && g_frames[thread->top].context.error_id == error_id;
  if (matches)
    pop_locked(thread, ASCIICHAT_ERRNO_HANDLED, now);
  registry_unlock();
  return matches;
}
static asciichat_errno_scope_t checkpoint(bool new_cause) {
  asciichat_errno_scope_t scope = {.thread_id = asciichat_thread_current_id()};
  registry_lock();
  error_thread_t *thread = register_current_locked(scope.thread_id);
  scope.generation = thread ? thread->generation : 0;
  scope.after_id = g_next_id;
  if (thread) {
    scope.previous_after_id = thread->cause_after_id;
    if (new_cause)
      thread->cause_after_id = scope.after_id;
  }
  registry_unlock();
  return scope;
}
asciichat_errno_scope_t asciichat_errno_checkpoint(void) {
  return checkpoint(false);
}
asciichat_errno_scope_t asciichat_errno_scope_begin(void) {
  return checkpoint(true);
}
void asciichat_errno_scope_end(asciichat_errno_scope_t scope, asciichat_errno_outcome_t outcome) {
  uint64_t now = time_get_ns();
  // Recovery runs on the offending thread. Cross-thread clearing has separate APIs.
  if (scope.thread_id != asciichat_thread_current_id())
    return;
  registry_lock();
  error_thread_t *thread = find_thread(scope.thread_id);
  if (thread && thread->generation == scope.generation) {
    while (thread->depth && g_frames[thread->top].context.error_id > scope.after_id)
      pop_locked(thread, outcome, now);
    thread->cause_after_id = scope.previous_after_id;
  }
  registry_unlock();
}
size_t asciichat_errno_snapshot(uint64_t id, asciichat_error_context_t *out, size_t capacity) {
  registry_lock();
  error_thread_t *thread = find_thread(id);
  size_t depth = thread ? thread->depth : 0;
  size_t count = 0;
  if (thread && out)
    for (int index = thread->top; index != NO_FRAME && count < capacity; index = g_frames[index].next)
      out[count++] = g_frames[index].context;
  registry_unlock();
  return depth;
}
size_t asciichat_errno_history_snapshot(asciichat_errno_history_t *out, size_t capacity) {
  registry_lock();
  size_t count = g_history_count;
  if (out)
    for (size_t i = 0; i < count && i < capacity; ++i)
      out[i] = g_history[(g_history_next + ASCIICHAT_ERRNO_HISTORY_SIZE - 1 - i) % ASCIICHAT_ERRNO_HISTORY_SIZE];
  registry_unlock();
  return count;
}

void asciichat_errno_suppress(bool suppress) {
  g_suppress_logging = suppress;
}
void asciichat_errno_destroy(void) {
  uint64_t now = time_get_ns();
  uint64_t id = asciichat_thread_current_id();
  registry_lock();
#ifndef EMSCRIPTEN_BUILD
  if (g_tls_ready)
    ascii_tls_set(g_tls_key, NULL);
#endif
  error_thread_t *thread = find_thread(id);
  if (thread)
    unregister_locked(thread, now);
  registry_unlock();
}
void asciichat_errno_shutdown(void) {
  asciichat_errno_destroy();
  registry_lock();
  g_shutdown = true;
  memset(g_threads, 0, sizeof(g_threads));
  memset(g_buckets, 0, sizeof(g_buckets));
  memset(g_frames, 0, sizeof(g_frames));
  memset(g_history, 0, sizeof(g_history));
  g_history_count = g_history_next = 0;
  g_stats.pending_errors = g_stats.registered_threads = 0;
#ifndef EMSCRIPTEN_BUILD
  bool delete_key = g_tls_ready;
  tls_key_t key = g_tls_key;
  g_tls_ready = false;
#endif
  registry_unlock();
#ifndef EMSCRIPTEN_BUILD
  if (delete_key)
    ascii_tls_key_delete(key);
#endif
}

void asciichat_error_stats_init(void) {
  registry_lock();
  initialize_locked();
  registry_unlock();
}
void asciichat_error_stats_record(asciichat_error_t code) {
  uint64_t timestamp = time_ns_to_us(time_get_realtime_ns());
  registry_lock();
  record_stats_locked(code, timestamp);
  registry_unlock();
}
asciichat_error_stats_t asciichat_error_stats_get(void) {
  registry_lock();
  asciichat_error_stats_t stats = g_stats;
  registry_unlock();
  return stats;
}
void asciichat_error_stats_reset(void) {
  registry_lock();
  size_t pending = g_stats.pending_errors, threads = g_stats.registered_threads;
  memset(&g_stats, 0, sizeof(g_stats));
  g_stats.pending_errors = pending;
  g_stats.registered_threads = threads;
  registry_unlock();
}
void asciichat_error_stats_print(void) {
  asciichat_error_stats_t stats = asciichat_error_stats_get();
  log_plain("Errors: total=%" PRIu64 " pending=%zu threads=%zu dropped=%" PRIu64 " history_overwritten=%" PRIu64,
            stats.total_errors, stats.pending_errors, stats.registered_threads, stats.dropped_errors,
            stats.overwritten_history);
  for (size_t i = 1; i < 256; ++i)
    if (stats.error_counts[i])
      log_plain("  %s: %" PRIu64, asciichat_error_string((asciichat_error_t)i), stats.error_counts[i]);
}

void asciichat_print_error_context(const asciichat_error_context_t *context) {
  if (!context || context->code == ASCIICHAT_OK)
    return;
  bool previous = g_recording;
  g_recording = true;
  uint64_t now = time_get_ns();
  log_plain("  Error #%" PRIu64 " thread=%" PRIu64 "/%" PRIu64 " code=%d cause=#%" PRIu64 " age=%.3fs",
            context->error_id, context->thread_id, context->generation, context->code, context->cause_id,
            now >= context->created_ns ? (double)(now - context->created_ns) / 1e9 : 0.0);
  log_plain("  %s%s", context->context_message, context->message_truncated ? " [truncated]" : "");
  if (context->file)
    log_plain("  %s:%d in %s()", extract_project_relative_path(context->file), context->line,
              context->function ? context->function : "?");
  if (context->has_system_error)
    log_plain("  System error: %d", context->system_errno);
  if (context->has_wsa_error)
    log_plain("  WSA error: %d", context->wsa_error);
  if (context->backtrace.count > 0) {
    backtrace_t trace = context->backtrace;
    trace.symbols = NULL;
    trace.tried_symbolize = false;
    backtrace_symbolize(&trace);
    if (trace.symbols)
      backtrace_print("Error backtrace", &trace, 0, 0, NULL);
    backtrace_t_free(&trace);
  }
  g_recording = previous;
}

void asciichat_errno_print_stacks(void) {
  // Copy one bounded thread stack at a time. A generation check prevents a
  // reused thread slot from being attributed to the thread sampled earlier.
  uint64_t ids[ASCIICHAT_ERRNO_MAX_THREADS], generations[ASCIICHAT_ERRNO_MAX_THREADS];
  size_t count = 0;
  registry_lock();
  for (size_t i = 0; i < ASCIICHAT_ERRNO_MAX_THREADS; ++i)
    if (g_threads[i].depth) {
      ids[count] = g_threads[i].id;
      generations[count++] = g_threads[i].generation;
    }
  registry_unlock();
  log_plain("Pending error stacks (%zu threads):", count);
  for (size_t i = 0; i < count; ++i) {
    asciichat_error_context_t contexts[ASCIICHAT_ERRNO_MAX_DEPTH];
    size_t depth = asciichat_errno_snapshot(ids[i], contexts, ASCIICHAT_ERRNO_MAX_DEPTH);
    if (!depth || contexts[0].generation != generations[i])
      continue;
    for (size_t j = 0; j < depth; ++j)
      asciichat_print_error_context(&contexts[j]);
  }
  asciichat_error_stats_print();
}
void asciichat_errno_print_history(void) {
  asciichat_errno_history_t events[ASCIICHAT_ERRNO_HISTORY_SIZE];
  size_t count = asciichat_errno_history_snapshot(events, ASCIICHAT_ERRNO_HISTORY_SIZE);
  static const char *outcomes[] = {"handled", "dismissed", "thread-exit", "overflow"};
  for (size_t i = 0; i < count; ++i) {
    const asciichat_errno_history_t *event = &events[i];
    log_plain("Error #%" PRIu64 " thread=%" PRIu64 " code=%d %s after %.3fms: %s", event->context.error_id,
              event->context.thread_id, event->context.code, outcomes[event->outcome],
              (double)(event->resolved_ns - event->context.created_ns) / 1e6, event->context.context_message);
  }
}
void asciichat_errno_print_hash_stats(void) {
  size_t used = 0, max_chain = 0;
  registry_lock();
  for (size_t i = 0; i < ERRNO_BUCKETS; ++i) {
    size_t chain = 0;
    for (error_thread_t *thread = g_buckets[i]; thread; thread = thread->next)
      ++chain;
    used += chain != 0;
    if (chain > max_chain)
      max_chain = chain;
  }
  size_t entries = g_stats.registered_threads;
  registry_unlock();
  log_plain("Error registry: entries=%zu buckets=%d used=%zu max_chain=%zu reserved_bytes=%zu", entries, ERRNO_BUCKETS,
            used, max_chain, sizeof(g_threads) + sizeof(g_buckets) + sizeof(g_frames) + sizeof(g_history));
}

void asciichat_fatal_with_context(asciichat_error_t code, const char *file, int line, const char *function,
                                  const char *format, ...) {
  // This path must also work when an allocator or a diagnostic itself failed.
  bool previous = g_recording;
  g_recording = true;
  char message[ASCIICHAT_ERRNO_MESSAGE_MAX];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format ? format : "", args);
  va_end(args);
  log_error("Fatal: %s (code=%d at %s:%d in %s)", message, code, file ? file : "?", line, function ? function : "?");
  asciichat_errno_print_stacks();
  g_recording = previous;
#ifdef EMSCRIPTEN_BUILD
  SET_ERRNO(code, "%s", message);
#else
  exit(code);
#endif
}

bool asciichat_errno_scope_is_clean(asciichat_errno_scope_t scope) {
  registry_lock();
  error_thread_t *thread = find_thread(scope.thread_id);
  bool clean = !thread || thread->generation != scope.generation || !thread->depth ||
               g_frames[thread->top].context.error_id <= scope.after_id;
  registry_unlock();
  return clean;
}
void asciichat_errno_print_chain(uint64_t id, uint64_t error_id) {
  asciichat_error_context_t frames[ASCIICHAT_ERRNO_MAX_DEPTH];
  size_t count = asciichat_errno_snapshot(id, frames, ASCIICHAT_ERRNO_MAX_DEPTH);
  log_plain("Failure chain (outer context -> root cause):");
  for (size_t i = 0; i < count && error_id; ++i) {
    if (frames[i].error_id == error_id) {
      asciichat_print_error_context(&frames[i]);
      error_id = frames[i].cause_id;
    }
  }
  if (error_id)
    log_plain("  Cause #%" PRIu64 " was resolved or evicted; consult handled-error history", error_id);
}

void asciichat_errno_request_exit(asciichat_error_t code) {
  registry_lock();
  if (g_exit_code == ASCIICHAT_OK)
    g_exit_code = code;
  registry_unlock();
}
asciichat_error_t asciichat_errno_exit_code(void) {
  registry_lock();
  asciichat_error_t code = g_exit_code;
  registry_unlock();
  return code;
}
