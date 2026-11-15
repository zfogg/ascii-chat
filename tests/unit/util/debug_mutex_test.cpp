#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <stdatomic.h>
#include <ascii-chat/debug/mutex.h>
#include <libplatform/thread.h>
#include <libplatform/cond.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/common.h>
#include <libplatform/atomic.h>
#include <libplatform/debug/sync.h>
#include <libplatform/rwlock.h>
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
  REQUIRE((mutex_init(&mutex, "formatted-mutex")) == (0));
  REQUIRE((mutex_format_state(NULL, text, sizeof(text))) == (0));
  REQUIRE((mutex_format_state(&mutex, NULL, sizeof(text))) == (0));
  REQUIRE((mutex_format_state(&mutex, text, 0)) == (0));
  REQUIRE((mutex_format_state(&mutex, text, sizeof(text))) > (0));
  REQUIRE((strstr(text, "LOCKED_BY")) == nullptr);
  REQUIRE((mutex_lock(&mutex)) == (0));
  REQUIRE((mutex_format_state(&mutex, text, sizeof(text))) > (0));
  REQUIRE((strstr(text, "lock=")) != nullptr);
  REQUIRE((strstr(text, "LOCKED_BY=thread.")) != nullptr);
  REQUIRE((strstr(text, "lock=1 unlock=0")) != nullptr);
  REQUIRE((mutex_unlock(&mutex)) == (0));
  REQUIRE((mutex_trylock(&mutex)) == (0));
  REQUIRE((mutex_unlock(&mutex)) == (0));
  REQUIRE((mutex_format_state(&mutex, text, sizeof(text))) > (0));
  REQUIRE((strstr(text, "unlock=")) != nullptr);
  REQUIRE((strstr(text, "trylock=1/1")) != nullptr);
  REQUIRE((strstr(text, "LOCKED_BY")) == nullptr);
  struct {
    char text[2];
    char guard;
  } short_output = {.guard = 'G'};
  REQUIRE((mutex_format_state(&mutex, short_output.text, sizeof(short_output.text))) > (1));
  REQUIRE((short_output.text[1]) == ('\0'));
  REQUIRE((short_output.guard) == ('G'));
  REQUIRE((mutex_destroy(&mutex)) == (0));
}

TEST_CASE("debug_mutex/formatted_rwlock_distinguishes_readers_writer_and_released_state", "[debug_mutex]") {
  rwlock_t lock;
  char text[1024];
  REQUIRE((rwlock_init(&lock, "formatted-rwlock")) == (0));
  REQUIRE((rwlock_format_state(NULL, text, sizeof(text))) == (0));
  REQUIRE((rwlock_format_state(&lock, NULL, sizeof(text))) == (0));
  REQUIRE((rwlock_format_state(&lock, text, 0)) == (0));
  REQUIRE((rwlock_rdlock(&lock)) == (0));
  REQUIRE((rwlock_format_state(&lock, text, sizeof(text))) > (0));
  REQUIRE((strstr(text, "READ_LOCKED=1")) != nullptr);
  REQUIRE((strstr(text, "rdlock=")) != nullptr);
  REQUIRE((rwlock_rdunlock(&lock)) == (0));
  REQUIRE((rwlock_wrlock(&lock)) == (0));
  REQUIRE((rwlock_format_state(&lock, text, sizeof(text))) > (0));
  REQUIRE((strstr(text, "WRITE_LOCKED_BY=thread.")) != nullptr);
  REQUIRE((strstr(text, "wrlock=")) != nullptr);
  REQUIRE((strstr(text, "READ_LOCKED=")) == nullptr);
  REQUIRE((rwlock_wrunlock(&lock)) == (0));
  REQUIRE((rwlock_format_state(&lock, text, sizeof(text))) > (0));
  REQUIRE((strstr(text, "unlock=")) != nullptr);
  REQUIRE((strstr(text, "rdlock=1 wrlock=1 unlock=2")) != nullptr);
  REQUIRE((strstr(text, "LOCKED")) == nullptr);
  REQUIRE((rwlock_destroy(&lock)) == (0));
}

TEST_CASE("debug_mutex/formatted_condition_state_tracks_timeout_signal_and_broadcast", "[debug_mutex]") {
  mutex_t mutex;
  cond_t condition;
  char text[1024];
  REQUIRE((mutex_init(&mutex, "formatted-condition-lock")) == (0));
  REQUIRE((cond_init(&condition, "formatted-condition")) == (0));
  REQUIRE((cond_format_state(NULL, text, sizeof(text))) == (0));
  REQUIRE((cond_format_state(&condition, NULL, sizeof(text))) == (0));
  REQUIRE((cond_format_state(&condition, text, 0)) == (0));
  REQUIRE((mutex_lock(&mutex)) == (0));
  REQUIRE((cond_timedwait(&condition, &mutex, 1000000)) != (0));
  REQUIRE((cond_signal(&condition)) == (0));
  REQUIRE((cond_broadcast(&condition)) == (0));
  REQUIRE((cond_format_state(&condition, text, sizeof(text))) > (0));
  REQUIRE((strstr(text, "wait=")) != nullptr);
  REQUIRE((strstr(text, "signal=")) != nullptr);
  REQUIRE((strstr(text, "broadcast=")) != nullptr);
  REQUIRE((strstr(text, "wait=1 signal=1 broadcast=1")) != nullptr);
  REQUIRE((strstr(text, "WAITING=")) == nullptr);
  REQUIRE((mutex_unlock(&mutex)) == (0));
  REQUIRE((cond_destroy(&condition)) == (0));
  REQUIRE((mutex_destroy(&mutex)) == (0));
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
    REQUIRE((platform_thread_create(&writers[i], "stack_stress", change_lock_stack, NULL)) == (ASCIICHAT_OK));

  for (int iteration = 0; iteration < 2000; iteration++) {
    mutex_stack_entry_t **stacks = NULL;
    int *counts = NULL;
    int count = 0;
    REQUIRE((mutex_stack_get_all_threads(&stacks, &counts, &count)) == (0));
    for (int i = 0; i < count; i++) {
      REQUIRE((counts[i]) >= (0));
      REQUIRE((counts[i]) <= (64));
    }
    mutex_stack_free_all_threads(stacks, counts, count);
    mutex_stack_detect_deadlocks();
  }

  atomic_store(&stack_stress_running, false);
  for (int i = 0; i < 4; i++)
    REQUIRE((thread_join(&writers[i], NULL)) == (ASCIICHAT_OK));
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
  REQUIRE((platform_thread_create(&monitor, "cond_monitor", inspect_conditions, NULL)) == (ASCIICHAT_OK));
  for (int i = 0; i < 5000; ++i) {
    cond_t *cond = SAFE_CALLOC(1, sizeof(*cond), cond_t *);
    REQUIRE((cond_init(cond, "condition_lifetime")) == (0));
    REQUIRE((cond_destroy(cond)) == (0));
    SAFE_FREE(cond);
  }
  atomic_store_bool(&cond_stress_running, false);
  REQUIRE((thread_join(&monitor, NULL)) == (ASCIICHAT_OK));
  NAMED_UNREGISTER(&cond_stress_running);
}
#endif
