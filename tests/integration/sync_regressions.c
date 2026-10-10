/** Native regressions for diagnostic snapshots and UI callback ownership. */
#include <ascii-chat/common.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/debug/mutex.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/cond.h>
#include <ascii-chat/platform/rwlock.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/ui/controller.h>
#include <ascii-chat/ui/sync.h>
#include <ascii-chat/util/time.h>
#include <stdatomic.h>
#include <string.h>
#include <errno.h>

static atomic_bool entered, proceed, removed;
static mutex_t test_mutex;
static cond_t test_cond;
static atomic_bool trylock_clean;
static rwlock_t *registry_lock;
static int objects[600];
static unsigned seen;

#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr)) {                                                                                                     \
      log_error("Regression failed at line %d: %s", __LINE__, #expr);                                                  \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)

static bool await_flag(atomic_bool *flag) {
  uint64_t deadline = time_get_ns() + 3 * NS_PER_SEC_INT;
  while (!atomic_load(flag) && time_get_ns() < deadline)
    platform_sleep_ns(NS_PER_MS_INT);
  return atomic_load(flag);
}

static void count_entry(uintptr_t key, const char *name, void *unused) {
  (void)unused;
  if (key >= (uintptr_t)objects && key < (uintptr_t)(objects + 600))
    ++seen;
  if (strstr(name, "named_registry_lock"))
    registry_lock = (rwlock_t *)key;
}

static void *hold_registry(void *unused) {
  (void)unused;
  rwlock_wrlock(registry_lock);
  atomic_store(&entered, true);
  // Bounded even when the operation under test mistakenly blocks.
  uint64_t deadline = time_get_ns() + NS_PER_SEC_INT;
  while (!atomic_load(&proceed) && time_get_ns() < deadline)
    platform_sleep_ns(NS_PER_MS_INT);
  rwlock_wrunlock(registry_lock);
  return NULL;
}

static void *wait_for_mutex(void *unused) {
  (void)unused;
  bool failed = mutex_trylock(&test_mutex) != 0;
  mutex_stack_entry_t entries[64];
  int depth = mutex_stack_get_current(entries, 64);
  bool tracked = false;
  for (int i = 0; i < depth && i < 64; ++i)
    tracked |= entries[i].mutex_key == (uintptr_t)&test_mutex;
  atomic_store(&trylock_clean, failed && !tracked);
  atomic_store(&entered, true);
  mutex_lock(&test_mutex);
  mutex_unlock(&test_mutex);
  return NULL;
}

static void *wait_for_condition(void *unused) {
  (void)unused;
  mutex_lock(&test_mutex);
  atomic_store(&entered, true);
  cond_timedwait(&test_cond, &test_mutex, NS_PER_SEC_INT);
  mutex_unlock(&test_mutex);
  return NULL;
}

static void slow_render(terminal_size_t size, const void *data) {
  (void)size;
  atomic_store(&entered, true);
  (void)await_flag(&proceed);
  // Replacing/removing the slot must not free these bytes in flight.
  if (strcmp(data, "retained snapshot") != 0)
    log_fatal("In-flight UI snapshot changed");
  ui_controller_state();
  ui_controller_write(STDOUT_FILENO, "CALLBACK FINISHED", 17);
}

static void *remove_screen(void *unused) {
  (void)unused;
  ui_controller_remove(UI_SCREEN_HELP);
  atomic_store(&removed, true);
  return NULL;
}

static void self_remove(terminal_size_t size, const void *data) {
  (void)size;
  ui_controller_remove(UI_SCREEN_HELP);
  ui_controller_write(STDOUT_FILENO, data, strlen(data));
  atomic_store(&entered, true);
}

#ifndef NDEBUG
static mutex_t cycle_a, cycle_b;
static atomic_t sync_values[600];
static void *cycle_worker(void *unused) {
  (void)unused;
  mutex_lock(&cycle_b);
  atomic_store(&entered, true);
  mutex_lock(&cycle_a);
  return NULL;
}
#endif

int main(int argc, char **argv) {
  CHECK(argc == 3);
  CHECK(asciichat_shared_init(argv[2], false, false) == ASCIICHAT_OK);
  char *options[] = {"sync-regressions", "--no-check-update", "mirror", NULL};
  CHECK(options_init(3, options) == ASCIICHAT_OK);
#ifndef NDEBUG
  if (!strcmp(argv[1], "deadlock") || !strcmp(argv[1], "stale")) {
    for (unsigned i = 0; i < 600; ++i) {
      atomic_store_u64(&sync_values[i], i);
      NAMED_REGISTER_ATOMIC(&sync_values[i], "sync_fixture_value", NULL);
    }
    CHECK(named_registry_for_each(count_entry, NULL, &(bool){false}) == ASCIICHAT_OK);
    CHECK(ui_controller_present(UI_SCREEN_STATUS, STDOUT_FILENO, (terminal_size_t){20, 5}, "SYNC FIXTURE READY", 18) ==
          ASCIICHAT_OK);
    platform_sleep_ns(NS_PER_SEC_INT);
    if (!strcmp(argv[1], "stale")) {
      CHECK(registry_lock != NULL);
      rwlock_wrlock(registry_lock);
      for (;;)
        platform_sleep_ns(NS_PER_SEC_INT);
    }
    CHECK(mutex_init(&cycle_a, "cycle_a") == 0);
    CHECK(mutex_init(&cycle_b, "cycle_b") == 0);
    mutex_lock(&cycle_a);
    asciichat_thread_t worker;
    CHECK(asciichat_thread_create(&worker, "cycle_worker", cycle_worker, NULL) == ASCIICHAT_OK);
    CHECK(await_flag(&entered));
    // Intentional main/worker deadlock: the PTY test terminates this fixture.
    mutex_lock(&cycle_b);
    return 1;
  }
  atomic_t value = {0};
  atomic_store_u64(&value, 7);
  atomic_store_u64(&value, 7);
  atomic_fetch_add_u64(&value, 0);
  uint64_t expected_value = 8;
  CHECK(!atomic_cas_u64(&value, &expected_value, 9));
  CHECK(atomic_cas_u64(&value, &expected_value, 7));
  CHECK(value.change_count == 1);
  atomic_fetch_add_u64(&value, 1);
  CHECK(value.change_count == 2);
#endif
  if (!strcmp(argv[1], "ui")) {
    CHECK(platform_isatty(STDOUT_FILENO));
    const char snapshot[] = "retained snapshot";
    CHECK(ui_controller_submit(UI_SCREEN_HELP, STDOUT_FILENO, (terminal_size_t){20, 5}, slow_render, snapshot,
                               sizeof(snapshot)) == ASCIICHAT_OK);
    CHECK(await_flag(&entered));
    uint64_t start = time_get_ns();
    CHECK(ui_controller_state().screen == UI_SCREEN_HELP);
    CHECK(ui_controller_present(UI_SCREEN_HELP, STDOUT_FILENO, (terminal_size_t){20, 5}, "replacement", 11) ==
          ASCIICHAT_OK);
    CHECK(time_get_ns() - start < 500 * NS_PER_MS_INT);
    asciichat_thread_t remover;
    CHECK(asciichat_thread_create(&remover, "remove_screen", remove_screen, NULL) == ASCIICHAT_OK);
    platform_sleep_ns(50 * NS_PER_MS_INT);
    CHECK(!atomic_load(&removed));
    atomic_store(&proceed, true);
    CHECK(asciichat_thread_join(&remover, NULL) == ASCIICHAT_OK);
    CHECK(atomic_load(&removed));
    atomic_store(&entered, false);
    CHECK(ui_controller_submit(UI_SCREEN_HELP, STDOUT_FILENO, (terminal_size_t){20, 5}, self_remove, snapshot,
                               sizeof(snapshot)) == ASCIICHAT_OK);
    CHECK(await_flag(&entered));
    ui_controller_finish(STDOUT_FILENO, "\nUI FINISHED\n", 13);
    ui_controller_shutdown();
  } else {
    for (unsigned i = 0; i < 600; ++i)
      CHECK(named_register((uintptr_t)&objects[i], "regression_entry", "test", "%p", __FILE__, __LINE__, __func__, 0) !=
            NULL);
    bool completed = false;
    CHECK(named_registry_for_each(count_entry, NULL, &completed) == ASCIICHAT_OK && completed);
    CHECK(seen == 600);
    CHECK(registry_lock != NULL);
    asciichat_thread_t holder;
    CHECK(asciichat_thread_create(&holder, "registry_holder", hold_registry, NULL) == ASCIICHAT_OK);
    CHECK(await_flag(&entered));
    uint64_t start = time_get_ns();
    CHECK(named_registry_for_each(count_entry, NULL, &completed) == ASCIICHAT_OK && !completed);
    uint64_t elapsed = time_get_ns() - start;
    atomic_store(&proceed, true);
    CHECK(asciichat_thread_join(&holder, NULL) == ASCIICHAT_OK);
    CHECK(elapsed < 500 * NS_PER_MS_INT);
    CHECK(seen == 600);
    CLEAR_ERRNO();
    for (unsigned i = 0; i < 600; ++i)
      NAMED_UNREGISTER(&objects[i]);
#ifndef NDEBUG
    CHECK(mutex_init(&test_mutex, "regression_mutex") == 0);
    mutex_lock(&test_mutex);
#ifdef _WIN32
    mutex_lock(&test_mutex);
    mutex_unlock(&test_mutex);
    CHECK(test_mutex.currently_held_by_key != 0);
#endif
    atomic_store(&entered, false);
    CHECK(asciichat_thread_create(&holder, "mutex_waiter", wait_for_mutex, NULL) == ASCIICHAT_OK);
    CHECK(await_flag(&entered));
    bool pending = false;
    uint64_t deadline = time_get_ns() + NS_PER_SEC_INT;
    while (!pending && time_get_ns() < deadline) {
      mutex_stack_entry_t **stacks = NULL;
      int *counts = NULL, count = 0;
      CHECK(mutex_stack_get_all_threads(&stacks, &counts, &count) == 0);
      for (int i = 0; i < count; ++i)
        for (int j = 0; j < counts[i]; ++j)
          pending |=
              stacks[i][j].mutex_key == (uintptr_t)&test_mutex && stacks[i][j].state == MUTEX_STACK_STATE_PENDING;
      mutex_stack_free_all_threads(stacks, counts, count);
      platform_sleep_ns(NS_PER_MS_INT);
    }
    mutex_unlock(&test_mutex);
    CHECK(asciichat_thread_join(&holder, NULL) == ASCIICHAT_OK);
    CHECK(pending);
    CHECK(atomic_load(&trylock_clean));
    CHECK(cond_init(&test_cond, "regression_condition") == 0);
    atomic_store(&entered, false);
    CHECK(asciichat_thread_create(&holder, "condition_waiter", wait_for_condition, NULL) == ASCIICHAT_OK);
    CHECK(await_flag(&entered));
    mutex_lock(&test_mutex);
    mutex_stack_entry_t **stacks = NULL;
    int *counts = NULL, count = 0, holders = 0;
    CHECK(mutex_stack_get_all_threads(&stacks, &counts, &count) == 0);
    for (int i = 0; i < count; ++i)
      for (int j = 0; j < counts[i]; ++j)
        holders += stacks[i][j].mutex_key == (uintptr_t)&test_mutex;
    mutex_stack_free_all_threads(stacks, counts, count);
    CHECK(holders == 1);
    CHECK(atomic_load_u64(&test_cond.waiting_count) == 1);
    cond_signal(&test_cond);
    mutex_unlock(&test_mutex);
    CHECK(asciichat_thread_join(&holder, NULL) == ASCIICHAT_OK);
    CHECK(atomic_load_u64(&test_cond.waiting_count) == 0);
    mutex_lock(&test_mutex);
    CHECK(cond_timedwait(&test_cond, &test_mutex, NS_PER_MS_INT) == ETIMEDOUT);
    CHECK(atomic_load_u64(&test_cond.waiting_count) == 0);
    mutex_unlock(&test_mutex);
    cond_destroy(&test_cond);
    mutex_destroy(&test_mutex);
#endif
    log_info("PASS registry capacity, contention, and mutex tracking");
  }
  asciichat_shared_destroy();
  if (!strcmp(argv[1], "ui"))
    ui_controller_write(STDOUT_FILENO, "\nPASS UI OWNERSHIP\n", 19);
  return 0;
}
