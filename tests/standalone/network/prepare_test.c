#include "prepare_fixture.h"
#include <ascii-chat/network/websocket/internal.h>

static atomic_t close_ready, close_go, close_finished, service_release, service_exited;

static void *gated_service(void *unused) {
  (void)unused;
  while (!atomic_load_bool(&service_release))
    platform_sleep_ms(1);
  atomic_store_bool(&service_exited, true);
  return NULL;
}

static void *concurrent_close(void *arg) {
  acip_transport_t *transport = arg;
  atomic_fetch_add_int(&close_ready, 1);
  while (!atomic_load_bool(&close_go))
    platform_sleep_ms(1);
  CHECK(transport->methods->close(transport) == ASCIICHAT_OK);
  atomic_fetch_add_int(&close_finished, 1);
  return NULL;
}

static void close_ownership(acip_transport_t *transport) {
  websocket_transport_data_t *ws = transport->impl_data;
  // Quiesce the real LWS worker, then hold its replacement at a deterministic
  // exit barrier. Close callers must signal cancellation without joining it;
  // the final destroy below is the sole join owner. This forces the failed-send
  // versus local-stop race without relying on network or scheduler timing.
  transport->methods->interrupt(transport);
  CHECK(thread_join(&ws->service_thread, NULL) == ASCIICHAT_OK);
  ws->service_running = false;
  CHECK(acthread_create(&ws->service_thread, "gated_service", gated_service, NULL) == ASCIICHAT_OK);
  ws->service_running = true;
  asciichat_thread_t closers[2];
  for (unsigned i = 0; i < 2; ++i)
    CHECK(acthread_create(&closers[i], "concurrent_close", concurrent_close, transport) == ASCIICHAT_OK);
  uint64_t deadline = time_get_ns() + 2 * NS_PER_SEC_INT;
  while (atomic_load_int(&close_ready) < 2 && time_get_ns() < deadline)
    platform_sleep_ms(1);
  CHECK(atomic_load_int(&close_ready) == 2);
  deadline = time_get_ns() + 2 * NS_PER_SEC_INT;
  atomic_store_bool(&close_go, true);
  while (atomic_load_int(&close_finished) < 2 && time_get_ns() < deadline)
    platform_sleep_ms(1);
  bool returned = atomic_load_int(&close_finished) == 2;
  CHECK(!atomic_load_bool(&service_exited));
  atomic_store_bool(&service_release, true);
  CHECK(returned);
  for (unsigned i = 0; i < 2; ++i)
    CHECK(thread_join(&closers[i], NULL) == ASCIICHAT_OK);
  CHECK(ws->service_running);
}

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  log_init(NULL, LOG_WARN, false, false);
  errno_suppress(true);
  CHECK(socket_init() == ASCIICHAT_OK);
  websocket_fixture(argv[1], close_ownership);
  CHECK(atomic_load_bool(&service_exited));
  buffer_system_cleanup();
  CHECK(buffer_process_reserved() == 0);
  socket_cleanup();
  log_destroy();
  return 0;
}
