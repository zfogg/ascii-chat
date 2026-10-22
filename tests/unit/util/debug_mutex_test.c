#include <criterion/criterion.h>
#include <stdatomic.h>
#include <ascii-chat/debug/mutex.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/platform/cond.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/common.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/platform/rwlock.h>
#include <string.h>

#ifndef NDEBUG
// Internal formatters back the public diagnostic log_state functions.
extern int mutex_format_state(const mutex_t *mutex, char *buffer, size_t size);
extern int rwlock_format_state(const rwlock_t *lock, char *buffer, size_t size);
extern int cond_format_state(const cond_t *condition, char *buffer, size_t size);

Test(debug_mutex, formatted_mutex_state_tracks_acquisition_and_release) {
  mutex_t mutex;
  char text[1024];
  cr_assert_eq(mutex_init(&mutex, "formatted-mutex"), 0);
  cr_assert_eq(mutex_format_state(NULL, text, sizeof(text)), 0);
  cr_assert_eq(mutex_format_state(&mutex, NULL, sizeof(text)), 0);
  cr_assert_eq(mutex_format_state(&mutex, text, 0), 0);
  cr_assert_gt(mutex_format_state(&mutex, text, sizeof(text)), 0);
  cr_assert_null(strstr(text, "LOCKED_BY"));
  cr_assert_eq(mutex_lock(&mutex), 0);
  cr_assert_gt(mutex_format_state(&mutex, text, sizeof(text)), 0);
  cr_assert_not_null(strstr(text, "lock="));
  cr_assert_not_null(strstr(text, "LOCKED_BY=thread."));
  cr_assert_not_null(strstr(text, "lock=1 unlock=0"));
  cr_assert_eq(mutex_unlock(&mutex), 0);
  cr_assert_eq(mutex_trylock(&mutex), 0);
  cr_assert_eq(mutex_unlock(&mutex), 0);
  cr_assert_gt(mutex_format_state(&mutex, text, sizeof(text)), 0);
  cr_assert_not_null(strstr(text, "unlock="));
  cr_assert_not_null(strstr(text, "trylock=1/1"));
  cr_assert_null(strstr(text, "LOCKED_BY"));
  struct {
    char text[2];
    char guard;
  } short_output = {.guard = 'G'};
  cr_assert_gt(mutex_format_state(&mutex, short_output.text, sizeof(short_output.text)), 1);
  cr_assert_eq(short_output.text[1], '\0');
  cr_assert_eq(short_output.guard, 'G');
  cr_assert_eq(mutex_destroy(&mutex), 0);
}

Test(debug_mutex, formatted_rwlock_distinguishes_readers_writer_and_released_state) {
  rwlock_t lock;
  char text[1024];
  cr_assert_eq(rwlock_init(&lock, "formatted-rwlock"), 0);
  cr_assert_eq(rwlock_format_state(NULL, text, sizeof(text)), 0);
  cr_assert_eq(rwlock_format_state(&lock, NULL, sizeof(text)), 0);
  cr_assert_eq(rwlock_format_state(&lock, text, 0), 0);
  cr_assert_eq(rwlock_rdlock(&lock), 0);
  cr_assert_gt(rwlock_format_state(&lock, text, sizeof(text)), 0);
  cr_assert_not_null(strstr(text, "READ_LOCKED=1"));
  cr_assert_not_null(strstr(text, "rdlock="));
  cr_assert_eq(rwlock_rdunlock(&lock), 0);
  cr_assert_eq(rwlock_wrlock(&lock), 0);
  cr_assert_gt(rwlock_format_state(&lock, text, sizeof(text)), 0);
  cr_assert_not_null(strstr(text, "WRITE_LOCKED_BY=thread."));
  cr_assert_not_null(strstr(text, "wrlock="));
  cr_assert_null(strstr(text, "READ_LOCKED="));
  cr_assert_eq(rwlock_wrunlock(&lock), 0);
  cr_assert_gt(rwlock_format_state(&lock, text, sizeof(text)), 0);
  cr_assert_not_null(strstr(text, "unlock="));
  cr_assert_not_null(strstr(text, "rdlock=1 wrlock=1 unlock=2"));
  cr_assert_null(strstr(text, "LOCKED"));
  cr_assert_eq(rwlock_destroy(&lock), 0);
}

