#include <ascii-chat/debug/stats.h>
#include <ascii-chat/debug/sync.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/debug/mutex.h>
#include <ascii-chat/debug/backtrace.h>
#include <ascii-chat/debug/memory.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/platform/cond.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/debug/debug_helpers.h>

static atomic_t g_signal = {0};
static atomic_t g_exiting = {0};
static atomic_t g_cleaning = {0};
static asciichat_thread_t g_thread;
static uint64_t g_main_thread;
static mutex_t g_mutex;
static cond_t g_condition;
static bool g_initialized;
static bool g_started;
static uint64_t g_state_deadline;
static uint64_t g_errno_deadline;
static uint64_t g_backtrace_deadline;
static uint64_t g_memory_deadline;
static uint64_t g_memory_interval;

void debug_stats_set_main_thread_id(void) {
  g_main_thread = asciichat_thread_current_id();
}
uint64_t debug_stats_get_main_thread_id(void) {
  return g_main_thread;
}
asciichat_error_t debug_stats_init(void) {
  if (g_initialized)
    return ASCIICHAT_OK;
  if (mutex_init(&g_mutex, "debug_stats") != 0)
    return SET_ERRNO(ERROR_THREAD, "Cannot initialize diagnostics mutex");
  if (cond_init(&g_condition, "debug_stats") != 0) {
    mutex_destroy(&g_mutex);
    return SET_ERRNO(ERROR_THREAD, "Cannot initialize diagnostics condition");
  }
  g_initialized = true;
  atomic_store_bool_impl(&g_cleaning, false);
  return ASCIICHAT_OK;
}
static void debug_stats_print_errno(void) {
  asciichat_errno_print_stacks();
  asciichat_errno_print_history();
  asciichat_errno_print_hash_stats();
}
static void debug_stats_print_sync(void) {
#ifndef NDEBUG
  debug_sync_print_state();
  named_print_hash_stats();
#else
  log_info("SYNC_STATE: synchronization diagnostics are unavailable in Release builds");
#endif
}
void debug_stats_print(void) {
  debug_stats_print_sync();
  debug_stats_print_errno();
}
void debug_stats_print_state_delayed(uint64_t delay) {
  if (!g_initialized)
    return;
  debug_report_schedule(&g_mutex, &g_condition, &g_state_deadline, delay);
}
void debug_stats_print_errno_delayed(uint64_t delay) {
  if (!g_initialized)
    return;
  debug_report_schedule(&g_mutex, &g_condition, &g_errno_deadline, delay);
}
void debug_stats_print_backtrace_delayed(uint64_t delay) {
  if (!g_initialized)
    return;
  debug_report_schedule(&g_mutex, &g_condition, &g_backtrace_deadline, delay);
}
void debug_stats_set_memory_report_interval(uint64_t interval) {
  if (!g_initialized)
    return;
  mutex_lock(&g_mutex);
  g_memory_interval = interval;
  g_memory_deadline = interval ? debug_report_deadline_after(time_get_ns(), interval) : 0;
  cond_signal(&g_condition);
  mutex_unlock(&g_mutex);
}
void debug_stats_trigger_print(void) {
  // Do not signal a condition variable or invoke diagnostic hooks in a signal
  // handler. The worker checks this flag at most 100ms after its next wakeup.
  atomic_store_bool_impl(&g_signal, true);
}
bool debug_stats_is_cleanup_in_progress(void) {
  return atomic_load_bool_impl(&g_cleaning);
}
void debug_stats_poll(void) {
  if (!g_initialized)
    return;
  uint64_t now = time_get_ns();
  bool all = atomic_exchange_bool_impl(&g_signal, false);
  bool state = false;
  mutex_lock(&g_mutex);
  bool backtrace = g_backtrace_deadline && now >= g_backtrace_deadline;
  bool memory = g_memory_deadline && now >= g_memory_deadline;
  if (g_state_deadline && now >= g_state_deadline) {
    state = true;
    g_state_deadline = 0;
  }
  bool errors = g_errno_deadline && now >= g_errno_deadline;
  if (errors)
    g_errno_deadline = 0;
  if (backtrace)
    g_backtrace_deadline = 0;
  if (memory)
    g_memory_deadline = debug_report_deadline_after(now, g_memory_interval);
  mutex_unlock(&g_mutex);
  if (all || state)
    debug_stats_print_sync();
  if (all || errors)
    debug_stats_print_errno();
  if (backtrace) {
    backtrace_t trace = {0};
    backtrace_capture(&trace);
    backtrace_symbolize(&trace);
    if (trace.symbols)
      backtrace_print("Diagnostics worker backtrace", &trace, 0, 0, NULL);
    backtrace_t_free(&trace);
  }
#if defined(DEBUG_MEMORY) && !defined(NDEBUG)
  if (memory)
    debug_memory_report();
#else
  (void)memory;
#endif
}
#ifndef EMSCRIPTEN_BUILD
static void *debug_stats_worker(void *unused) {
  (void)unused;
  while (!atomic_load_bool_impl(&g_exiting)) {
    debug_stats_poll();
#ifndef NDEBUG
    if (!atomic_load_bool_impl(&g_exiting)) {
      debug_sync_check_cond_deadlocks();
      mutex_stack_detect_deadlocks();
    }
#endif
    mutex_lock(&g_mutex);
    uint64_t now = time_get_ns();
    uint64_t wait = 100 * NS_PER_MS_INT;
    uint64_t deadlines[] = {g_state_deadline, g_errno_deadline, g_backtrace_deadline, g_memory_deadline};
    for (size_t i = 0; i < sizeof(deadlines) / sizeof(deadlines[0]); ++i)
      if (deadlines[i]) {
        uint64_t remaining = deadlines[i] > now ? deadlines[i] - now : 1;
        if (remaining < wait)
          wait = remaining;
      }
    if (!atomic_load_bool_impl(&g_exiting))
      cond_timedwait(&g_condition, &g_mutex, wait);
    mutex_unlock(&g_mutex);
  }
  return NULL;
}
#endif
asciichat_error_t debug_stats_start_thread(void) {
  if (g_started)
    return ASCIICHAT_OK;
  asciichat_error_t result = debug_stats_init();
  if (result != ASCIICHAT_OK)
    return result;
  atomic_store_bool_impl(&g_exiting, false);
  atomic_store_bool_impl(&g_cleaning, false);
  options_t *opts = options_get();
  if (opts) {
    if (IS_OPTION_EXPLICIT(debug_sync_state_time, opts))
      debug_stats_print_state_delayed((uint64_t)(opts->debug_sync_state_time * NS_PER_SEC_INT));
    if (IS_OPTION_EXPLICIT(debug_errno_stacks_time, opts))
      debug_stats_print_errno_delayed((uint64_t)(opts->debug_errno_stacks_time * NS_PER_SEC_INT));
    if (IS_OPTION_EXPLICIT(debug_backtrace_time, opts))
      debug_stats_print_backtrace_delayed((uint64_t)(opts->debug_backtrace_time * NS_PER_SEC_INT));
    if (opts->debug_memory_report_interval > 0)
      debug_stats_set_memory_report_interval((uint64_t)(opts->debug_memory_report_interval * NS_PER_SEC_INT));
  }
#ifndef EMSCRIPTEN_BUILD
  if (asciichat_thread_create(&g_thread, "debug_stats", debug_stats_worker, NULL) != 0)
    return SET_ERRNO(ERROR_THREAD, "Cannot start diagnostics worker");
#endif
  g_started = true;
  return ASCIICHAT_OK;
}
void debug_stats_cleanup_thread(void) {
  if (!g_started)
    return;
  atomic_store_bool_impl(&g_cleaning, true);
  mutex_lock(&g_mutex);
  atomic_store_bool_impl(&g_exiting, true);
  cond_signal(&g_condition);
  mutex_unlock(&g_mutex);
#ifndef EMSCRIPTEN_BUILD
  // Join fully: diagnostic data must not be destroyed while a reader is alive.
  if (asciichat_thread_join(&g_thread, NULL) != 0)
    FATAL(ERROR_THREAD, "Cannot join diagnostics worker");
#endif
  g_started = false;
}
void debug_stats_destroy(void) {
  debug_stats_cleanup_thread();
  if (g_initialized) {
    cond_destroy(&g_condition);
    mutex_destroy(&g_mutex);
    g_initialized = false;
    atomic_store_bool_impl(&g_signal, false);
    g_state_deadline = g_errno_deadline = g_backtrace_deadline = g_memory_deadline = g_memory_interval = 0;
  }
}
void debug_stats_final_cleanup(void) {
#ifndef NDEBUG
  mutex_stack_cleanup_current_thread();
#endif
}
