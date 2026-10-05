#include <criterion/criterion.h>
#include <stdatomic.h>
#include <ascii-chat/debug/mutex.h>
#include <ascii-chat/platform/thread.h>

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
#endif
