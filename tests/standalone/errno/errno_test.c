/** Native tests use the production library and platform threads, including on Windows. */
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/errno.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/debug/stats.h>
#include <ascii-chat/atomic.h>
#include <assert.h>
#include <string.h>

#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr))                                                                                                       \
      FATAL(ERROR_ASSERTION_FAILED, "Test failed: %s at line %d", #expr, __LINE__);                                    \
  } while (0)

static void stack_contract(void) {
  CLEAR_ERRNO_ALL();
  CHECK(!HAS_ERRNO(NULL));
  CHECK(!HAS_ERRNO_CODE(ASCIICHAT_OK));
  CHECK(!HAS_ERRNO_CODE_TID(UINT64_MAX, ERROR_NETWORK));
  int argument = 0;
  CHECK(SET_ERRNO(ERROR_NETWORK, "root %d", ++argument) == ERROR_NETWORK);
  CHECK(argument == 1);
  asciichat_error_context_t root, copy;
  CHECK(HAS_ERRNO(&root));
  asciichat_errno_scope_t checkpoint = asciichat_errno_checkpoint();
  SET_ERRNO(ERROR_CRYPTO, "wrapper");
  CHECK(HAS_ERRNO_CODE(ERROR_NETWORK));
  CHECK(HAS_ERRNO_CODE_TID(root.thread_id, ERROR_CRYPTO));
  CHECK(HAS_ERRNO(&copy) && copy.cause_id == root.error_id);
  CHECK(HAS_ERRNO_CODE_SINCE(checkpoint, ERROR_CRYPTO));
  CHECK(!HAS_ERRNO_CODE_SINCE(checkpoint, ERROR_NETWORK));
  CHECK(copy.error_id > root.error_id);
  CHECK(!asciichat_clear_errno_if_top(root.thread_id, root.generation, root.error_id));
  CHECK(asciichat_clear_errno_if_top(copy.thread_id, copy.generation, copy.error_id));
  CHECK(GET_ERRNO() == ERROR_NETWORK);
  CLEAR_ERRNO();
  CHECK(GET_ERRNO() == ASCIICHAT_OK);
  CHECK(strcmp(root.context_message, "root 1") == 0);
  CLEAR_ERRNO();
  SET_ERRNO(ASCIICHAT_OK, "not an error");
  CHECK(!HAS_ERRNO(NULL));

  asciichat_set_errno_with_system_error(ERROR_NETWORK, __FILE__, __LINE__, __func__, 42);
  CHECK(HAS_ERRNO(&copy) && copy.has_system_error && copy.system_errno == 42);
  asciichat_set_errno_with_wsa_error(ERROR_NETWORK, __FILE__, __LINE__, __func__, 10054);
  CHECK(HAS_ERRNO(&copy) && copy.has_wsa_error && !copy.has_system_error && copy.wsa_error == 10054);
  SET_ERRNO(ERROR_CONFIG, "plain");
  CHECK(HAS_ERRNO(&copy) && !copy.has_wsa_error && !copy.has_system_error);
  CLEAR_ERRNO_ALL();

  char long_message[ASCIICHAT_ERRNO_MESSAGE_MAX * 2];
  memset(long_message, 'x', sizeof(long_message) - 1);
  long_message[sizeof(long_message) - 1] = 0;
  SET_ERRNO(ERROR_CONFIG, "%s", long_message);
  CHECK(HAS_ERRNO(&copy) && copy.message_truncated);
  CHECK(strlen(copy.context_message) == ASCIICHAT_ERRNO_MESSAGE_MAX - 1);
  CLEAR_ERRNO_ALL();
}

static void scopes_and_history(void) {
  SET_ERRNO(ERROR_AUDIO, "unrelated older failure");
  asciichat_errno_scope_t scope = asciichat_errno_scope_begin();
  CHECK(asciichat_errno_scope_is_clean(scope));
  SET_ERRNO(ERROR_MEDIA_OPEN, "yt-dlp attempt");
  CHECK(HAS_ERRNO_CODE_SINCE(scope, ERROR_MEDIA_OPEN));
  CHECK(!HAS_ERRNO_CODE_SINCE(scope, ERROR_AUDIO));
  CHECK(!asciichat_errno_scope_is_clean(scope));
  asciichat_error_context_t attempt;
  CHECK(HAS_ERRNO(&attempt));
  asciichat_errno_scope_t nested = asciichat_errno_scope_begin();
  SET_ERRNO(ERROR_NETWORK, "nested attempt");
  asciichat_errno_scope_end(nested, ASCIICHAT_ERRNO_HANDLED);
  CHECK(HAS_ERRNO_CODE(ERROR_MEDIA_OPEN));
  platform_sleep_ns(NS_PER_MS_INT);
  asciichat_errno_scope_end(scope, ASCIICHAT_ERRNO_DISMISSED);
  ASSERT_NO_ERRNO_SINCE(scope);
  CHECK(GET_ERRNO() == ERROR_AUDIO);
  asciichat_errno_history_t history[8];
  CHECK(asciichat_errno_history_snapshot(history, 8) >= 2);
  CHECK(history[0].context.error_id == attempt.error_id);
  CHECK(history[0].outcome == ASCIICHAT_ERRNO_DISMISSED);
  CHECK(history[0].resolved_ns >= history[0].context.created_ns);
  CLEAR_ERRNO_ALL();

  SET_ERRNO(ERROR_CONFIG, "old registration");
  CHECK(HAS_ERRNO(&attempt));
  asciichat_errno_destroy();
  SET_ERRNO(ERROR_CONFIG, "new registration");
  asciichat_error_context_t next;
  CHECK(HAS_ERRNO(&next) && next.generation != attempt.generation);
  CHECK(!asciichat_clear_errno_if_top(next.thread_id, attempt.generation, next.error_id));
  asciichat_errno_scope_end(scope, ASCIICHAT_ERRNO_DISMISSED);
  CHECK(HAS_ERRNO(NULL));
  CLEAR_ERRNO_ALL();
}

