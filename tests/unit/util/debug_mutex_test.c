#include <criterion/criterion.h>
#include <stdatomic.h>
#include <ascii-chat/debug/mutex.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/platform/cond.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/common.h>
#include <ascii-chat/atomic.h>

#ifndef NDEBUG
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
    cr_assert_eq(asciichat_thread_create(&writers[i], "stack_stress", change_lock_stack, NULL), ASCIICHAT_OK);

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
    cr_assert_eq(asciichat_thread_join(&writers[i], NULL), ASCIICHAT_OK);
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
  cr_assert_eq(asciichat_thread_create(&monitor, "cond_monitor", inspect_conditions, NULL), ASCIICHAT_OK);
  for (int i = 0; i < 5000; ++i) {
    cond_t *cond = SAFE_CALLOC(1, sizeof(*cond), cond_t *);
    cr_assert_eq(cond_init(cond, "condition_lifetime"), 0);
    cr_assert_eq(cond_destroy(cond), 0);
    SAFE_FREE(cond);
  }
  atomic_store_bool(&cond_stress_running, false);
  cr_assert_eq(asciichat_thread_join(&monitor, NULL), ASCIICHAT_OK);
  NAMED_UNREGISTER(&cond_stress_running);
}
#endif
