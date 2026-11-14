#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <stdatomic.h>
#include <ascii-chat/debug/mutex.h>
#include <libplatform/thread.h>
#include <libplatform/cond.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/common.h>
#include <libplatform/atomic.h>
#include <libplatform/debug/sync.h>
#include <ascii-chat/platform/rwlock.h>
#include <string.h>

#ifndef NDEBUG
// Internal formatters back the public diagnostic log_state functions.
extern "C" {
extern int mutex_format_state(const mutex_t *mutex, char *buffer, size_t size);
extern int rwlock_format_state(const rwlock_t *lock, char *buffer, size_t size);
extern int cond_format_state(const cond_t *condition, char *buffer, size_t size);
}

TEST_CASE("debug_mutex/formatted_mutex_state_tracks_acquisition_and_release", "[debug_mutex]") {
  mutex_t mutex;
  char text[1024];
  ASCIICHAT_REQUIRE_EQ(mutex_init(&mutex, "formatted-mutex"), 0);
  ASCIICHAT_REQUIRE_EQ(mutex_format_state(NULL, text, sizeof(text)), 0);
  ASCIICHAT_REQUIRE_EQ(mutex_format_state(&mutex, NULL, sizeof(text)), 0);
  ASCIICHAT_REQUIRE_EQ(mutex_format_state(&mutex, text, 0), 0);
  ASCIICHAT_REQUIRE_GT(mutex_format_state(&mutex, text, sizeof(text)), 0);
  ASCIICHAT_REQUIRE_NULL(strstr(text, "LOCKED_BY"));
  ASCIICHAT_REQUIRE_EQ(mutex_lock(&mutex), 0);
  ASCIICHAT_REQUIRE_GT(mutex_format_state(&mutex, text, sizeof(text)), 0);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "lock="));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "LOCKED_BY=thread."));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "lock=1 unlock=0"));
  ASCIICHAT_REQUIRE_EQ(mutex_unlock(&mutex), 0);
  ASCIICHAT_REQUIRE_EQ(mutex_trylock(&mutex), 0);
  ASCIICHAT_REQUIRE_EQ(mutex_unlock(&mutex), 0);
  ASCIICHAT_REQUIRE_GT(mutex_format_state(&mutex, text, sizeof(text)), 0);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "unlock="));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "trylock=1/1"));
  ASCIICHAT_REQUIRE_NULL(strstr(text, "LOCKED_BY"));
  struct {
    char text[2];
    char guard;
  } short_output = {.guard = 'G'};
  ASCIICHAT_REQUIRE_GT(mutex_format_state(&mutex, short_output.text, sizeof(short_output.text)), 1);
  ASCIICHAT_REQUIRE_EQ(short_output.text[1], '\0');
  ASCIICHAT_REQUIRE_EQ(short_output.guard, 'G');
  ASCIICHAT_REQUIRE_EQ(mutex_destroy(&mutex), 0);
}

TEST_CASE("debug_mutex/formatted_rwlock_distinguishes_readers_writer_and_released_state", "[debug_mutex]") {
  rwlock_t lock;
  char text[1024];
  ASCIICHAT_REQUIRE_EQ(rwlock_init(&lock, "formatted-rwlock"), 0);
  ASCIICHAT_REQUIRE_EQ(rwlock_format_state(NULL, text, sizeof(text)), 0);
  ASCIICHAT_REQUIRE_EQ(rwlock_format_state(&lock, NULL, sizeof(text)), 0);
  ASCIICHAT_REQUIRE_EQ(rwlock_format_state(&lock, text, 0), 0);
  ASCIICHAT_REQUIRE_EQ(rwlock_rdlock(&lock), 0);
  ASCIICHAT_REQUIRE_GT(rwlock_format_state(&lock, text, sizeof(text)), 0);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "READ_LOCKED=1"));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "rdlock="));
  ASCIICHAT_REQUIRE_EQ(rwlock_rdunlock(&lock), 0);
  ASCIICHAT_REQUIRE_EQ(rwlock_wrlock(&lock), 0);
  ASCIICHAT_REQUIRE_GT(rwlock_format_state(&lock, text, sizeof(text)), 0);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "WRITE_LOCKED_BY=thread."));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "wrlock="));
  ASCIICHAT_REQUIRE_NULL(strstr(text, "READ_LOCKED="));
  ASCIICHAT_REQUIRE_EQ(rwlock_wrunlock(&lock), 0);
  ASCIICHAT_REQUIRE_GT(rwlock_format_state(&lock, text, sizeof(text)), 0);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "unlock="));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "rdlock=1 wrlock=1 unlock=2"));
  ASCIICHAT_REQUIRE_NULL(strstr(text, "LOCKED"));
  ASCIICHAT_REQUIRE_EQ(rwlock_destroy(&lock), 0);
}