static void capacity_contract(void) {
  asciichat_error_stats_reset();
  SET_ERRNO(ERROR_MEDIA_OPEN, "root");
  asciichat_error_context_t root;
  CHECK(HAS_ERRNO(&root));
  for (int i = 0; i < ASCIICHAT_ERRNO_MAX_DEPTH * 4; ++i)
    SET_ERRNO(ERROR_NETWORK, "attempt %d", i);
  uint64_t tid = asciichat_thread_current_id();
  asciichat_error_context_t frames[ASCIICHAT_ERRNO_MAX_DEPTH];
  CHECK(asciichat_errno_snapshot(tid, frames, ASCIICHAT_ERRNO_MAX_DEPTH) == ASCIICHAT_ERRNO_MAX_DEPTH);
  CHECK(frames[ASCIICHAT_ERRNO_MAX_DEPTH - 1].error_id == root.error_id);
  CHECK(strcmp(frames[0].context_message, "attempt 255") == 0);
  CHECK(asciichat_errno_snapshot(tid, frames, 1) == ASCIICHAT_ERRNO_MAX_DEPTH);
  CHECK(asciichat_error_stats_get().dropped_errors > 0);
  CLEAR_ERRNO_ALL();
  CHECK(asciichat_error_stats_get().pending_errors == 0);
  CHECK(asciichat_errno_history_snapshot(NULL, 0) == ASCIICHAT_ERRNO_HISTORY_SIZE);
  CHECK(asciichat_error_stats_get().overwritten_history > 0);
}

typedef struct {
  atomic_t ready;
  atomic_t release;
  uint64_t tid;
  bool overflow;
} worker_data_t;

static void *held_error_worker(void *arg) {
  worker_data_t *data = arg;
  asciichat_errno_suppress(true);
  data->tid = asciichat_thread_current_id();
  for (int i = 0; i < (data->overflow ? ASCIICHAT_ERRNO_MAX_DEPTH : 1); ++i)
    SET_ERRNO(ERROR_NETWORK, "worker %d", i);
  atomic_store_bool_impl(&data->ready, true);
  while (!atomic_load_bool_impl(&data->release))
    platform_sleep_ns(NS_PER_MS_INT);
  // Deliberately leave pending frames to exercise automatic thread cleanup.
  return NULL;
}

static void cross_thread_contract(void) {
  worker_data_t data = {0};
  asciichat_thread_t thread;
  CHECK(asciichat_thread_create(&thread, "errno-test", held_error_worker, &data) == 0);
  while (!atomic_load_bool_impl(&data.ready))
    platform_sleep_ns(NS_PER_MS_INT);
  asciichat_error_context_t snapshot;
  CHECK(asciichat_has_thread_errno(data.tid, &snapshot));
  CHECK(HAS_ERRNO_CODE_TID(data.tid, ERROR_NETWORK));
  CLEAR_ERRNO_TID(data.tid);
  CHECK(!HAS_ERRNO_CODE_TID(data.tid, ERROR_NETWORK));
  CHECK(strcmp(snapshot.context_message, "worker 0") == 0);
  atomic_store_bool_impl(&data.release, true);
  CHECK(asciichat_thread_join(&thread, NULL) == 0);
  SET_ERRNO(ERROR_AUDIO, "recording survives worker exit");
  CHECK(GET_ERRNO() == ERROR_AUDIO);
  CLEAR_ERRNO_ALL();

  asciichat_error_stats_t before = asciichat_error_stats_get();
  memset(&data, 0, sizeof(data));
  CHECK(asciichat_thread_create(&thread, "errno-exit-test", held_error_worker, &data) == 0);
  while (!atomic_load_bool_impl(&data.ready))
    platform_sleep_ns(NS_PER_MS_INT);
  atomic_store_bool_impl(&data.release, true);
  CHECK(asciichat_thread_join(&thread, NULL) == 0);
  CHECK(!HAS_ERRNO_CODE_TID(data.tid, ERROR_NETWORK));
  CHECK(asciichat_error_stats_get().registered_threads == before.registered_threads);
}

