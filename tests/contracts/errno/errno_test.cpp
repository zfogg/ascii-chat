#include <catch2/catch_test_macros.hpp>
#include "../worker_assert.h"
#define CHECK_WORKER ASCIICHAT_WORKER_ASSERT
#define CHECK(...) REQUIRE(__VA_ARGS__)
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
TEST_CASE("errno_stack/diagnostic_verbosity_contract", "[errno_stack] [contract]") {
  diagnostic_verbosity_contract();
}

TEST_CASE("errno_stack/network_recovery_contract", "[errno_stack] [contract]") {
  network_recovery_contract();
}

TEST_CASE("errno_stack/peek_contract", "[errno_stack] [contract]") {
  peek_contract();
}

TEST_CASE("errno_stack/stack_contract", "[errno_stack] [contract]") {
  stack_contract();
}

TEST_CASE("errno_stack/selectable_errno_contract", "[errno_stack] [contract]") {
  selectable_errno_contract();
}

TEST_CASE("errno_stack/scopes_and_history", "[errno_stack] [contract]") {
  scopes_and_history();
}

TEST_CASE("errno_stack/capacity_contract", "[errno_stack] [contract]") {
  capacity_contract();
}

TEST_CASE("errno_stack/formatted_error_contract", "[errno_stack] [contract]") {
  formatted_error_contract();
}

TEST_CASE("errno_stack/cross_thread_contract", "[errno_stack] [contract]") {
  cross_thread_contract();
}

TEST_CASE("errno_stack/cross_thread_history_timing", "[errno_stack] [contract]") {
  cross_thread_history_timing();
}

TEST_CASE("errno_stack/concurrency_contract", "[errno_stack] [contract]") {
  concurrency_contract();
}

TEST_CASE("errno_stack/scheduler_shutdown_and_first_exit_code", "[errno_stack] [contract]") {
  REQUIRE((debug_stats_init()) == (ASCIICHAT_OK));
  REQUIRE((debug_stats_start_thread()) == (ASCIICHAT_OK));
  REQUIRE((debug_sync_start_thread()) == (ASCIICHAT_OK));
  REQUIRE((debug_errno_start_thread()) == (ASCIICHAT_OK));
  debug_errno_print_delayed(60 * NS_PER_SEC_INT);
  debug_sync_print_state_delayed(60 * NS_PER_SEC_INT);
  debug_sync_print_backtrace_delayed(60 * NS_PER_SEC_INT);
  platform_sleep_ns(5 * NS_PER_MS_INT);
  uint64_t start = time_get_ns();
  debug_sync_destroy();
  debug_errno_destroy();
  debug_stats_destroy();
  REQUIRE(time_get_ns() - start < NS_PER_SEC_INT);
  errno_destroy();
  REQUIRE((asciichat_error_stats_get().registered_threads) == (0));
  errno_request_exit(ERROR_CRYPTO_AUTH);
  errno_request_exit(ERROR_MEDIA_OPEN);
  CLEAR_ERRNO_ALL();
  errno_shutdown();
  REQUIRE((errno_exit_code()) == (ERROR_CRYPTO_AUTH));
  errno_shutdown();
}
