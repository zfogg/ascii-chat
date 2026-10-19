/** Production participant lifecycle with deterministic device/startup failures and real workers. */
#include "session/participant.h"
#include "session/capture.h"
#include "session/audio.h"
#include "../../../src/discovery/session.h"
#include <ascii-chat/common.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/audio/opus.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/util/time.h>
#include <stdlib.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/buffer.h>

#define CHECK(c)                                                                                                       \
  do {                                                                                                                 \
    if (!(c)) {                                                                                                        \
      log_error("CHECK failed: %s at %d", #c, __LINE__);                                                               \
      abort();                                                                                                         \
    }                                                                                                                  \
  } while (0)
static int fail_stage, capture_count, audio_count, codec_count, workers;
static bool fail_join;
static bool fail_tcp_transport;
static int transports_destroyed;
static atomic_t packets, video_packets, audio_packets;
static image_t *frame;

static session_capture_ctx_t *test_capture_create(const session_capture_config_t *config) {
  (void)config;
  if (fail_stage == 1) {
    SET_ERRNO(ERROR_MEMORY, "Injected allocation failure");
    return NULL;
  }
  capture_count++;
  return (session_capture_ctx_t *)SAFE_CALLOC(1, 1, char *);
}
static void test_capture_destroy(session_capture_ctx_t *ctx) {
  CHECK(workers == 0);
  capture_count--;
  SAFE_FREE(ctx);
}
static image_t *test_read_frame(session_capture_ctx_t *ctx) {
  (void)ctx;
  return frame;
}
static image_t *test_process(session_capture_ctx_t *ctx, image_t *input) {
  (void)ctx;
  (void)input;
  return image_new(2, 2);
}
static void test_sleep(session_capture_ctx_t *ctx) {
  (void)ctx;
  platform_sleep_ms(1);
}
static void *test_media_source(session_capture_ctx_t *ctx) {
  return ctx;
}
static session_audio_ctx_t *test_audio_create(bool host) {
  (void)host;
  if (fail_stage == 3) {
    SET_ERRNO(ERROR_MEMORY, "Injected allocation failure");
    return NULL;
  }
  audio_count++;
  return (session_audio_ctx_t *)SAFE_CALLOC(1, 1, char *);
}
static void test_audio_destroy(session_audio_ctx_t *ctx) {
  CHECK(workers <= 1); // Video may still be running; audio has already joined.
  audio_count--;
  SAFE_FREE(ctx);
}
static void test_audio_source(session_audio_ctx_t *ctx, void *source) {
  (void)ctx;
  (void)source;
}
static asciichat_error_t test_audio_start(session_audio_ctx_t *ctx) {
  (void)ctx;
  return fail_stage == 4 ? ERROR_INVALID_STATE : ASCIICHAT_OK;
}
static size_t test_audio_read(session_audio_ctx_t *ctx, float *samples, size_t count) {
  (void)ctx;
  memset(samples, 0, count * sizeof(float));
  platform_sleep_ms(1);
  return count;
}
static opus_codec_t *test_codec_create(opus_application_t app, int rate, int bitrate) {
  if (fail_stage == 5) {
    SET_ERRNO(ERROR_MEMORY, "Injected allocation failure");
    return NULL;
  }
  opus_codec_t *codec = opus_codec_create_encoder(app, rate, bitrate);
  CHECK(codec);
  codec_count++;
  return codec;
}
static void test_codec_destroy(opus_codec_t *codec) {
  codec_count--;
  opus_codec_destroy(codec);
}
static int test_thread_create(asciichat_thread_t *thread, const char *name, void *(*fn)(void *), void *arg) {
  if (fail_stage == 2 || fail_stage == 6)
    return -1;
  int result = asciichat_thread_create(thread, name, fn, arg);
  if (!result)
    workers++;
  return result;
}
static int test_thread_join(asciichat_thread_t *thread, void **result) {
  if (fail_join)
    return -1;
  int error = asciichat_thread_join(thread, result);
  if (!error)
    workers--;
  return error;
}
static acip_transport_t *test_tcp_transport_create_owned(const char *name, socket_t socket, crypto_context_t *crypto) {
  if (fail_tcp_transport) {
    SET_ERRNO(ERROR_MEMORY, "Injected TCP wrapper allocation failure");
    return NULL;
  }
  return acip_tcp_transport_create_owned(name, socket, crypto);
}
#define acip_tcp_transport_create_owned test_tcp_transport_create_owned
#define connection_factory_open test_connection_factory_open
#include "../../../lib/network/connection_factory.c"
#define session_capture_create test_capture_create
#define session_capture_destroy test_capture_destroy
#define session_capture_read_frame test_read_frame
#define session_capture_process_for_transmission test_process
#define session_capture_sleep_for_fps test_sleep
#define session_capture_get_media_source test_media_source
#define session_audio_create test_audio_create
#define session_audio_destroy test_audio_destroy
#define session_audio_set_capture_source test_audio_source
#define session_audio_start_duplex test_audio_start
#define session_audio_read_captured test_audio_read
#define opus_codec_create_encoder test_codec_create
#define opus_codec_destroy test_codec_destroy
#define asciichat_thread_create test_thread_create
#define asciichat_thread_join test_thread_join
#include "session/participant.c"
#include "../../../src/discovery/session.c"

static bool connected(acip_transport_t *transport) {
  (void)transport;
  return true;
}
static asciichat_error_t test_transport_send(acip_transport_t *transport, const void *data, size_t len) {
  (void)transport;
  CHECK(data && len);
  atomic_fetch_add_int(&packets, 1);
  CHECK(len >= sizeof(packet_header_t));
  const packet_header_t *header = data;
  uint16_t type = NET_TO_HOST_U16(header->type);
  if (type == PACKET_TYPE_IMAGE_FRAME)
    atomic_fetch_add_int(&video_packets, 1);
  if (type == PACKET_TYPE_AUDIO_OPUS_BATCH)
    atomic_fetch_add_int(&audio_packets, 1);
  return ASCIICHAT_OK;
}
static void clean(void) {
  CHECK(!workers && !capture_count && !audio_count && !codec_count);
}
static void destroy_transport_impl(acip_transport_t *transport) {
  (void)transport;
  clean();
  transports_destroyed++;
}
static void *destroy_from_foreign_thread(void *context) {
  session_participant_destroy(context);
  return NULL;
}

int main(void) {
  log_init(NULL, LOG_ERROR, false, false);
  log_set_terminal_output(true);
  CHECK(socket_init() == ASCIICHAT_OK);
  options_t options = options_t_new();
  CHECK(options_state_init() == ASCIICHAT_OK);
  CHECK(options_state_set(&options) == ASCIICHAT_OK);
  frame = image_new(2, 2);
  CHECK(frame);
  acip_transport_methods_t methods = {.send = test_transport_send, .is_connected = connected};
  acip_transport_t transport = {.methods = &methods};
  session_participant_config_t config = {.enable_video = true, .enable_audio = true};
  // Refusing cleanup on another thread must retain even a workerless owner.
  session_participant_t *retained = session_participant_create(&config);
  CHECK(session_participant_set_transport(retained, &transport, ACIP_TRANSPORT_BORROWED) == ASCIICHAT_OK);
  asciichat_thread_t foreign;
  CHECK(asciichat_thread_create(&foreign, "foreign_destroy", destroy_from_foreign_thread, retained) == ASCIICHAT_OK);
  CHECK(asciichat_thread_join(&foreign, NULL) == ASCIICHAT_OK);
  CHECK(retained->initialized && session_participant_get_transport(retained) == &transport);
  CHECK(atomic_load_int(&retained->connection.state) == SESSION_CONNECTION_STOPPING);
  session_participant_destroy(retained);
  clean();
  CHECK(session_participant_start_video(NULL) == ERROR_INVALID_PARAM);
  session_participant_destroy(NULL);
  for (fail_stage = 1; fail_stage <= 6; fail_stage++) {
    session_participant_t *p = session_participant_create(&config);
    CHECK(p);
    CHECK(session_participant_start_video(p) == ERROR_INVALID_STATE);
    CHECK(session_participant_set_transport(p, &transport, ACIP_TRANSPORT_BORROWED) == ASCIICHAT_OK);
    asciichat_error_t error = fail_stage <= 2 ? session_participant_start_video(p) : session_participant_start_audio(p);
    CHECK(error == (fail_stage == 1 || fail_stage == 3 || fail_stage == 5 ? ERROR_MEMORY
                    : fail_stage == 2 || fail_stage == 6                  ? ERROR_THREAD
                                                                          : ERROR_INVALID_STATE));
    CHECK(!session_participant_is_video_active(p) && !session_participant_is_audio_active(p));
    clean();
    session_participant_disconnect(p);
    session_participant_disconnect(p);
    session_participant_destroy(p);
    clean();
  }
  fail_stage = 0;
  for (int cycle = 0; cycle < 20; cycle++) {
    session_participant_t *p = session_participant_create(&config);
    CHECK(session_participant_set_transport(p, &transport, ACIP_TRANSPORT_BORROWED) == ASCIICHAT_OK);
    CHECK(session_participant_start_video(p) == ASCIICHAT_OK);
    CHECK(session_participant_start_video(p) == ASCIICHAT_OK);
    CHECK(session_participant_start_audio(p) == ASCIICHAT_OK);
    CHECK(session_participant_start_audio(p) == ASCIICHAT_OK);
    CHECK(workers == 2 && session_participant_is_video_active(p) && session_participant_is_audio_active(p));
    CHECK(session_participant_set_transport(p, NULL, ACIP_TRANSPORT_BORROWED) == ERROR_INVALID_STATE);
    acip_transport_t *unchanged = &transport;
    acip_transport_ownership_t unchanged_ownership = ACIP_TRANSPORT_OWNED;
    CHECK(session_participant_take_transport(p, &unchanged, &unchanged_ownership) == ERROR_INVALID_STATE);
    CHECK(unchanged == &transport && unchanged_ownership == ACIP_TRANSPORT_OWNED);
    int before = atomic_load_int(&packets);
    for (int i = 0; i < 200 && atomic_load_int(&packets) < before + 4; i++)
      platform_sleep_ms(1);
    CHECK(atomic_load_int(&packets) >= before + 4);
    if (cycle % 3 == 0) {
      session_participant_stop_video(p); // Joins audio before releasing its borrowed source.
      clean();
      session_participant_stop_video(p);
      CHECK(session_participant_start_video(p) == ASCIICHAT_OK);
    }
    if (cycle % 3 == 1)
      p->connected = false; // Partial/disconnected state still owns workers.
    if (cycle % 3 == 2) {
      session_participant_destroy(p);
      clean();
      continue;
    }
    session_participant_disconnect(p);
    clean();
    CHECK(!session_participant_is_video_active(p) && !session_participant_is_audio_active(p));
    session_participant_destroy(p);
    clean();
  }
  CHECK(atomic_load_int(&video_packets) > 0 && atomic_load_int(&audio_packets) > 0);
  // Discovery retains a borrowed transport until all workers join, then frees
  // it exactly once on teardown. A failed join permits retry without dangling pointers.
  discovery_session_t *session = SAFE_CALLOC(1, sizeof(*session), discovery_session_t *);
  CHECK(mutex_init(&session->nat_quality_mutex, "test_nat_quality") == ASCIICHAT_OK);
  session->participant_ctx = session_participant_create(&config);
  acip_transport_t *owned = SAFE_CALLOC(1, sizeof(*owned), acip_transport_t *);
  methods.destroy_impl = destroy_transport_impl;
  owned->methods = &methods;
  CHECK(session_participant_set_transport(session->participant_ctx, owned, ACIP_TRANSPORT_OWNED) == ASCIICHAT_OK);
  CHECK(session_participant_start_video(session->participant_ctx) == ASCIICHAT_OK);
  CHECK(session_participant_start_audio(session->participant_ctx) == ASCIICHAT_OK);
  fail_join = true;
  discovery_session_destroy(session);
  CHECK(transports_destroyed == 0 && workers == 2);
  CHECK(session_participant_get_transport(session->participant_ctx) == owned);
  fail_join = false;
  discovery_session_destroy(session);
  session = NULL;
  owned = NULL;
  CHECK(transports_destroyed == 1);
  clean();
  session = SAFE_CALLOC(1, sizeof(*session), discovery_session_t *);
  CHECK(mutex_init(&session->nat_quality_mutex, "test_nat_quality") == ASCIICHAT_OK);
  session->participant_ctx = session_participant_create(&config);
  owned = SAFE_CALLOC(1, sizeof(*owned), acip_transport_t *);
  owned->methods = &methods;
  CHECK(session_participant_set_transport(session->participant_ctx, owned, ACIP_TRANSPORT_OWNED) == ASCIICHAT_OK);
  CHECK(session_participant_start_video(session->participant_ctx) == ASCIICHAT_OK);
  CHECK(session_participant_start_audio(session->participant_ctx) == ASCIICHAT_OK);
  // The same release operation is used before WebRTC recovery and host migration.
  CHECK(release_participant_transport(session) == ASCIICHAT_OK);
  CHECK(!session_participant_get_transport(session->participant_ctx));
  CHECK(release_participant_transport(session) == ASCIICHAT_OK);
  CHECK(transports_destroyed == 2);
  discovery_session_destroy(session);
  session = NULL;
  owned = NULL;
  CHECK(transports_destroyed == 2);
  clean();
  // A real TCP connection uses the same ACIP media send path as borrowed transports.
  config.port = UINT16_MAX + 1;
  session_participant_t *invalid_port = session_participant_create(&config);
  CHECK(invalid_port && session_participant_connect(invalid_port) == ERROR_INVALID_PARAM);
  CHECK(!session_participant_has_transport(invalid_port));
  session_participant_destroy(invalid_port);
  socket_t listener = socket_create("participant-test-listener", AF_INET, SOCK_STREAM, 0);
  CHECK(listener != INVALID_SOCKET_VALUE);
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  CHECK(socket_bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0);
  CHECK(socket_listen(listener, 4) == 0);
  socklen_t address_size = sizeof(address);
  CHECK(socket_getsockname(listener, (struct sockaddr *)&address, &address_size) == 0);
  config.address = "127.0.0.1";
  config.port = NET_TO_HOST_U16(address.sin_port);
  session_participant_t *tcp_participant = session_participant_create(&config);
  fail_tcp_transport = true;
  CHECK(session_participant_connect(tcp_participant) == ERROR_MEMORY);
  CHECK(!session_participant_is_connected(tcp_participant));
  CHECK(!session_participant_has_transport(tcp_participant));
  CHECK(session_participant_get_transport(tcp_participant) == NULL);
  socket_t rejected = socket_accept(listener, NULL, NULL, "rejected-wrapper");
  CHECK(rejected != INVALID_SOCKET_VALUE);
  socket_close(rejected);
  fail_tcp_transport = false;
  for (int cycle = 0; cycle < 2; cycle++) {
    CHECK(session_participant_connect(tcp_participant) == ASCIICHAT_OK);
    acip_transport_t *tcp = session_participant_get_transport(tcp_participant);
    CHECK(tcp && acip_transport_get_type(tcp) == ACIP_TRANSPORT_TCP);
    socket_t owned_socket = acip_transport_get_socket(tcp);
    CHECK(session_participant_connect(tcp_participant) == ASCIICHAT_OK);
    CHECK(session_participant_get_transport(tcp_participant) == tcp);
    acip_transport_t *taken = NULL;
    acip_transport_ownership_t ownership = ACIP_TRANSPORT_BORROWED;
    CHECK(session_participant_take_transport(tcp_participant, &taken, &ownership) == ASCIICHAT_OK);
    CHECK(taken == tcp && ownership == ACIP_TRANSPORT_OWNED);
    CHECK(!session_participant_has_transport(tcp_participant));
    CHECK(session_participant_set_transport(tcp_participant, &transport, ACIP_TRANSPORT_BORROWED) == ASCIICHAT_OK);
    CHECK(session_participant_set_transport(tcp_participant, NULL, ACIP_TRANSPORT_BORROWED) == ASCIICHAT_OK);
    CHECK(!session_participant_has_transport(tcp_participant));
    CHECK(session_participant_set_transport(tcp_participant, taken, ownership) == ASCIICHAT_OK);
    socket_t peer = socket_accept(listener, NULL, NULL, "participant-test-peer");
    CHECK(peer != INVALID_SOCKET_VALUE);
    acip_transport_t *receiver = acip_tcp_transport_create("participant-test-receiver", peer, NULL);
    CHECK(receiver);
    receiver->receive_timeout_ns = 2 * NS_PER_SEC_INT;
    CHECK(session_participant_start_video(tcp_participant) == ASCIICHAT_OK);
    CHECK(session_participant_start_audio(tcp_participant) == ASCIICHAT_OK);
    bool video_received = false, audio_received = false;
    for (int packet = 0; packet < 100 && !(video_received && audio_received); packet++) {
      packet_type_t type;
      void *payload = NULL, *allocation = NULL;
      size_t length = 0;
      CHECK(packet_receive_via_transport(receiver, &type, &payload, &length, &allocation) == ASCIICHAT_OK);
      video_received |= type == PACKET_TYPE_IMAGE_FRAME;
      audio_received |= type == PACKET_TYPE_AUDIO_OPUS_BATCH;
      buffer_release(allocation);
    }
    CHECK(video_received && audio_received);
    session = SAFE_CALLOC(1, sizeof(*session), discovery_session_t *);
    CHECK(mutex_init(&session->nat_quality_mutex, "test_tcp_nat_quality") == ASCIICHAT_OK);
    session->participant_ctx = tcp_participant;
    owned = SAFE_CALLOC(1, sizeof(*owned), acip_transport_t *);
    owned->methods = &methods;
    // A failed restart still leaves the successfully attached transport owned.
    fail_stage = cycle == 1 ? 1 : 0;
    CHECK(select_participant_transport(session, owned) == ASCIICHAT_OK);
    fail_stage = 0;
    CHECK(session_participant_is_video_active(tcp_participant) == (cycle == 0));
    CHECK(session_participant_is_audio_active(tcp_participant));
    int audio_before_switch = atomic_load_int(&audio_packets);
    int video_before_switch = atomic_load_int(&video_packets);
    for (int i = 0; i < 200 && (atomic_load_int(&audio_packets) <= audio_before_switch ||
                                (cycle == 0 && atomic_load_int(&video_packets) <= video_before_switch));
         ++i)
      platform_sleep_ms(1);
    CHECK(atomic_load_int(&audio_packets) > audio_before_switch);
    CHECK(cycle == 1 || atomic_load_int(&video_packets) > video_before_switch);
    CHECK(session->host_control_transport == tcp);
    CHECK(session->host_control_ownership == ACIP_TRANSPORT_OWNED);
    CHECK(session_participant_get_transport(tcp_participant) == owned);
    int destroyed_before_release = transports_destroyed;
    CHECK(release_participant_transport(session) == ASCIICHAT_OK);
    CHECK(transports_destroyed == destroyed_before_release + 1);
    CHECK(!session_participant_has_transport(tcp_participant));
    CHECK(session->host_control_transport == tcp);
    if (cycle == 0) {
      session->participant_ctx = NULL;
      session_participant_disconnect(tcp_participant);
      CHECK(!session_participant_has_transport(tcp_participant));
      CHECK(session_participant_get_transport(tcp_participant) == NULL);
    }
    discovery_session_destroy(session);
    session = NULL;
    struct sockaddr_storage closed_address;
    socklen_t closed_size = sizeof(closed_address);
    CHECK(socket_getsockname(owned_socket, (struct sockaddr *)&closed_address, &closed_size) != 0);
    acip_transport_destroy(receiver);
    socket_close(peer);
    clean();
  }
  tcp_participant = NULL;
  socket_close(listener);
  // Async host startup tokens cannot reactivate a stopped or replaced stream.
  session_av_state_t *state = session_av_state_create();
  CHECK(state && session_av_state_generation(state) == 0);
  uint32_t old = session_av_state_begin(state);
  CHECK(old && !session_av_state_is_active(state) && !session_av_state_begin(state));
  session_av_state_begin_stop(state);
  CHECK(!session_av_state_complete(state, old));
  session_av_state_finish_stop(state);
  uint32_t next = session_av_state_begin(state);
  CHECK(next != old && !session_av_state_complete(state, old));
  CHECK(session_av_state_complete(state, next) && session_av_state_is_active(state));
  session_av_state_begin_stop(state);
  session_av_state_finish_stop(state);
  CHECK(session_av_state_generation(state) == next);
  session_av_state_destroy(state);
  image_destroy(frame);
  options_state_destroy();
  log_info("PASS: six startup failures, 20 real-worker cycles, media packets, cleanup, async cancellation");
  return 0;
}
