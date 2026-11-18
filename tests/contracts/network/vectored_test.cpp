#include <catch2/catch_test_macros.hpp>
#include "../worker_assert.h"
#define CHECK_WORKER ASCIICHAT_WORKER_ASSERT
#define CHECK(...) REQUIRE(__VA_ARGS__)
#include "../../standalone/network/vectored_fixture.h"

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
TEST_CASE("vectored/crc_vectors", "[vectored] [contract]") {
  crc_vectors();
}

TEST_CASE("vectored/deadline_and_validation", "[vectored] [contract]") {
  deadline_and_validation();
}

TEST_CASE("vectored/message_fallback", "[vectored] [contract]") {
  message_fallback();
}

TEST_CASE("vectored/media_wire_compatibility", "[vectored] [contract]") {
  media_wire_compatibility();
}

TEST_CASE("vectored/malformed_and_disconnect", "[vectored] [contract]") {
  malformed_and_disconnect();
}

TEST_CASE("vectored/concurrent_senders", "[vectored] [contract]") {
  concurrent_senders();
}

TEST_CASE("vectored/full_duplex", "[vectored] [contract]") {
  full_duplex();
}

TEST_CASE("vectored/zerocopy_cancellation", "[vectored] [contract]") {
  zerocopy_cancellation();
}

TEST_CASE("vectored/plain_0", "[vectored] [contract]") {
  roundtrip(0, false, 3, false);
}

TEST_CASE("vectored/encrypted_0", "[vectored] [contract]") {
  roundtrip(0, true, 3, false);
}

TEST_CASE("vectored/plain_256", "[vectored] [contract]") {
  roundtrip(256, false, 3, false);
}

TEST_CASE("vectored/encrypted_256", "[vectored] [contract]") {
  roundtrip(256, true, 3, false);
}

TEST_CASE("vectored/plain_4096", "[vectored] [contract]") {
  roundtrip(4096, false, 3, false);
}

TEST_CASE("vectored/encrypted_4096", "[vectored] [contract]") {
  roundtrip(4096, true, 3, false);
}

TEST_CASE("vectored/plain_65536", "[vectored] [contract]") {
  roundtrip(65536, false, 3, false);
}

TEST_CASE("vectored/encrypted_65536", "[vectored] [contract]") {
  roundtrip(65536, true, 3, false);
}

TEST_CASE("vectored/plain_maximum_plaintext", "[vectored] [contract]") {
  roundtrip(CRYPTO_MAX_PLAINTEXT_SIZE - sizeof(packet_header_t), false, 3, false);
}

TEST_CASE("vectored/encrypted_maximum_plaintext", "[vectored] [contract]") {
  roundtrip(CRYPTO_MAX_PLAINTEXT_SIZE - 24 - sizeof(packet_header_t), true, 3, false);
}

TEST_CASE("vectored/maximum_packet", "[vectored] [contract]") {
  roundtrip(MAX_PACKET_SIZE, false, 2, false);
}
