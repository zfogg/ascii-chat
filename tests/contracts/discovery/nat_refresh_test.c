#include <criterion/criterion.h>
#include <ascii-chat/options/rcu.h>

int refresh_create(asciichat_thread_t *thread, const char *name, void *(*worker)(void *), void *arg);
int refresh_join(asciichat_thread_t *thread, void **result, uint64_t timeout);

/* Exercise the production worker lifecycle with deterministic network/thread
 * boundaries. No camera, public STUN server, or scheduler timing is required. */
#define nat_detect_quality refresh_probe
#define acthread_create refresh_create
#define thread_join_timeout refresh_join
#include "../../../src/discovery/session.c"
#undef thread_join_timeout
#undef acthread_create
#undef nat_detect_quality

static discovery_session_t session;
static uint64_t clock_ms;
static unsigned int launches, joins;
static int create_result, join_result;
static asciichat_error_t probe_result;
static void *(*pending_worker)(void *);
static void *pending_arg;

static uint64_t refresh_clock(void) {
  return clock_ms * 1000;
}
asciichat_error_t refresh_probe(nat_quality_t *quality, const char *host, uint16_t port) {
  (void)host;
  (void)port;
  nat_quality_init(quality);
  quality->detection_complete = true;
  quality->has_public_ip = true;
  return probe_result;
}
int refresh_create(asciichat_thread_t *thread, const char *name, void *(*worker)(void *), void *arg) {
  (void)thread;
  (void)name;
  launches++;
  if (!create_result) {
    pending_worker = worker;
    pending_arg = arg;
  }
  return create_result;
}
int refresh_join(asciichat_thread_t *thread, void **result, uint64_t timeout) {
  (void)thread;
  (void)result;
  cr_assert_eq(timeout, 0, "Refresh must not block on a detector doing network I/O");
  cr_assert(session.nat_detection_completed);
  joins++;
  return join_result;
}
static void setup(void) {
  cr_assert_eq(asciichat_shared_init(NULL, false, true), ASCIICHAT_OK);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  static app_callbacks_t callbacks = {.platform_get_monotonic_time_us = refresh_clock};
  app_callbacks_register(&callbacks);
  memset(&session, 0, sizeof(session));
  cr_assert_eq(mutex_init(&session.nat_quality_mutex, "nat_refresh_test"), 0);
  clock_ms = 100000;
  launches = joins = 0;
  create_result = join_result = 0;
  probe_result = ASCIICHAT_OK;
  pending_worker = NULL;
}
static void teardown(void) {
  mutex_destroy(&session.nat_quality_mutex);
  options_state_destroy();
  asciichat_shared_destroy();
}
TestSuite(nat_refresh, .init = setup, .fini = teardown);

Test(nat_refresh, stale_result_refreshes_without_overlap_or_cache_loss) {
  start_nat_detection(&session);
  cr_assert_eq(launches, 1);
  start_nat_detection(&session);
  cr_assert_eq(launches, 1);
  cr_assert_eq(joins, 0, "Running worker must not be joined");
  cr_assert(pending_worker);
  pending_worker(pending_arg);
  cr_assert(session.nat_detection_completed && session.nat_quality_cached);
  uint64_t measured = session.nat_quality_measured_ms;
  clock_ms += 5000;
  start_nat_detection(&session);
  cr_assert_eq(launches, 1, "Fresh cache must not cause another measurement");
  clock_ms = measured + CONSENSUS_HANDOFF_FRESH_MS + 1;
  join_result = -2;
  start_nat_detection(&session);
  cr_assert(session.nat_detection_thread_started);
  cr_assert_eq(launches, 1, "Failed join must retain the previous handle");
  join_result = 0;
  start_nat_detection(&session);
  cr_assert_eq(launches, 2);
  cr_assert(!session.nat_detection_completed);
  cr_assert(session.nat_quality_cached && session.cached_nat_quality.has_public_ip);
  cr_assert_eq(session.nat_quality_measured_ms, measured);
  unsigned int previous_joins = joins;
  start_nat_detection(&session);
  cr_assert_eq(joins, previous_joins);
  cr_assert_eq(launches, 2);
  pending_worker(pending_arg);
  cr_assert_eq(session.nat_quality_measured_ms, clock_ms);
}

Test(nat_refresh, failed_probe_and_failed_start_are_throttled_and_retryable) {
  start_nat_detection(&session);
  pending_worker(pending_arg);
  uint64_t measured = session.nat_quality_measured_ms;
  clock_ms += CONSENSUS_HANDOFF_FRESH_MS + 1;
  start_nat_detection(&session);
  probe_result = ERROR_NETWORK;
  pending_worker(pending_arg);
  cr_assert_eq(session.nat_quality_measured_ms, measured);
  cr_assert(session.nat_quality_cached && session.nat_detection_completed);
  start_nat_detection(&session);
  cr_assert_eq(launches, 2, "Failed probe must not retry in a tight loop");
  clock_ms += CONSENSUS_HANDOFF_FRESH_MS + 1;
  create_result = -1;
  start_nat_detection(&session);
  cr_assert_eq(launches, 3);
  cr_assert(!session.nat_detection_thread_started);
  start_nat_detection(&session);
  cr_assert_eq(launches, 3, "Failed thread start must also be throttled");
  clock_ms += CONSENSUS_HANDOFF_FRESH_MS + 1;
  create_result = 0;
  probe_result = ASCIICHAT_OK;
  start_nat_detection(&session);
  cr_assert_eq(launches, 4);
  pending_worker(pending_arg);
  cr_assert_eq(session.nat_quality_measured_ms, clock_ms);
}
