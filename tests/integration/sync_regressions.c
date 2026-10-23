/** Native regressions for diagnostic snapshots and UI callback ownership. */
#include <ascii-chat/common.h>
#include <ascii-chat/debug/coverage.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/debug/mutex.h>
#include <ascii-chat/debug/atomic.h>
#include <ascii-chat/log/named.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/cond.h>
#include <ascii-chat/platform/wakeup.h>
#include <ascii-chat/platform/rwlock.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/colorscheme.h>
#include <ascii-chat/tui/controller.h>
#include <ascii-chat/tui/sync.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/format.h>
#include <ascii-chat/stats/stats.h>
#include <stdatomic.h>
#include <string.h>
#include <errno.h>

static atomic_bool entered, proceed, removed;
static mutex_t test_mutex;
static cond_t test_cond;
static atomic_bool trylock_clean;
static rwlock_t *registry_lock;
static mutex_t *scheme_mutex;
static mutex_t *memory_mutex;
static mutex_t *controller_mutex;
static int objects[600];
static unsigned seen;

#ifndef NDEBUG
static void inspect_atomic(uintptr_t key, void *value) {
  *(uint64_t *)value = atomic_load(&((atomic_t *)key)->impl);
}
#endif

#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr)) {                                                                                                     \
      log_error("Regression failed at line %d: %s", __LINE__, #expr);                                                  \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)

static platform_wakeup_t *test_wakeup;
static atomic_uint wake_generation;
static atomic_bool wake_acknowledged;

static void *publish_wakeups(void *unused) {
  (void)unused;
  for (unsigned i = 1; i <= 10000; ++i) {
    atomic_store(&wake_acknowledged, false);
    atomic_store(&wake_generation, i);
    platform_wakeup_signal(test_wakeup);
    while (!atomic_load(&wake_acknowledged))
      platform_sleep_ns(1000);
  }
  return NULL;
}

static int check_wakeups(void) {
  CHECK(platform_wakeup_create(&test_wakeup) == ASCIICHAT_OK);
  CHECK(!platform_wakeup_wait(test_wakeup, NS_PER_MS_INT));
  for (unsigned i = 0; i < 10000; ++i)
    platform_wakeup_signal(test_wakeup);
  CHECK(platform_wakeup_wait(test_wakeup, NS_PER_MS_INT));
  CHECK(!platform_wakeup_wait(test_wakeup, NS_PER_MS_INT));
  asciichat_thread_t publisher = {0};
  CHECK(acthread_create(&publisher, "wakeup_regression", publish_wakeups, NULL) == ASCIICHAT_OK);
  for (unsigned i = 1; i <= 10000; ++i) {
    CHECK(platform_wakeup_wait(test_wakeup, NS_PER_SEC_INT));
    CHECK(atomic_load(&wake_generation) == i);
    atomic_store(&wake_acknowledged, true);
  }
  CHECK(thread_join(&publisher, NULL) == ASCIICHAT_OK);
  platform_wakeup_destroy(test_wakeup);
  test_wakeup = NULL;
  return 0;
}

static atomic_uint duration_workers_done;
static atomic_bool duration_workers_release;
static void *record_durations(void *data) {
  stats_scope_t *scope = data;
  while (!atomic_load(&duration_workers_release))
    platform_sleep_ns(NS_PER_MS_INT);
  for (unsigned i = 0; i < 10000; ++i)
    stats_duration_record(scope, STATS_DURATION_CAPTURE, 7);
  atomic_fetch_add(&duration_workers_done, 1);
  return NULL;
}

static int check_duration_recording(void) {
  stats_capabilities_t capabilities = {0};
  capabilities.durations[STATS_DURATION_CAPTURE] = true;
  stats_scope_t *scope = NULL;
  CHECK(stats_scope_create(&capabilities, &scope) == ASCIICHAT_OK);
  asciichat_thread_t workers[20];
  atomic_store(&duration_workers_done, 0);
  atomic_store(&duration_workers_release, false);
  for (unsigned i = 0; i < 4; ++i)
    CHECK(acthread_create(&workers[i], "duration_writer", record_durations, scope) == ASCIICHAT_OK);
  atomic_store(&duration_workers_release, true);
  stats_snapshot_t snapshot;
  do {
    CHECK(stats_scope_snapshot(scope, &snapshot) == ASCIICHAT_OK);
    stats_duration_snapshot_t duration = snapshot.durations[STATS_DURATION_CAPTURE];
    CHECK(duration.total_ns == duration.observations * 7);
    if (duration.observations) {
      CHECK(duration.min_ns == 7 && duration.max_ns == 7 && duration.last_ns == 7);
    }
  } while (atomic_load(&duration_workers_done) < 4);
  for (unsigned i = 0; i < 4; ++i)
    CHECK(thread_join(&workers[i], NULL) == ASCIICHAT_OK);
  CHECK(stats_scope_snapshot(scope, &snapshot) == ASCIICHAT_OK);
  CHECK(snapshot.durations[STATS_DURATION_CAPTURE].observations == 40000);
  // More simultaneous producers than shards exercise the coherent fallback.
  atomic_store(&duration_workers_release, false);
  for (unsigned i = 0; i < 20; ++i)
    CHECK(acthread_create(&workers[i], "duration_writer", record_durations, scope) == ASCIICHAT_OK);
  atomic_store(&duration_workers_release, true);
  for (unsigned i = 0; i < 20; ++i)
    CHECK(thread_join(&workers[i], NULL) == ASCIICHAT_OK);
  stats_duration_record(scope, STATS_DURATION_CAPTURE, 0);
  stats_duration_record(scope, STATS_DURATION_CAPTURE, 900);
  CHECK(stats_scope_snapshot(scope, &snapshot) == ASCIICHAT_OK);
  CHECK(snapshot.durations[STATS_DURATION_CAPTURE].observations == 240002);
  CHECK(snapshot.durations[STATS_DURATION_CAPTURE].total_ns == 240000 * 7 + 900);
  CHECK(snapshot.durations[STATS_DURATION_CAPTURE].min_ns == 0);
  CHECK(snapshot.durations[STATS_DURATION_CAPTURE].max_ns == 900);
  CHECK(snapshot.durations[STATS_DURATION_CAPTURE].last_ns == 900);
  stats_scope_destroy(scope);
  return 0;
}

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
  if (strstr(name, "debug_memory."))
    memory_mutex = (mutex_t *)key;
  if (strstr(name, "colorscheme."))
    scheme_mutex = (mutex_t *)key;
  if (strstr(name, "named_registry_lock"))
    registry_lock = (rwlock_t *)key;
}

