#include "../worker_assert.h"
#define CHECK_WORKER cr_assert_worker
#include <criterion/criterion.h>
#define CHECK(...) cr_assert(__VA_ARGS__)
#include "../../standalone/network/vectored_fixture.h"

static void setup(void) {
  log_init(NULL, LOG_WARN, false, false);
  errno_suppress(true);
  cr_assert_eq(socket_init(), ASCIICHAT_OK);
}

static void teardown(void) {
  socket_cleanup();
  buffer_stats_t stats;
  buffer_domain_stats(buffer_default_domain(), &stats);
  cr_assert_eq(stats.outstanding, 0);
  buffer_system_cleanup();
  cr_assert_eq(buffer_process_reserved(), 0);
  log_destroy();
}

TestSuite(vectored, .init = setup, .fini = teardown, .timeout = 30);

Test(vectored, crc_vectors) {
  crc_vectors();
}

Test(vectored, deadline_and_validation) {
  deadline_and_validation();
}

Test(vectored, message_fallback) {
  message_fallback();
}

Test(vectored, media_wire_compatibility) {
  media_wire_compatibility();
}

Test(vectored, malformed_and_disconnect) {
  malformed_and_disconnect();
}

Test(vectored, concurrent_senders) {
  concurrent_senders();
}

Test(vectored, full_duplex) {
  full_duplex();
}

Test(vectored, zerocopy_cancellation) {
  zerocopy_cancellation();
}

Test(vectored, plain_0) {
  roundtrip(0, false, 3, false);
}

Test(vectored, encrypted_0) {
  roundtrip(0, true, 3, false);
}

Test(vectored, plain_256) {
  roundtrip(256, false, 3, false);
}

Test(vectored, encrypted_256) {
  roundtrip(256, true, 3, false);
}

Test(vectored, plain_4096) {
  roundtrip(4096, false, 3, false);
}

Test(vectored, encrypted_4096) {
  roundtrip(4096, true, 3, false);
}

Test(vectored, plain_65536) {
  roundtrip(65536, false, 3, false);
}

Test(vectored, encrypted_65536) {
  roundtrip(65536, true, 3, false);
}

Test(vectored, plain_maximum_plaintext) {
  roundtrip(CRYPTO_MAX_PLAINTEXT_SIZE - sizeof(packet_header_t), false, 3, false);
}

Test(vectored, encrypted_maximum_plaintext) {
  roundtrip(CRYPTO_MAX_PLAINTEXT_SIZE - sizeof(packet_header_t), true, 3, false);
}

Test(vectored, maximum_packet) {
  roundtrip(MAX_PACKET_SIZE, false, 2, false);
}
