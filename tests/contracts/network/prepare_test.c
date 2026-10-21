#include <criterion/criterion.h>
#define CHECK(...) cr_assert(__VA_ARGS__)
#include "../../standalone/network/prepare_fixture.h"

static void setup(void) {
  log_init(NULL, LOG_WARN, false, false);
  asciichat_errno_suppress(true);
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

TestSuite(prepare, .init = setup, .fini = teardown, .timeout = 30);

Test(prepare, wire_headers_and_crc) {
  fixtures();
}

Test(prepare, retained_async_owners) {
  retained_async_receive();
}

Test(prepare, sync_plain) {
  tcp_fixture(false, false);
}

Test(prepare, sync_encrypted) {
  tcp_fixture(false, true);
}

Test(prepare, async_plain) {
  tcp_fixture(true, false);
}

Test(prepare, async_encrypted) {
  tcp_fixture(true, true);
}