static void *stress_worker(void *arg) {
  atomic_t *done = arg;
  asciichat_errno_suppress(true);
  for (int i = 0; i < 1000; ++i) {
    asciichat_errno_scope_t scope = asciichat_errno_scope_begin();
    SET_ERRNO(ERROR_NETWORK, "root %d", i);
    SET_ERRNO(ERROR_MEDIA_OPEN, "wrapper %d", i);
    asciichat_errno_scope_end(scope, ASCIICHAT_ERRNO_DISMISSED);
  }
  atomic_fetch_add_int_impl(done, 1);
  return NULL;
}

static void concurrency_contract(void) {
  asciichat_thread_t threads[8];
  atomic_t done = {0};
  for (size_t i = 0; i < 8; ++i)
    CHECK(asciichat_thread_create(&threads[i], "errno-stress", stress_worker, &done) == 0);
  while (atomic_load_int_impl(&done) < 8) {
    asciichat_errno_history_t history[4];
    asciichat_errno_history_snapshot(history, 4);
    platform_sleep_ns(NS_PER_MS_INT);
  }
  for (size_t i = 0; i < 8; ++i)
    CHECK(asciichat_thread_join(&threads[i], NULL) == 0);
  CHECK(asciichat_error_stats_get().pending_errors == 0);

  worker_data_t data[20] = {0};
  asciichat_thread_t held[20];
  for (size_t i = 0; i < 20; ++i) {
    data[i].overflow = true;
    CHECK(asciichat_thread_create(&held[i], "errno-budget", held_error_worker, &data[i]) == 0);
    while (!atomic_load_bool_impl(&data[i].ready))
      platform_sleep_ns(NS_PER_MS_INT);
  }
  CHECK(asciichat_error_stats_get().pending_errors <= ASCIICHAT_ERRNO_MAX_FRAMES);
  for (size_t i = 0; i < 20; ++i) {
    asciichat_error_context_t contexts[ASCIICHAT_ERRNO_MAX_DEPTH];
    size_t depth = asciichat_errno_snapshot(data[i].tid, contexts, ASCIICHAT_ERRNO_MAX_DEPTH);
    CHECK(depth >= 2);
    CHECK(strcmp(contexts[0].context_message, "worker 63") == 0);
    CHECK(strcmp(contexts[depth - 1].context_message, "worker 0") == 0);
  }
  CHECK(asciichat_error_stats_get().dropped_errors > 0);
  for (size_t i = 0; i < 20; ++i) {
    CLEAR_ERRNO_ALL_TID(data[i].tid);
    atomic_store_bool_impl(&data[i].release, true);
  }
  for (size_t i = 0; i < 20; ++i)
    CHECK(asciichat_thread_join(&held[i], NULL) == 0);
  CHECK(asciichat_error_stats_get().pending_errors == 0);
}

int main(int argc, char **argv) {
  log_init(NULL, LOG_WARN, false, false);
  asciichat_errno_suppress(true);
  if (argc > 1 && strcmp(argv[1], "--assert-leak") == 0) {
    asciichat_errno_scope_t scope = asciichat_errno_scope_begin();
    SET_ERRNO(ERROR_NETWORK, "unresolved");
    ASSERT_NO_ERRNO_SINCE(scope);
    return 0;
  }
  stack_contract();
  scopes_and_history();
  capacity_contract();
  cross_thread_contract();
  concurrency_contract();
  CHECK(debug_stats_init() == ASCIICHAT_OK);
  CHECK(debug_stats_start_thread() == ASCIICHAT_OK);
  debug_stats_print_state_delayed(60 * NS_PER_SEC_INT);
  debug_stats_print_backtrace_delayed(60 * NS_PER_SEC_INT);
  platform_sleep_ns(5 * NS_PER_MS_INT);
  uint64_t start = time_get_ns();
  debug_stats_destroy();
  CHECK(time_get_ns() - start < NS_PER_SEC_INT);
  asciichat_errno_destroy();
  CHECK(asciichat_error_stats_get().registered_threads == 0);
  asciichat_errno_request_exit(ERROR_CRYPTO_AUTH);
  asciichat_errno_request_exit(ERROR_MEDIA_OPEN);
  CLEAR_ERRNO_ALL();
  asciichat_errno_shutdown();
  CHECK(asciichat_errno_exit_code() == ERROR_CRYPTO_AUTH);
  asciichat_errno_shutdown();
  log_warn("All errno stack, scope, history, capacity, concurrency, lifecycle and scheduler checks passed");
  log_destroy();
  return 0;
}
