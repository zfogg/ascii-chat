/** Force competing first callers through the production splash initialization. */
#include <ascii-chat/common.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/util/time.h>
#include <stdatomic.h>

static int observed_mutex_init(mutex_t *mutex, const char *name);
static int observed_mutex_lock(mutex_t *mutex);
static void observed_sleep_ms(unsigned int milliseconds);

// Pause initialization without invoking an uninitialized native lock in the test.
#define mutex_init observed_mutex_init
#undef mutex_lock
#define mutex_lock observed_mutex_lock
#define platform_sleep_ms observed_sleep_ms
#include "../../../lib/tui/splash.c"
#undef mutex_init
#undef mutex_lock
#undef platform_sleep_ms

static mutex_t *target_mutex;
static lifecycle_t *target_lifecycle;
static _Atomic bool initializing, release_initializer, waited, premature_lock;

static int observed_mutex_init(mutex_t *mutex, const char *name) {
  if (mutex == target_mutex) {
    atomic_store(&initializing, true);
    while (!atomic_load(&release_initializer))
      platform_sleep_ms(1);
  }
  return mutex_init(mutex, name);
}

static int observed_mutex_lock(mutex_t *mutex) {
  if (mutex == target_mutex && !lifecycle_is_initialized(target_lifecycle)) {
    atomic_store(&premature_lock, true);
    // Record the violation, then wait so the probe itself never enters an
    // uninitialized Windows CRITICAL_SECTION or races its native initialization.
    while (!lifecycle_is_initialized(target_lifecycle))
      platform_sleep_ms(1);
  }
  return mutex_lock_impl(mutex);
}

static void observed_sleep_ms(unsigned int milliseconds) {
  if (atomic_load(&initializing) && !atomic_load(&release_initializer))
    atomic_store(&waited, true);
  platform_sleep_ms(milliseconds);
}

static bool await_flag(_Atomic bool *flag) {
  uint64_t deadline = time_get_ns() + 5 * NS_PER_SEC_INT;
  while (!atomic_load(flag) && time_get_ns() < deadline)
    platform_sleep_ms(1);
  return atomic_load(flag);
}

static void *set_splash_state(void *data) {
  if (data)
    splash_clear_discovery_session();
  else
    splash_set_update_notification("update available");
  return NULL;
}

static bool competing_initializers(bool discovery) {
  target_mutex = discovery ? &g_discovery_splash_mutex : &g_update_notification_mutex;
  target_lifecycle = discovery ? &g_discovery_splash_lifecycle : &g_update_notification_lifecycle;
  atomic_store(&initializing, false);
  atomic_store(&release_initializer, false);
  atomic_store(&waited, false);
  atomic_store(&premature_lock, false);
  asciichat_thread_t first = {0}, second = {0};
  void *data = discovery ? (void *)&discovery : NULL;
  if (acthread_create(&first, "splash_init_first", set_splash_state, data) != ASCIICHAT_OK)
    return false;
  bool started = await_flag(&initializing);
  bool contender = started && acthread_create(&second, "splash_init_second", set_splash_state, data) == ASCIICHAT_OK;
  uint64_t deadline = time_get_ns() + 5 * NS_PER_SEC_INT;
  while (contender && !atomic_load(&waited) && !atomic_load(&premature_lock) && time_get_ns() < deadline)
    platform_sleep_ms(1);
  bool serialized = atomic_load(&waited) && !atomic_load(&premature_lock);
  atomic_store(&release_initializer, true);
  bool joined = thread_join(&first, NULL) == ASCIICHAT_OK;
  if (contender)
    joined &= thread_join(&second, NULL) == ASCIICHAT_OK;
  if (!started || !contender || !serialized || !joined) {
    log_error("Splash first-call race: discovery=%d started=%d contender=%d waited=%d premature_lock=%d joined=%d",
              discovery, started, contender, atomic_load(&waited), atomic_load(&premature_lock), joined);
    return false;
  }
  return true;
}

int main(void) {
  if (asciichat_shared_init(NULL, false, true) != ASCIICHAT_OK)
    return 1;
  log_set_terminal_output(true);
  bool notification = competing_initializers(false);
  bool discovery = competing_initializers(true);
  return notification && discovery ? 0 : 1;
}