TEST_CASE("debug_mutex/formatted_condition_state_tracks_timeout_signal_and_broadcast", "[debug_mutex]") {
  mutex_t mutex;
  cond_t condition;
  char text[1024];
  ASCIICHAT_REQUIRE_EQ(mutex_init(&mutex, "formatted-condition-lock"), 0);
  ASCIICHAT_REQUIRE_EQ(cond_init(&condition, "formatted-condition"), 0);
  ASCIICHAT_REQUIRE_EQ(cond_format_state(NULL, text, sizeof(text)), 0);
  ASCIICHAT_REQUIRE_EQ(cond_format_state(&condition, NULL, sizeof(text)), 0);
  ASCIICHAT_REQUIRE_EQ(cond_format_state(&condition, text, 0), 0);
  ASCIICHAT_REQUIRE_EQ(mutex_lock(&mutex), 0);
  ASCIICHAT_REQUIRE_NE(cond_timedwait(&condition, &mutex, 1000000), 0);
  ASCIICHAT_REQUIRE_EQ(cond_signal(&condition), 0);
  ASCIICHAT_REQUIRE_EQ(cond_broadcast(&condition), 0);
  ASCIICHAT_REQUIRE_GT(cond_format_state(&condition, text, sizeof(text)), 0);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "wait="));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "signal="));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "broadcast="));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "wait=1 signal=1 broadcast=1"));
  ASCIICHAT_REQUIRE_NULL(strstr(text, "WAITING="));
  ASCIICHAT_REQUIRE_EQ(mutex_unlock(&mutex), 0);
  ASCIICHAT_REQUIRE_EQ(cond_destroy(&condition), 0);
  ASCIICHAT_REQUIRE_EQ(mutex_destroy(&mutex), 0);
}

static atomic_bool stack_stress_running;

static void *change_lock_stack(void *unused) {
  (void)unused;
  uintptr_t key = (uintptr_t)&unused;
  while (atomic_load(&stack_stress_running)) {
    mutex_stack_push_pending(key, "snapshot stress");
    mutex_stack_mark_locked(key);
    mutex_stack_pop(key);
  }
  return NULL;
}

TEST_CASE("debug_mutex/concurrent_stack_snapshots", "[debug_mutex]") {
  platform_thread_t writers[4];
  atomic_store(&stack_stress_running, true);
  for (int i = 0; i < 4; i++)
    ASCIICHAT_REQUIRE_EQ(platform_thread_create(&writers[i], "stack_stress", change_lock_stack, NULL), ASCIICHAT_OK);

  for (int iteration = 0; iteration < 2000; iteration++) {
    mutex_stack_entry_t **stacks = NULL;
    int *counts = NULL;
    int count = 0;
    ASCIICHAT_REQUIRE_EQ(mutex_stack_get_all_threads(&stacks, &counts, &count), 0);
    for (int i = 0; i < count; i++) {
      ASCIICHAT_REQUIRE_GE(counts[i], 0);
      ASCIICHAT_REQUIRE_LE(counts[i], 64);
    }
    mutex_stack_free_all_threads(stacks, counts, count);
    mutex_stack_detect_deadlocks();
  }

  atomic_store(&stack_stress_running, false);
  for (int i = 0; i < 4; i++)
    ASCIICHAT_REQUIRE_EQ(thread_join(&writers[i], NULL), ASCIICHAT_OK);
}

static atomic_t cond_stress_running = {0};

static void *inspect_conditions(void *unused) {
  (void)unused;
  while (atomic_load_bool(&cond_stress_running))
    debug_sync_check_cond_deadlocks();
  return NULL;
}

TEST_CASE("debug_mutex/concurrent_condition_destruction", "[debug_mutex]") {
  // The monitor must not dereference a condition after unregistration frees it.
  NAMED_REGISTER_ATOMIC(&cond_stress_running, "cond_stress_running", NULL);
  atomic_store_bool(&cond_stress_running, true);
  platform_thread_t monitor;
  ASCIICHAT_REQUIRE_EQ(platform_thread_create(&monitor, "cond_monitor", inspect_conditions, NULL), ASCIICHAT_OK);
  for (int i = 0; i < 5000; ++i) {
    cond_t *cond = SAFE_CALLOC(1, sizeof(*cond), cond_t *);
    ASCIICHAT_REQUIRE_EQ(cond_init(cond, "condition_lifetime"), 0);
    ASCIICHAT_REQUIRE_EQ(cond_destroy(cond), 0);
    SAFE_FREE(cond);
  }
  atomic_store_bool(&cond_stress_running, false);
  ASCIICHAT_REQUIRE_EQ(thread_join(&monitor, NULL), ASCIICHAT_OK);
  NAMED_UNREGISTER(&cond_stress_running);
}
#endif
