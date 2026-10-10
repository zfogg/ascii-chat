#include <ascii-chat/debug/errno.h>
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
static mutex_t g_mutex;
static cond_t g_condition;
static bool g_initialized;
static bool g_started;
static uint64_t g_errno_deadline;

asciichat_error_t debug_errno_init(void) {
  if (g_initialized)
    return ASCIICHAT_OK;
  if (mutex_init(&g_mutex, "debug_errno") != 0)
    return SET_ERRNO(ERROR_THREAD, "Cannot initialize diagnostics mutex");
  if (cond_init(&g_condition, "debug_errno") != 0) {
    mutex_destroy(&g_mutex);
    return SET_ERRNO(ERROR_THREAD, "Cannot initialize diagnostics condition");
  }
  g_initialized = true;
  atomic_store_bool_impl(&g_cleaning, false);
  return ASCIICHAT_OK;
}
static void debug_errno_print_errno(void) {
  log_info("Errno stacks:");
  asciichat_errno_print_stacks();
  asciichat_errno_print_history();
  asciichat_errno_print_hash_stats();
}
void debug_errno_print(void) {
  debug_errno_print_errno();
}
void debug_errno_print_delayed(uint64_t delay) {
  if (!g_initialized)
    return;
  debug_report_schedule(&g_mutex, &g_condition, &g_errno_deadline, delay);
}
void debug_errno_trigger_print(void) {
  // Do not signal a condition variable or invoke diagnostic hooks in a signal
  // handler. The worker checks this flag at most 100ms after its next wakeup.
  atomic_store_bool_impl(&g_signal, true);
}
bool debug_errno_is_cleanup_in_progress(void) {
  return atomic_load_bool_impl(&g_cleaning);
}
void debug_errno_poll(void) {
  if (!g_initialized)
    return;
  uint64_t now = time_get_ns();
  bool all = atomic_exchange_bool_impl(&g_signal, false);
  mutex_lock(&g_mutex);
  bool errors = g_errno_deadline && now >= g_errno_deadline;
  if (errors)
    g_errno_deadline = 0;
  mutex_unlock(&g_mutex);
  if (all || errors)
    debug_errno_print_errno();
}
#ifndef EMSCRIPTEN_BUILD
static void *debug_errno_worker(void *unused) {
  (void)unused;
  while (!atomic_load_bool_impl(&g_exiting)) {
    debug_errno_poll();
    mutex_lock(&g_mutex);
    uint64_t now = time_get_ns();
    uint64_t wait = 100 * NS_PER_MS_INT;
    uint64_t deadlines[] = {g_errno_deadline};
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
asciichat_error_t debug_errno_start_thread(void) {
  if (g_started)
    return ASCIICHAT_OK;
  asciichat_error_t result = debug_errno_init();
  if (result != ASCIICHAT_OK)
    return result;
  atomic_store_bool_impl(&g_exiting, false);
  atomic_store_bool_impl(&g_cleaning, false);
  options_t *opts = options_get();
  if (opts) {
    if (IS_OPTION_EXPLICIT(debug_errno_stacks_time, opts))
      debug_errno_print_delayed((uint64_t)(opts->debug_errno_stacks_time * NS_PER_SEC_INT));
  }
#ifndef EMSCRIPTEN_BUILD
  if (asciichat_thread_create(&g_thread, "debug_errno", debug_errno_worker, NULL) != 0)
    return SET_ERRNO(ERROR_THREAD, "Cannot start diagnostics worker");
#endif
  g_started = true;
  return ASCIICHAT_OK;
}
void debug_errno_cleanup_thread(void) {
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
void debug_errno_destroy(void) {
  debug_errno_cleanup_thread();
  if (g_initialized) {
    cond_destroy(&g_condition);
    mutex_destroy(&g_mutex);
    g_initialized = false;
    atomic_store_bool_impl(&g_signal, false);
    g_errno_deadline = 0;
  }
}
