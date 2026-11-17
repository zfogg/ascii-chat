#include <catch2/catch_test_macros.hpp>
#define CHECK(...) REQUIRE(__VA_ARGS__)
#include "../../standalone/network/prepare_fixture.h"

static void setup(void) {
  log_init(NULL, LOG_WARN, false, false);
  errno_suppress(true);
  REQUIRE((socket_init()) == (ASCIICHAT_OK));
}

static void teardown(void) {
  socket_cleanup();
  buffer_stats_t stats;
  buffer_domain_stats(buffer_default_domain(), &stats);
  REQUIRE((stats.outstanding) == (0));
  buffer_system_cleanup();
  REQUIRE((buffer_process_reserved()) == (0));
  log_destroy();
}
TEST_CASE("prepare/wire_headers_and_crc", "[prepare] [contract]") {
  fixtures();
}

TEST_CASE("prepare/retained_async_owners", "[prepare] [contract]") {
  retained_async_receive();
}

TEST_CASE("prepare/sync_plain", "[prepare] [contract]") {
  tcp_fixture(false, false);
}

TEST_CASE("prepare/sync_encrypted", "[prepare] [contract]") {
  tcp_fixture(false, true);
}

TEST_CASE("prepare/async_plain", "[prepare] [contract]") {
  tcp_fixture(true, false);
}

TEST_CASE("prepare/async_encrypted", "[prepare] [contract]") {
  tcp_fixture(true, true);
}