static void find_controller(uintptr_t key, uint64_t generation, const char *name, const char *type, const char *file,
                            int line, void *data) {
  (void)generation;
  (void)file;
  (void)line;
  (void)data;
  if (strcmp(type, "mutex") == 0 && strstr(name, "ui_controller."))
    controller_mutex = (mutex_t *)key;
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
static mutex_t rate_hot, rate_warm;
static cond_t sort_actions, sort_balanced, sort_wait;
static atomic_t rate_value, total_value;
static atomic_ptr_t total_pointer;
static void *finish_blocked_fixture(void *arg) {
  const char *stop_path = arg;
  while (platform_access(stop_path, PLATFORM_ACCESS_EXISTS) != 0)
    platform_sleep_ns(10 * NS_PER_MS_INT);
  asciichat_coverage_flush();
  _Exit(0);
}
static void *rate_worker(void *unused) {
  (void)unused;
  for (;;) {
    for (unsigned i = 0; i < 40; ++i) {
      mutex_lock(&rate_hot);
      mutex_unlock(&rate_hot);
      if (i % 2 == 0) {
        mutex_lock(&rate_warm);
        mutex_unlock(&rate_warm);
      }
    }
    atomic_fetch_add_u64(&rate_value, 1);
    platform_sleep_ns(10 * NS_PER_MS_INT);
  }
  return NULL;
}
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
  const struct {
    uint64_t ns;
    int decimals;
    const char *text;
  } duration_cases[] = {{0, -1, "0ns"},
                        {100, -1, "100ns"},
                        {3500, -1, "3.5µs"},
                        {1250000, -1, "1.25ms"},
                        {255000000, -1, "255ms"},
                        {2500000000, -1, "2.5s"},
                        {60000000000, -1, "1:00"},
                        {83456000000, -1, "1:23"},
                        {119999600000, -1, "1:59"},
                        {3599999600000, -1, "59:59"},
                        {3600000000000, -1, "1:00:00"},
                        {7530000000000, -1, "2:05:30"},
                        {86399999999999, -1, "23:59:59"},
                        {86400000000000, -1, "1d 0:00"},
                        {3207240000000000, -1, "37d 2:54"},
                        {83456789123, 9, "1:23"},
                        {83456000000, 0, "1:23"}};
  for (size_t i = 0; i < sizeof(duration_cases) / sizeof(duration_cases[0]); ++i) {
    char text[32];
    CHECK(time_pretty(duration_cases[i].ns, duration_cases[i].decimals, text, sizeof(text)) >= 0);
    CHECK(!strcmp(text, duration_cases[i].text));
  }
  const struct {
    uint64_t value;
    const char *text;
  } compact_cases[] = {{0, "0"},
                       {100, "100"},
                       {999, "999"},
                       {1000, "1k"},
                       {1050, "1.1k"},
                       {57400, "57.4k"},
                       {203200, "203.2k"},
                       {1300000, "1.3m"},
                       {124400000, "124.4m"},
                       {UINT64_C(7300000000), "7.3b"},
                       {UINT64_C(800200000000000), "800.2t"},
                       {999949, "999.9k"},
                       {999950, "1m"},
                       {UINT64_C(999950000000), "1t"},
                       {UINT64_MAX, "18.4e"}};
  for (size_t i = 0; i < sizeof(compact_cases) / sizeof(compact_cases[0]); ++i) {
    char text[32];
    format_number_compact(compact_cases[i].value, text, sizeof(text));
    CHECK(!strcmp(text, compact_cases[i].text));
  }
  char short_buffer[3] = {'x', 'x', 'x'};
  format_number_compact(57400, short_buffer, sizeof(short_buffer));
  CHECK(!strcmp(short_buffer, "57"));
  format_number_compact(1, short_buffer, 0);
  CHECK(!strcmp(short_buffer, "57"));
  format_number_compact(1, short_buffer, 1);
  CHECK(short_buffer[0] == '\0');
  format_number_compact(1, NULL, 1);
  CHECK(asciichat_shared_init(argv[2], false, false) == ASCIICHAT_OK);
#ifndef NDEBUG
  debug_atomic_init();
#endif
  char *options[] = {"sync-regressions", "--no-check-update", "mirror", NULL};
  CHECK(options_init(3, options) == ASCIICHAT_OK);
  if (!strcmp(argv[1], "sync"))
    CHECK(check_duration_recording() == 0);
  if (!strcmp(argv[1], "sync"))
    CHECK(check_wakeups() == 0);
#ifndef NDEBUG
  if (!strcmp(argv[1], "deadlock") || !strcmp(argv[1], "stale")) {
    static char stop_path[1024];
    SAFE_SNPRINTF(stop_path, sizeof(stop_path), "%s.stop", argv[2]);
    asciichat_thread_t finisher;
    CHECK(acthread_create(&finisher, "fixture_finish", finish_blocked_fixture, stop_path) == ASCIICHAT_OK);
    for (unsigned i = 0; i < 600; ++i) {
      atomic_store_u64(&sync_values[i], i);
      NAMED_REGISTER_ATOMIC(&sync_values[i], "sync_fixture_value", NULL);
    }
    // Re-registering with an owner-qualified name must not add another object
    // to the inspector's inventory.
    NAMED_REGISTER_ATOMIC(&sync_values[0], "sync_fixture_value", NULL);
    CHECK(named_registry_for_each(count_entry, NULL, &(bool){false}) == ASCIICHAT_OK);
    CHECK(ui_controller_present(UI_SCREEN_STATUS, STDOUT_FILENO, (terminal_size_t){20, 5}, "SYNC PREPARING", 14) ==
          ASCIICHAT_OK);
    platform_sleep_ns(NS_PER_SEC_INT);
    if (!strcmp(argv[1], "stale")) {
      CHECK(registry_lock != NULL);
      // A stale sample must first have been collected while the overlay is
      // visible; hidden sessions intentionally do not sample the registry.
      ui_sync_handle_key(KEY_0);
      platform_sleep_ns(NS_PER_SEC_INT);
      ui_sync_handle_key(KEY_0);
      CHECK(ui_controller_present(UI_SCREEN_STATUS, STDOUT_FILENO, (terminal_size_t){20, 5}, "SYNC FIXTURE READY",
                                  18) == ASCIICHAT_OK);
      rwlock_wrlock(registry_lock);
      for (;;)
        platform_sleep_ns(NS_PER_SEC_INT);
    }
    CHECK(ui_controller_present(UI_SCREEN_STATUS, STDOUT_FILENO, (terminal_size_t){20, 5}, "SYNC FIXTURE READY", 18) ==
          ASCIICHAT_OK);
    CHECK(mutex_init(&rate_hot, "zzz_rate_hot") == ASCIICHAT_OK);
    CHECK(mutex_init(&rate_warm, "aaa_rate_warm") == ASCIICHAT_OK);
    CHECK(cond_init(&sort_actions, "sort_actions") == ASCIICHAT_OK);
    CHECK(cond_init(&sort_balanced, "sort_balanced") == ASCIICHAT_OK);
    CHECK(cond_init(&sort_wait, "sort_wait") == ASCIICHAT_OK);
    // A balanced history must outrank either one-dimensional extreme.
    sort_actions.signal_count = UINT64_C(1000000000000);
    sort_balanced.signal_count = UINT64_C(750000000000);
    sort_balanced.total_wait_ns = UINT64_C(2700000000000);
    sort_wait.total_wait_ns = UINT64_C(3600000000000);
    NAMED_REGISTER_ATOMIC(&rate_value, "aaa_rate_counter", NULL);
    NAMED_REGISTER_ATOMIC(&total_value, "aaa_total_uses", NULL);
    for (unsigned i = 0; i < 3; ++i)
      atomic_store_u64(&total_value, 7);
    (void)atomic_load_u64(&total_value);
    (void)atomic_load_u64(&total_value);
    (void)atomic_fetch_add_u64(&total_value, 0);
    uint64_t old = 8;
    CHECK(!atomic_cas_u64(&total_value, &old, 7));
    CHECK(atomic_cas_u64(&total_value, &old, 7));
    (void)atomic_exchange_u64(&total_value, 7);
    NAMED_REGISTER_ATOMIC_PTR(&total_pointer, "aaa_pointer_uses", NULL);
    atomic_ptr_store(&total_pointer, NULL);
    atomic_ptr_store(&total_pointer, NULL);
    (void)atomic_ptr_load(&total_pointer);
    (void)atomic_ptr_load(&total_pointer);
    void *old_pointer = &objects[0];
    CHECK(!atomic_ptr_cas(&total_pointer, &old_pointer, NULL));
    CHECK(atomic_ptr_cas(&total_pointer, &old_pointer, NULL));
    (void)atomic_ptr_exchange(&total_pointer, NULL);
    asciichat_thread_t rates;
    CHECK(acthread_create(&rates, "rate_worker", rate_worker, NULL) == ASCIICHAT_OK);
    CHECK(mutex_init(&cycle_a, "cycle_a") == 0);
    CHECK(mutex_init(&cycle_b, "cycle_b") == 0);
    mutex_lock(&cycle_a);
    asciichat_thread_t worker;
    CHECK(acthread_create(&worker, "cycle_worker", cycle_worker, NULL) == ASCIICHAT_OK);
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
#ifndef NDEBUG
    bool completed = false;
    size_t required = 0;
    CHECK(named_registry_try_snapshot(SIZE_MAX, &required, &completed, find_controller, NULL) == ASCIICHAT_OK &&
          completed);
    CHECK(controller_mutex);
    uint64_t query_locks = controller_mutex->lock_count;
#endif
    for (unsigned i = 0; i < 1000; ++i) {
      CHECK(ui_controller_state().screen == UI_SCREEN_HELP);
      CHECK(ui_controller_size().cols > 0);
    }
#ifndef NDEBUG
    CHECK(controller_mutex->lock_count == query_locks);
#endif
    CHECK(!ui_controller_state().covered);
    CHECK(ui_controller_submit(UI_SCREEN_HELP, STDOUT_FILENO, (terminal_size_t){200, 200}, slow_render, snapshot,
                               sizeof(snapshot)) == ASCIICHAT_OK);
    CHECK(ui_controller_state().covered);
    ui_controller_select_interactive(UI_SCREEN_SYNC);
    CHECK(ui_controller_state().screen == -1);
    ui_controller_select_interactive(UI_SCREEN_HELP);
    uint64_t start = time_get_ns();
    CHECK(ui_controller_state().screen == UI_SCREEN_HELP);
    CHECK(ui_controller_present(UI_SCREEN_HELP, STDOUT_FILENO, (terminal_size_t){20, 5}, "replacement", 11) ==
          ASCIICHAT_OK);
    CHECK(time_get_ns() - start < 500 * NS_PER_MS_INT);
    CHECK(!ui_controller_state().covered);
    asciichat_thread_t remover;
    CHECK(acthread_create(&remover, "remove_screen", remove_screen, NULL) == ASCIICHAT_OK);
    platform_sleep_ns(50 * NS_PER_MS_INT);
    CHECK(!atomic_load(&removed));
    atomic_store(&proceed, true);
    CHECK(thread_join(&remover, NULL) == ASCIICHAT_OK);
    CHECK(atomic_load(&removed));
    atomic_store(&entered, false);
    CHECK(ui_controller_submit(UI_SCREEN_HELP, STDOUT_FILENO, (terminal_size_t){20, 5}, self_remove, snapshot,
                               sizeof(snapshot)) == ASCIICHAT_OK);
    CHECK(await_flag(&entered));
    ui_controller_finish(STDOUT_FILENO, "\nUI FINISHED\n", 13);
    ui_controller_shutdown();

    // Hold an old media callback in flight while replacing the current frame.
    // Warm all three slots, then verify publishing fixed-size frames allocates nothing.
    atomic_store(&entered, false);
    atomic_store(&proceed, false);
    CHECK(ui_controller_submit(UI_SCREEN_MEDIA, STDOUT_FILENO, (terminal_size_t){20, 5}, slow_render, snapshot,
                               sizeof(snapshot)) == ASCIICHAT_OK);
    CHECK(await_flag(&entered));
    const char media[] = "frame stays immutable";
    for (unsigned i = 0; i < 8; ++i)
      CHECK(ui_controller_present(UI_SCREEN_MEDIA, STDOUT_FILENO, (terminal_size_t){20, 5}, media, sizeof(media)) ==
            ASCIICHAT_OK);
#ifndef NDEBUG
    bool found = false;
    CHECK(named_registry_for_each(count_entry, NULL, &found) == ASCIICHAT_OK && found);
    CHECK(memory_mutex);
    uint64_t media_memory_locks = memory_mutex->lock_count;
#endif
    for (unsigned i = 0; i < 1000; ++i)
      CHECK(ui_controller_present(UI_SCREEN_MEDIA, STDOUT_FILENO, (terminal_size_t){20, 5}, media, sizeof(media)) ==
            ASCIICHAT_OK);
#ifndef NDEBUG
    CHECK(memory_mutex->lock_count == media_memory_locks);
#endif
    atomic_store(&proceed, true);
    ui_controller_remove(UI_SCREEN_MEDIA);
    ui_controller_shutdown();
  } else {
#ifndef NDEBUG
    for (unsigned i = 0; i < 600; ++i)
      CHECK(named_register((uintptr_t)&objects[i], "regression_entry", "test", "%p", __FILE__, __LINE__, __func__, 0) !=
            NULL);
    bool completed = false;
    CHECK(named_registry_for_each(count_entry, NULL, &completed) == ASCIICHAT_OK && completed);
    CHECK(seen == 600);
    CHECK(registry_lock != NULL);
#endif
    CHECK(colorscheme_init() == ASCIICHAT_OK);
    color_scheme_t scheme = *colorscheme_get_active_scheme();
    colorscheme_palette_t palette;
    colorscheme_current_palette(&palette, TERM_COLOR_TRUECOLOR);
#ifndef NDEBUG
    CHECK(named_registry_for_each(count_entry, NULL, &completed) == ASCIICHAT_OK && completed);
    CHECK(scheme_mutex);
#endif

#ifndef NDEBUG
    seen = 600;
    uint64_t color_locks = scheme_mutex->lock_count;
#endif
    for (unsigned i = 0; i < 1000; ++i) {
      colorscheme_current_palette(&palette, TERM_COLOR_TRUECOLOR);
      CHECK(colorscheme_get_active_scheme()->log_colors_dark[0].r == scheme.log_colors_dark[0].r);
    }
#ifndef NDEBUG
    CHECK(scheme_mutex->lock_count == color_locks);
#endif
    scheme.log_colors_dark[0] = (rgb_pixel_t){1, 2, 3};
    scheme.log_colors_light[0] = (rgb_pixel_t){1, 2, 3};
    colorscheme_apply(&scheme);
    colorscheme_current_palette(&palette, TERM_COLOR_TRUECOLOR);
    CHECK(strcmp(palette.codes[0], "\033[38;2;1;2;3m") == 0);
    colorscheme_current_palette(&palette, TERM_COLOR_NONE);
    CHECK(palette.codes[0][0] == '\0');
#ifndef NDEBUG
    CHECK(memory_mutex);
    uint64_t memory_locks = memory_mutex->lock_count;
    for (unsigned i = 0; i < 100; ++i)
      mutex_stack_detect_deadlocks();
    CHECK(memory_mutex->lock_count == memory_locks);
    uint64_t scans_before = registry_lock->rdlock_count;
    debug_sync_check_cond_deadlocks();
    CHECK(registry_lock->rdlock_count == scans_before + 2);
    scans_before = registry_lock->rdlock_count;
    for (unsigned i = 0; i < 100; ++i)
      debug_sync_check_cond_deadlocks();
    CHECK(registry_lock->rdlock_count == scans_before);
#endif
#ifndef NDEBUG
    asciichat_thread_t holder;
    CHECK(acthread_create(&holder, "registry_holder", hold_registry, NULL) == ASCIICHAT_OK);
    CHECK(await_flag(&entered));
    uint64_t start = time_get_ns();
    CHECK(named_registry_for_each(count_entry, NULL, &completed) == ASCIICHAT_OK && !completed);
    CHECK(!named_registry_has_type("test", 4));
    uint64_t elapsed = time_get_ns() - start;
    atomic_store(&proceed, true);
    CHECK(thread_join(&holder, NULL) == ASCIICHAT_OK);
    CHECK(elapsed < 500 * NS_PER_MS_INT);
    CHECK(seen == 600);
    CLEAR_ERRNO();
    for (unsigned i = 0; i < 600; ++i)
      NAMED_UNREGISTER(&objects[i]);
    CHECK(mutex_init(&test_mutex, "regression_mutex") == 0);
    mutex_lock(&test_mutex);
#ifdef _WIN32
    mutex_lock(&test_mutex);
    mutex_unlock(&test_mutex);
    CHECK(test_mutex.currently_held_by_key == (uintptr_t)thread_current_id());
#endif
    atomic_store(&entered, false);
    CHECK(acthread_create(&holder, "mutex_waiter", wait_for_mutex, NULL) == ASCIICHAT_OK);
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
    CHECK(thread_join(&holder, NULL) == ASCIICHAT_OK);
    CHECK(pending);
    CHECK(atomic_load(&trylock_clean));
    CHECK(cond_init(&test_cond, "regression_condition") == 0);
    atomic_store(&entered, false);
    CHECK(acthread_create(&holder, "condition_waiter", wait_for_condition, NULL) == ASCIICHAT_OK);
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
    CHECK(thread_join(&holder, NULL) == ASCIICHAT_OK);
    CHECK(atomic_load_u64(&test_cond.waiting_count) == 0);
    mutex_lock(&test_mutex);
    CHECK(cond_timedwait(&test_cond, &test_mutex, NS_PER_MS_INT) == ETIMEDOUT);
    CHECK(atomic_load_u64(&test_cond.waiting_count) == 0);
    mutex_unlock(&test_mutex);
    // Two real waiters share one wall-clock interval, not two thread-seconds.
    uint64_t completed_wait = test_cond.total_wait_ns;
    asciichat_thread_t overlapping[2];
    for (unsigned i = 0; i < 2; ++i)
      CHECK(acthread_create(&overlapping[i], "overlap_waiter", wait_for_condition, NULL) == ASCIICHAT_OK);
    deadline = time_get_ns() + NS_PER_SEC_INT;
    while (atomic_load_u64(&test_cond.waiting_count) != 2 && time_get_ns() < deadline)
      platform_sleep_ms(1);
    mutex_lock(&test_mutex);
    CHECK(atomic_load_u64(&test_cond.waiting_count) == 2);
    uint64_t wait_started = test_cond.wait_started_ns, total_wait = 0;
    CHECK(wait_started);
    uint64_t query_now = time_get_ns();
    CHECK(cond_total_wait_ns(&test_cond, query_now, &total_wait));
    CHECK(total_wait == completed_wait + query_now - wait_started);
    CHECK(cond_total_wait_ns(&test_cond, query_now + NS_PER_SEC_INT, &total_wait));
    CHECK(total_wait == completed_wait + query_now + NS_PER_SEC_INT - wait_started);
    cond_broadcast(&test_cond);
    mutex_unlock(&test_mutex);
    for (unsigned i = 0; i < 2; ++i)
      CHECK(thread_join(&overlapping[i], NULL) == ASCIICHAT_OK);
    CHECK(test_cond.wait_started_ns == 0);
    completed_wait = test_cond.total_wait_ns;
    CHECK(cond_total_wait_ns(&test_cond, time_get_ns() + NS_PER_SEC_INT, &total_wait));
    CHECK(total_wait == completed_wait);
    atomic_fetch_add(&test_cond.wait_revision, 1);
    CHECK(!cond_total_wait_ns(&test_cond, time_get_ns(), &total_wait));
    atomic_fetch_add(&test_cond.wait_revision, 1);
    cond_destroy(&test_cond);
    mutex_destroy(&test_mutex);
    ringbuffer_t *queue = ringbuffer_create(sizeof(int), 8);
    CHECK(queue != NULL);
    uintptr_t queue_keys[] = {(uintptr_t)&queue->head, (uintptr_t)&queue->tail, (uintptr_t)&queue->size};
    uint64_t observed;
    for (size_t i = 0; i < 3; ++i)
      CHECK(named_registry_read(queue_keys[i], "atomic", inspect_atomic, &observed));
    ringbuffer_destroy(queue);
    for (size_t i = 0; i < 3; ++i)
      CHECK(!named_registry_read(queue_keys[i], "atomic", inspect_atomic, &observed));
    audio_ring_buffer_t *audio = audio_ring_buffer_create();
    CHECK(audio != NULL);
    uintptr_t audio_keys[] = {(uintptr_t)&audio->write_index,          (uintptr_t)&audio->read_index,
                              (uintptr_t)&audio->discard_pending,      (uintptr_t)&audio->discard_to_index,
                              (uintptr_t)&audio->jitter_buffer_filled, (uintptr_t)&audio->crossfade_samples_remaining,
                              (uintptr_t)&audio->crossfade_fade_in,    (uintptr_t)&audio->underrun_count};
    for (size_t i = 0; i < 8; ++i)
      CHECK(named_registry_read(audio_keys[i], "atomic", inspect_atomic, &observed));
    audio_ring_buffer_destroy(audio);
    for (size_t i = 0; i < 8; ++i)
      CHECK(!named_registry_read(audio_keys[i], "atomic", inspect_atomic, &observed));
    int type_fixture;
    named_register((uintptr_t)&type_fixture, "type_fixture", "regression_type", "0x%tx", __FILE__, __LINE__, __func__,
                   0);
    CHECK(named_registry_has_type("regression_type_suffix", strlen("regression_type")));
    CHECK(!named_registry_has_type("regression_typ", strlen("regression_typ")));
    CHECK(!named_registry_has_type(NULL, 1));
    CHECK(!named_registry_has_type("", 0));
    // Warm metadata reads and unknown addresses must not touch the registry.
    uint64_t lookups_before = registry_lock->rdlock_count;
    CHECK(!named_registry_has_type("unregistered_type", strlen("unregistered_type")));
    named_metadata_t metadata;
    for (unsigned i = 0; i < 1000; ++i) {
      CHECK(named_metadata_copy((uintptr_t)&type_fixture, &metadata));
      CHECK(strstr(metadata.name, "type_fixture") != NULL);
      CHECK(!strcmp(metadata.type, "regression_type"));
      CHECK(!named_metadata_copy((uintptr_t)&objects[i % 600], &metadata));
    }
    CHECK(registry_lock->rdlock_count == lookups_before);
    char formatted[512];
    char message[128];
    safe_snprintf(message, sizeof(message), "fixture=0x%tx", (ptrdiff_t)&type_fixture);
    CHECK(log_named_format_message(message, formatted, sizeof(formatted)) > 0);
    CHECK(strstr(formatted, "regression_type/type_fixture") != NULL);
    named_unregister((uintptr_t)&type_fixture);
    CHECK(!named_registry_has_type("regression_type", strlen("regression_type")));
    CHECK(!named_metadata_copy((uintptr_t)&type_fixture, &metadata));
    // Previously copied strings survive removal, and address reuse invalidates
    // both positive and negative cache entries.
    CHECK(!strcmp(metadata.type, "regression_type"));
    named_register((uintptr_t)&type_fixture, "replacement", "replacement_type", "0x%tx", __FILE__, __LINE__, __func__,
                   0);
    CHECK(named_metadata_copy((uintptr_t)&type_fixture, &metadata));
    CHECK(strstr(metadata.name, "replacement") != NULL);
    CHECK(!strcmp(metadata.type, "replacement_type"));
    named_register((uintptr_t)&type_fixture, "latest_alias", "replacement_type", "0x%tx", __FILE__, __LINE__, __func__,
                   0);
    CHECK(named_metadata_copy((uintptr_t)&type_fixture, &metadata));
    CHECK(strstr(metadata.name, "latest_alias") != NULL);
    named_unregister((uintptr_t)&type_fixture);
    CHECK(!named_metadata_copy((uintptr_t)&type_fixture, &metadata));
#endif
    log_info("PASS registry capacity, contention, and mutex tracking");
  }
  asciichat_shared_destroy();
  if (!strcmp(argv[1], "ui"))
    ui_controller_write(STDOUT_FILENO, "\nPASS UI OWNERSHIP\n", 19);
  return 0;
}
