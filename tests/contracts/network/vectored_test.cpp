#include "../worker_assert.h"
#define CHECK_WORKER ASCIICHAT_WORKER_ASSERT
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#define CHECK(...) ASCIICHAT_REQUIRE(__VA_ARGS__)
#include "../../standalone/network/vectored_fixture.h"

static void setup(void) {
  log_init(NULL, LOG_WARN, false, false);
  errno_suppress(true);
  ASCIICHAT_REQUIRE_EQ(socket_init(), ASCIICHAT_OK);
}

static void teardown(void) {
  socket_cleanup();
  buffer_stats_t stats;
  buffer_domain_stats(buffer_default_domain(), &stats);
  ASCIICHAT_REQUIRE_EQ(stats.outstanding, 0);
  buffer_system_cleanup();
  ASCIICHAT_REQUIRE_EQ(buffer_process_reserved(), 0);
  log_destroy();
}
TEST_CASE("vectored/crc_vectors", "[vectored]") {
  crc_vectors();
}

TEST_CASE("vectored/deadline_and_validation", "[vectored]") {
  deadline_and_validation();
}

TEST_CASE("vectored/message_fallback", "[vectored]") {
  message_fallback();
}

TEST_CASE("vectored/media_wire_compatibility", "[vectored]") {
  media_wire_compatibility();
}

TEST_CASE("vectored/malformed_and_disconnect", "[vectored]") {
  malformed_and_disconnect();
}

TEST_CASE("vectored/concurrent_senders", "[vectored]") {
  concurrent_senders();
}

TEST_CASE("vectored/full_duplex", "[vectored]") {
  full_duplex();
}

TEST_CASE("vectored/zerocopy_cancellation", "[vectored]") {
  zerocopy_cancellation();
}

TEST_CASE("vectored/plain_0", "[vectored]") {
  roundtrip(0, false, 3, false);
}

TEST_CASE("vectored/encrypted_0", "[vectored]") {
  roundtrip(0, true, 3, false);
}

TEST_CASE("vectored/plain_256", "[vectored]") {
  roundtrip(256, false, 3, false);
}

TEST_CASE("vectored/encrypted_256", "[vectored]") {
  roundtrip(256, true, 3, false);
}

TEST_CASE("vectored/plain_4096", "[vectored]") {
  roundtrip(4096, false, 3, false);
}

TEST_CASE("vectored/encrypted_4096", "[vectored]") {
  roundtrip(4096, true, 3, false);
}

TEST_CASE("vectored/plain_65536", "[vectored]") {
  roundtrip(65536, false, 3, false);
}

TEST_CASE("vectored/encrypted_65536", "[vectored]") {
  roundtrip(65536, true, 3, false);
}

TEST_CASE("vectored/plain_maximum_plaintext", "[vectored]") {
  roundtrip(CRYPTO_MAX_PLAINTEXT_SIZE - sizeof(packet_header_t), false, 3, false);
}

TEST_CASE("vectored/encrypted_maximum_plaintext", "[vectored]") {
  roundtrip(CRYPTO_MAX_PLAINTEXT_SIZE - 24 - sizeof(packet_header_t), true, 3, false);
}

TEST_CASE("vectored/maximum_packet", "[vectored]") {
  roundtrip(MAX_PACKET_SIZE, false, 2, false);
}
