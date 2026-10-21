#include <criterion/criterion.h>
#include <ascii-chat/network/tcp/async.h>
#include <ascii-chat/common.h>

static platform_event_close_fn failed_close;
static void *failed_arg;
static unsigned close_requests, destroyed;

static asciichat_error_t fail_stream_start(platform_event_stream_t *stream, platform_event_read_fn read,
                                           platform_event_close_fn close, void *arg) {
  (void)stream;
  (void)read;
  failed_close = close;
  failed_arg = arg;
  return SET_ERRNO(ERROR_NETWORK, "Injected accepted stream startup failure");
}

static void record_stream_close(platform_event_stream_t *stream, asciichat_error_t reason) {
  (void)stream;
  cr_assert_eq(reason, ERROR_NETWORK);
  ++close_requests;
}

static void record_transport_destroy(acip_transport_t *transport) {
  ++destroyed;
  acip_transport_destroy(transport);
}

size_t test_async_tcp_transport_queued(acip_transport_t *transport);

static void observe_fake_stream(platform_event_stream_t *stream, platform_event_queue_fn observer, void *context) {
  (void)stream;
  if (observer)
    observer(context, 0);
}

// Exercise the production constructor with a startup failure that retains its close callback.
#define platform_event_stream_start fail_stream_start
#define platform_event_stream_observe_queue observe_fake_stream
#define platform_event_stream_close record_stream_close
#define acip_transport_destroy record_transport_destroy
#define acip_async_tcp_transport_create test_async_tcp_transport_create
#define acip_async_tcp_transport_detach test_async_tcp_transport_detach
#define acip_async_tcp_transport_is test_async_tcp_transport_is
#define acip_async_tcp_transport_queued test_async_tcp_transport_queued
#define acip_async_tcp_client_create test_async_tcp_client_create
#define acip_async_tcp_accepted_create test_async_tcp_accepted_create
#include "../../../lib/network/tcp/async.c"

void accepted_start_failure_test(void) {
  int stream_token;
  platform_event_stream_t *stream = (platform_event_stream_t *)&stream_token;
  cr_assert_null(acip_async_tcp_accepted_create(stream));
  cr_assert_eq(close_requests, 1);
  cr_assert_eq(destroyed, 0, "Transport must survive until close completion");
  cr_assert_not_null(failed_close);
  failed_close(failed_arg, ERROR_NETWORK);
  cr_assert_eq(destroyed, 1);
  CLEAR_ERRNO_ALL();
}
