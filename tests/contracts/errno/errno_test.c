#include "../worker_assert.h"
#define CHECK_WORKER cr_assert_worker
#include <criterion/criterion.h>
#define CHECK(...) cr_assert(__VA_ARGS__)
#include "../../standalone/errno/errno_fixture.h"

static void setup(void) {
  log_init(NULL, LOG_WARN, false, false);
  errno_suppress(true);
}

static void teardown(void) {
  CLEAR_ERRNO_ALL();
  errno_destroy();
  log_destroy();
}

TestSuite(errno_stack, .init = setup, .fini = teardown, .timeout = 30);

Test(errno_stack, diagnostic_verbosity_contract) {
  diagnostic_verbosity_contract();
}

Test(errno_stack, network_recovery_contract) {
  network_recovery_contract();
}

Test(errno_stack, peek_contract) {
  peek_contract();
}

Test(errno_stack, stack_contract) {
  stack_contract();
}

Test(errno_stack, selectable_errno_contract) {
  selectable_errno_contract();
}

Test(errno_stack, scopes_and_history) {
  scopes_and_history();
}

Test(errno_stack, capacity_contract) {
  capacity_contract();
}

Test(errno_stack, formatted_error_contract) {
  formatted_error_contract();
}

Test(errno_stack, cross_thread_contract) {
  cross_thread_contract();
}

Test(errno_stack, cross_thread_history_timing) {
  cross_thread_history_timing();
}

Test(errno_stack, concurrency_contract) {
  concurrency_contract();
}

Test(errno_stack, scheduler_shutdown_and_first_exit_code) {
  cr_assert_eq(debug_stats_init(), ASCIICHAT_OK);
  cr_assert_eq(debug_stats_start_thread(), ASCIICHAT_OK);
  cr_assert_eq(debug_sync_start_thread(), ASCIICHAT_OK);
  cr_assert_eq(debug_errno_start_thread(), ASCIICHAT_OK);
  debug_errno_print_delayed(60 * NS_PER_SEC_INT);
  debug_sync_print_state_delayed(60 * NS_PER_SEC_INT);
  debug_sync_print_backtrace_delayed(60 * NS_PER_SEC_INT);
  platform_sleep_ns(5 * NS_PER_MS_INT);
  uint64_t start = time_get_ns();
  debug_sync_destroy();
  debug_errno_destroy();
  debug_stats_destroy();
  cr_assert(time_get_ns() - start < NS_PER_SEC_INT);
  errno_destroy();
  cr_assert_eq(asciichat_error_stats_get().registered_threads, 0);
  errno_request_exit(ERROR_CRYPTO_AUTH);
  errno_request_exit(ERROR_MEDIA_OPEN);
  CLEAR_ERRNO_ALL();
  errno_shutdown();
  cr_assert_eq(errno_exit_code(), ERROR_CRYPTO_AUTH);
  errno_shutdown();
}
