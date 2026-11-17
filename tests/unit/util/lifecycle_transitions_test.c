#include <criterion/criterion.h>
#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/log/log.h>

static void setup(void) {
  log_init(NULL, LOG_ERROR, false, false);
}
TestSuite(lifecycle_transitions, .init = setup, .fini = log_destroy);

Test(lifecycle_transitions, aborted_initialization_and_completed_destruction_allow_retry) {
  lifecycle_t state = LIFECYCLE_INIT;
  cr_assert_not(lifecycle_is_initialized(&state));
  cr_assert_not(lifecycle_destroy_once(&state));
  cr_assert(lifecycle_init_once(&state));
  cr_assert_not(lifecycle_init_once(&state));
  cr_assert_not(lifecycle_destroy_once(&state));
  lifecycle_init_abort(&state);
  cr_assert(lifecycle_init_once(&state));
  lifecycle_init_commit(&state);
  cr_assert(lifecycle_is_initialized(&state));
  cr_assert_not(lifecycle_init_once(&state));
  cr_assert(lifecycle_destroy_once(&state));
  cr_assert_not(lifecycle_is_initialized(&state));
  cr_assert_not(lifecycle_destroy_once(&state));
  cr_assert_not(lifecycle_init_once(&state));
  lifecycle_destroy_commit(&state);
  cr_assert(lifecycle_init(&state, NULL));
  cr_assert_not(lifecycle_init(&state, NULL));
  cr_assert(lifecycle_reset(&state));
  cr_assert_not(lifecycle_reset(&state));
  cr_assert(lifecycle_init(&state, "retry"));
  cr_assert(lifecycle_shutdown(&state));
  cr_assert_not(lifecycle_shutdown(&state));
}

Test(lifecycle_transitions, permanent_shutdown_prevents_reinitialization) {
  lifecycle_t unused = LIFECYCLE_INIT, initialized = LIFECYCLE_INIT;
  cr_assert_not(lifecycle_shutdown_forever(&unused));
  cr_assert(lifecycle_is_dead(&unused));
  cr_assert(lifecycle_init(&initialized, "permanent"));
  cr_assert(lifecycle_shutdown_forever(&initialized));
  lifecycle_t *states[] = {&unused, &initialized};
  for (size_t i = 0; i < 2; ++i) {
    cr_assert(lifecycle_is_dead(states[i]));
    cr_assert_not(lifecycle_shutdown_forever(states[i]));
    cr_assert_not(lifecycle_init(states[i], "dead"));
    cr_assert_not(lifecycle_init_once(states[i]));
    cr_assert_not(lifecycle_destroy_once(states[i]));
    cr_assert_not(lifecycle_shutdown(states[i]));
    cr_assert_not(lifecycle_reset(states[i]));
  }
}

Test(lifecycle_transitions, owned_sync_primitives_can_be_used_after_each_reinitialization) {
  mutex_t mutex;
  rwlock_t rwlock;
  lifecycle_t locked = LIFECYCLE_INIT_MUTEX(&mutex), shared = LIFECYCLE_INIT_RWLOCK(&rwlock);
  for (int cycle = 0; cycle < 2; ++cycle) {
    cr_assert(lifecycle_init(&locked, "lifecycle-mutex"));
    cr_assert(lifecycle_init(&shared, "lifecycle-rwlock"));
    mutex_lock(&mutex);
    mutex_unlock(&mutex);
    rwlock_wrlock(&rwlock);
    rwlock_wrunlock(&rwlock);
    cr_assert(cycle ? lifecycle_shutdown(&locked) : lifecycle_reset(&locked));
    cr_assert(cycle ? lifecycle_shutdown(&shared) : lifecycle_reset(&shared));
  }
}

Test(lifecycle_transitions, absent_handles_are_safe_for_optional_cleanup) {
  cr_assert_not(lifecycle_init(NULL, NULL));
  cr_assert_not(lifecycle_init_once(NULL));
  lifecycle_init_abort(NULL);
  lifecycle_init_commit(NULL);
  lifecycle_destroy_commit(NULL);
  cr_assert_not(lifecycle_destroy_once(NULL));
  cr_assert_not(lifecycle_shutdown(NULL));
  cr_assert_not(lifecycle_shutdown_forever(NULL));
  cr_assert_not(lifecycle_reset(NULL));
  cr_assert_not(lifecycle_is_initialized(NULL));
  cr_assert_not(lifecycle_is_dead(NULL));
}