Test(debug_mutex, formatted_condition_state_tracks_timeout_signal_and_broadcast) {
  mutex_t mutex;
  cond_t condition;
  char text[1024];
  cr_assert_eq(mutex_init(&mutex, "formatted-condition-lock"), 0);
  cr_assert_eq(cond_init(&condition, "formatted-condition"), 0);
  cr_assert_eq(cond_format_state(NULL, text, sizeof(text)), 0);
  cr_assert_eq(cond_format_state(&condition, NULL, sizeof(text)), 0);
  cr_assert_eq(cond_format_state(&condition, text, 0), 0);
  cr_assert_eq(mutex_lock(&mutex), 0);
  cr_assert_neq(cond_timedwait(&condition, &mutex, 1000000), 0);
  cr_assert_eq(cond_signal(&condition), 0);
  cr_assert_eq(cond_broadcast(&condition), 0);
  cr_assert_gt(cond_format_state(&condition, text, sizeof(text)), 0);
  cr_assert_not_null(strstr(text, "wait="));
  cr_assert_not_null(strstr(text, "signal="));
  cr_assert_not_null(strstr(text, "broadcast="));
  cr_assert_not_null(strstr(text, "wait=1 signal=1 broadcast=1"));
  cr_assert_null(strstr(text, "WAITING="));
  cr_assert_eq(mutex_unlock(&mutex), 0);
  cr_assert_eq(cond_destroy(&condition), 0);
  cr_assert_eq(mutex_destroy(&mutex), 0);
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

Test(debug_mutex, concurrent_stack_snapshots) {
  asciichat_thread_t writers[4];
  atomic_store(&stack_stress_running, true);
  for (int i = 0; i < 4; i++)
    cr_assert_eq(acthread_create(&writers[i], "stack_stress", change_lock_stack, NULL), ASCIICHAT_OK);

  for (int iteration = 0; iteration < 2000; iteration++) {
    mutex_stack_entry_t **stacks = NULL;
    int *counts = NULL;
    int count = 0;
    cr_assert_eq(mutex_stack_get_all_threads(&stacks, &counts, &count), 0);
    for (int i = 0; i < count; i++) {
      cr_assert_geq(counts[i], 0);
      cr_assert_leq(counts[i], 64);
    }
    mutex_stack_free_all_threads(stacks, counts, count);
    mutex_stack_detect_deadlocks();
  }

  atomic_store(&stack_stress_running, false);
  for (int i = 0; i < 4; i++)
    cr_assert_eq(thread_join(&writers[i], NULL), ASCIICHAT_OK);
}

static atomic_t cond_stress_running = {0};

static void *inspect_conditions(void *unused) {
  (void)unused;
  while (atomic_load_bool(&cond_stress_running))
    debug_sync_check_cond_deadlocks();
  return NULL;
}

Test(debug_mutex, concurrent_condition_destruction) {
  // The monitor must not dereference a condition after unregistration frees it.
  NAMED_REGISTER_ATOMIC(&cond_stress_running, "cond_stress_running", NULL);
  atomic_store_bool(&cond_stress_running, true);
  asciichat_thread_t monitor;
  cr_assert_eq(acthread_create(&monitor, "cond_monitor", inspect_conditions, NULL), ASCIICHAT_OK);
  for (int i = 0; i < 5000; ++i) {
    cond_t *cond = SAFE_CALLOC(1, sizeof(*cond), cond_t *);
    cr_assert_eq(cond_init(cond, "condition_lifetime"), 0);
    cr_assert_eq(cond_destroy(cond), 0);
    SAFE_FREE(cond);
  }
  atomic_store_bool(&cond_stress_running, false);
  cr_assert_eq(thread_join(&monitor, NULL), ASCIICHAT_OK);
  NAMED_UNREGISTER(&cond_stress_running);
}
#endif
