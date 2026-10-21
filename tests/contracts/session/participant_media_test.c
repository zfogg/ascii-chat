#include <criterion/criterion.h>
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
#include <ascii-chat/network/connection.h>
#include <ascii-chat/network/negotiation.h>
#include <ascii-chat/util/time.h>
#include <stdlib.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/buffer.h>

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
  cr_assert_eq(workers, 0);
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
  cr_assert(workers <= 1); // Video may still be running; audio has already joined.
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
  cr_assert(codec);
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
static asciichat_error_t test_connection_open(const char *name, const char *address, uint16_t port,
                                              const network_connect_config_t *settings, network_connection_t **out,
                                              connection_endpoint_t *endpoint) {
  if (fail_tcp_transport)
    return SET_ERRNO(ERROR_MEMORY, "Injected connection allocation failure");
  return network_connection_open(name, address, port, settings, out, endpoint);
}
static asciichat_error_t fixture_start(asciichat_thread_t *thread, void *(*fn)(void *), void *context) {
  return asciichat_thread_create(thread, "participant_fixture", fn, context);
}
static asciichat_error_t fixture_join(asciichat_thread_t *thread) {
  return asciichat_thread_join(thread, NULL);
}
#define network_connection_open test_connection_open
static void test_capture_audio(session_capture_ctx_t *ctx, void *audio) {
  (void)ctx;
  (void)audio;
}
static void *test_audio_context(session_audio_ctx_t *ctx) {
  (void)ctx;
  return NULL;
}

static uint64_t test_capture_epoch(session_capture_ctx_t *ctx) {
  (void)ctx;
  return 0;
}
static void test_capture_complete_open(session_capture_ctx_t *ctx, uint64_t epoch) {
  (void)ctx;
  (void)epoch;
}

#define session_capture_source_epoch test_capture_epoch
#define session_capture_complete_open test_capture_complete_open
#define session_capture_set_audio_context test_capture_audio
#define session_audio_get_context test_audio_context
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
  cr_assert(data && len);
  atomic_fetch_add_int(&packets, 1);
  cr_assert(len >= sizeof(packet_header_t));
  const packet_header_t *header = data;
  uint16_t type = NET_TO_HOST_U16(header->type);
  if (type == PACKET_TYPE_IMAGE_FRAME)
    atomic_fetch_add_int(&video_packets, 1);
  if (type == PACKET_TYPE_AUDIO_OPUS_BATCH)
    atomic_fetch_add_int(&audio_packets, 1);
  return ASCIICHAT_OK;
}
static void clean(void) {
  cr_assert(!workers && !capture_count && !audio_count && !codec_count);
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

typedef struct {
  network_server_t *server;
  atomic_t video, audio, closed;
  const char *password;
} network_fixture_t;
static asciichat_error_t fixture_open(network_connection_t *c, void *arg) {
  network_fixture_t *fixture = arg;
  network_negotiation_t *n = SAFE_CALLOC(1, sizeof(*n), network_negotiation_t *);
  network_connection_set_context(c, n);
  asciichat_error_t result = network_connection_prepare_security(c, true, fixture->password);
  if (result != ASCIICHAT_OK)
    return result;
  *n = (network_negotiation_t){.protocol = NETWORK_PROTOCOL_MEDIA,
                               .handshake = network_connection_handshake(c),
                               .initialized = true,
                               .forbid_encryption = true,
                               .password = fixture->password,
                               .require_auth = true};
  return ASCIICHAT_OK;
}
static asciichat_error_t fixture_packet(network_connection_t *c, packet_type_t type, const void *data, size_t len,
                                        void *arg) {
  network_fixture_t *f = arg;
  if (network_connection_state(c) == NETWORK_NEGOTIATING) {
    network_negotiation_t *n = network_connection_context(c);
    asciichat_error_t result = network_negotiation_receive(n, network_connection_transport(c), type, data, len);
    if (result == ASCIICHAT_OK && n->ready)
      network_connection_ready(c);
    return result;
  }
  if (type == PACKET_TYPE_IMAGE_FRAME)
    atomic_fetch_add_int(&f->video, 1);
  if (type == PACKET_TYPE_AUDIO_OPUS_BATCH)
    atomic_fetch_add_int(&f->audio, 1);
  return ASCIICHAT_OK;
}
static void fixture_closed(network_connection_t *c, void *arg) {
  atomic_fetch_add_int(&((network_fixture_t *)arg)->closed, 1);
  network_negotiation_t *n = network_connection_context(c);
  SAFE_FREE(n);
}
static void *fixture_run(void *arg) {
  network_server_run(((network_fixture_t *)arg)->server);
  return NULL;
}

Test(participant_media, startup_failures_and_worker_lifecycle, .timeout = 60) {
  log_init(NULL, LOG_WARN, false, false);
  log_set_terminal_output(true);
  cr_assert_eq(platform_setenv("ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK", "1"), 0);
  cr_assert_eq(socket_init(), ASCIICHAT_OK);
  options_t options = options_t_new();
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  cr_assert_eq(options_state_set(&options), ASCIICHAT_OK);
  frame = image_new(2, 2);
  cr_assert(frame);
  acip_transport_methods_t methods = {.send = test_transport_send, .is_connected = connected};
  acip_transport_t transport = {.methods = &methods};
  session_participant_config_t config = {.enable_video = true, .enable_audio = true};
  // Refusing cleanup on another thread must retain even a workerless owner.
  session_participant_t *retained = session_participant_create(&config);
  cr_assert_eq(session_participant_set_transport(retained, &transport, ACIP_TRANSPORT_BORROWED), ASCIICHAT_OK);
  asciichat_thread_t foreign;
  cr_assert_eq(asciichat_thread_create(&foreign, "foreign_destroy", destroy_from_foreign_thread, retained),
               ASCIICHAT_OK);
  cr_assert_eq(asciichat_thread_join(&foreign, NULL), ASCIICHAT_OK);
  cr_assert(retained->initialized && session_participant_get_transport(retained) == &transport);
  cr_assert_eq(atomic_load_int(&retained->connection.state), SESSION_CONNECTION_STOPPING);
  session_participant_destroy(retained);
  clean();
  cr_assert_eq(session_participant_start_video(NULL), ERROR_INVALID_PARAM);
  session_participant_destroy(NULL);
  for (fail_stage = 1; fail_stage <= 6; fail_stage++) {
    session_participant_t *p = session_participant_create(&config);
    cr_assert(p);
    cr_assert_eq(session_participant_start_video(p), ERROR_INVALID_STATE);
    cr_assert_eq(session_participant_set_transport(p, &transport, ACIP_TRANSPORT_BORROWED), ASCIICHAT_OK);
    asciichat_error_t error = fail_stage <= 2 ? session_participant_start_video(p) : session_participant_start_audio(p);
    cr_assert_eq(error, (fail_stage == 1 || fail_stage == 3 || fail_stage == 5 ? ERROR_MEMORY
                         : fail_stage == 2 || fail_stage == 6                  ? ERROR_THREAD
                                                                               : ERROR_INVALID_STATE));
    cr_assert(!session_participant_is_video_active(p) && !session_participant_is_audio_active(p));
    clean();
    session_participant_disconnect(p);
    session_participant_disconnect(p);
    session_participant_destroy(p);
    clean();
  }
  fail_stage = 0;
  for (int cycle = 0; cycle < 20; cycle++) {
    session_participant_t *p = session_participant_create(&config);
    cr_assert_eq(session_participant_set_transport(p, &transport, ACIP_TRANSPORT_BORROWED), ASCIICHAT_OK);
    cr_assert_eq(session_participant_start_video(p), ASCIICHAT_OK);
    cr_assert_eq(session_participant_start_video(p), ASCIICHAT_OK);
    cr_assert_eq(session_participant_start_audio(p), ASCIICHAT_OK);
    cr_assert_eq(session_participant_start_audio(p), ASCIICHAT_OK);
    cr_assert(workers == 2 && session_participant_is_video_active(p) && session_participant_is_audio_active(p));
    cr_assert_eq(session_participant_set_transport(p, NULL, ACIP_TRANSPORT_BORROWED), ERROR_INVALID_STATE);
    network_connection_t *unchanged = (network_connection_t *)&transport;
    cr_assert_eq(session_participant_take_connection(p, &unchanged), ERROR_INVALID_STATE);
    cr_assert_eq(unchanged, (network_connection_t *)&transport);
    int before = atomic_load_int(&packets);
    for (int i = 0; i < 200 && atomic_load_int(&packets) < before + 4; i++)
      platform_sleep_ms(1);
    cr_assert(atomic_load_int(&packets) >= before + 4);
    if (cycle % 3 == 0) {
      session_participant_stop_video(p); // Joins audio before releasing its borrowed source.
      clean();
      session_participant_stop_video(p);
      cr_assert_eq(session_participant_start_video(p), ASCIICHAT_OK);
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
    cr_assert(!session_participant_is_video_active(p) && !session_participant_is_audio_active(p));
    session_participant_destroy(p);
    clean();
  }
  cr_assert(atomic_load_int(&video_packets) > 0 && atomic_load_int(&audio_packets) > 0);
  // Discovery retains a borrowed transport until all workers join, then frees
  // it exactly once on teardown. A failed join permits retry without dangling pointers.
  discovery_session_t *session = SAFE_CALLOC(1, sizeof(*session), discovery_session_t *);
  cr_assert_eq(mutex_init(&session->nat_quality_mutex, "test_nat_quality"), ASCIICHAT_OK);
  session->participant_ctx = session_participant_create(&config);
  acip_transport_t *owned = SAFE_CALLOC(1, sizeof(*owned), acip_transport_t *);
  methods.destroy_impl = destroy_transport_impl;
  owned->methods = &methods;
  cr_assert_eq(session_participant_set_transport(session->participant_ctx, owned, ACIP_TRANSPORT_OWNED), ASCIICHAT_OK);
  cr_assert_eq(session_participant_start_video(session->participant_ctx), ASCIICHAT_OK);
  cr_assert_eq(session_participant_start_audio(session->participant_ctx), ASCIICHAT_OK);
  fail_join = true;
  discovery_session_destroy(session);
  cr_assert(transports_destroyed == 0 && workers == 2);
  cr_assert_eq(session_participant_get_transport(session->participant_ctx), owned);
  fail_join = false;
  discovery_session_destroy(session);
  session = NULL;
  owned = NULL;
  cr_assert_eq(transports_destroyed, 1);
  clean();
  session = SAFE_CALLOC(1, sizeof(*session), discovery_session_t *);
  cr_assert_eq(mutex_init(&session->nat_quality_mutex, "test_nat_quality"), ASCIICHAT_OK);
  session->participant_ctx = session_participant_create(&config);
  owned = SAFE_CALLOC(1, sizeof(*owned), acip_transport_t *);
  owned->methods = &methods;
  cr_assert_eq(session_participant_set_transport(session->participant_ctx, owned, ACIP_TRANSPORT_OWNED), ASCIICHAT_OK);
  cr_assert_eq(session_participant_start_video(session->participant_ctx), ASCIICHAT_OK);
  cr_assert_eq(session_participant_start_audio(session->participant_ctx), ASCIICHAT_OK);
  // The same release operation is used before WebRTC recovery and host migration.
  cr_assert_eq(release_participant_transport(session), ASCIICHAT_OK);
  cr_assert(!session_participant_get_transport(session->participant_ctx));
  cr_assert_eq(release_participant_transport(session), ASCIICHAT_OK);
  cr_assert_eq(transports_destroyed, 2);
  discovery_session_destroy(session);
  session = NULL;
  owned = NULL;
  cr_assert_eq(transports_destroyed, 2);
  clean();
  // Exercise real establishment, negotiation, media and retained fallback ownership.
  options.no_encrypt = true;
  cr_assert_eq(options_state_set(&options), ASCIICHAT_OK);
  socket_t probe = socket_create("participant-port", AF_INET, SOCK_STREAM, 0);
  cr_assert_neq(probe, INVALID_SOCKET_VALUE);
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = HOST_TO_NET_U32(INADDR_LOOPBACK)};
  cr_assert_eq(socket_bind(probe, (struct sockaddr *)&address, sizeof(address)), 0);
  socklen_t address_size = sizeof(address);
  cr_assert_eq(socket_getsockname(probe, (struct sockaddr *)&address, &address_size), 0);
  socket_close(probe);
  config.address = "127.0.0.1";
  config.port = NET_TO_HOST_U16(address.sin_port);
  config.password = "participant-fixture-password";
  network_fixture_t fixture = {.password = config.password};
  network_server_config_t server_config = {
      .port = config.port,
      .ipv4_address = "127.0.0.1",
      .bind_ipv4 = true,
      .callbacks = {.open = fixture_open, .packet = fixture_packet, .closed = fixture_closed},
      .user_data = &fixture};
  cr_assert_eq(network_server_create(&server_config, &fixture.server), ASCIICHAT_OK);
  cr_assert_eq(network_server_start(fixture.server), ASCIICHAT_OK);
  asciichat_thread_t server_thread;
  cr_assert_eq(fixture_start(&server_thread, fixture_run, &fixture), ASCIICHAT_OK);
  session_participant_t *tcp_participant = session_participant_create(&config);
  fail_tcp_transport = true;
  cr_assert_eq(session_participant_connect(tcp_participant), ERROR_MEMORY);
  cr_assert(!session_participant_is_connected(tcp_participant));
  cr_assert(!session_participant_has_transport(tcp_participant));
  fail_tcp_transport = false;
  for (int cycle = 0; cycle < 2; cycle++) {
    cr_assert_eq(session_participant_connect(tcp_participant), ASCIICHAT_OK);
    acip_transport_t *tcp = session_participant_get_transport(tcp_participant);
    cr_assert(tcp && acip_transport_get_type(tcp) == ACIP_TRANSPORT_TCP);
    cr_assert_eq(session_participant_connect(tcp_participant), ASCIICHAT_OK);
    cr_assert_eq(session_participant_get_transport(tcp_participant), tcp);
    network_connection_t *taken = NULL;
    cr_assert_eq(session_participant_take_connection(tcp_participant, &taken), ASCIICHAT_OK);
    cr_assert_eq(network_connection_transport(taken), tcp);
    cr_assert(!session_participant_has_transport(tcp_participant));
    cr_assert_eq(session_participant_set_transport(tcp_participant, &transport, ACIP_TRANSPORT_BORROWED), ASCIICHAT_OK);
    cr_assert_eq(session_participant_set_connection(tcp_participant, &taken), ASCIICHAT_OK);
    cr_assert(!taken);
    int video_before = atomic_load_int(&fixture.video), audio_before = atomic_load_int(&fixture.audio);
    cr_assert_eq(session_participant_start_video(tcp_participant), ASCIICHAT_OK);
    cr_assert_eq(session_participant_start_audio(tcp_participant), ASCIICHAT_OK);
    for (int i = 0; i < 2000 && (atomic_load_int(&fixture.video) == video_before ||
                                 atomic_load_int(&fixture.audio) == audio_before);
         ++i)
      platform_sleep_ms(1);
    cr_assert(atomic_load_int(&fixture.video) > video_before && atomic_load_int(&fixture.audio) > audio_before);
    session = SAFE_CALLOC(1, sizeof(*session), discovery_session_t *);
    cr_assert_eq(mutex_init(&session->nat_quality_mutex, "test_tcp_nat_quality"), ASCIICHAT_OK);
    session->participant_ctx = tcp_participant;
    owned = SAFE_CALLOC(1, sizeof(*owned), acip_transport_t *);
    owned->methods = &methods;
    // A failed restart still leaves the successfully attached transport owned.
    fail_stage = cycle == 1 ? 1 : 0;
    cr_assert_eq(select_participant_transport(session, owned), ASCIICHAT_OK);
    fail_stage = 0;
    cr_assert_eq(session_participant_is_video_active(tcp_participant), (cycle == 0));
    cr_assert(session_participant_is_audio_active(tcp_participant));
    int audio_before_switch = atomic_load_int(&audio_packets);
    int video_before_switch = atomic_load_int(&video_packets);
    for (int i = 0; i < 200 && (atomic_load_int(&audio_packets) <= audio_before_switch ||
                                (cycle == 0 && atomic_load_int(&video_packets) <= video_before_switch));
         ++i)
      platform_sleep_ms(1);
    cr_assert(atomic_load_int(&audio_packets) > audio_before_switch);
    cr_assert(cycle == 1 || atomic_load_int(&video_packets) > video_before_switch);
    cr_assert_eq(session->host_control_transport, tcp);
    cr_assert(session->host_control_connection);
    cr_assert_eq(session_participant_get_transport(tcp_participant), owned);
    int destroyed_before_release = transports_destroyed;
    cr_assert_eq(release_participant_transport(session), ASCIICHAT_OK);
    cr_assert_eq(transports_destroyed, destroyed_before_release + 1);
    cr_assert(!session_participant_has_transport(tcp_participant));
    cr_assert_eq(session->host_control_transport, tcp);
    if (cycle == 0) {
      session->participant_ctx = NULL;
      session_participant_disconnect(tcp_participant);
      cr_assert(!session_participant_has_transport(tcp_participant));
    }
    discovery_session_destroy(session);
    session = NULL;
    uint64_t close_deadline = time_get_ns() + 2 * NS_PER_SEC_INT;
    while (atomic_load_int(&fixture.closed) < cycle + 1 && time_get_ns() < close_deadline)
      platform_sleep_ms(1);
    cr_assert_eq(atomic_load_int(&fixture.closed), cycle + 1);
    clean();
  }
  tcp_participant = NULL;
  network_server_request_stop(fixture.server);
  cr_assert_eq(fixture_join(&server_thread), ASCIICHAT_OK);
  cr_assert_eq(network_server_stop(fixture.server), ASCIICHAT_OK);
  network_server_destroy(&fixture.server);
  image_destroy(frame);
  options_state_destroy();
  log_info("PASS: six startup failures, 20 real-worker cycles, media packets, cleanup, async cancellation");
}

Test(participant_media, stale_startup_tokens_cannot_reactivate_streams) {
  // Async host startup tokens cannot reactivate a stopped or replaced stream.
  session_av_state_t *state = session_av_state_create();
  cr_assert(state && session_av_state_generation(state) == 0);
  uint32_t old = session_av_state_begin(state);
  cr_assert(old && !session_av_state_is_active(state) && !session_av_state_begin(state));
  session_av_state_begin_stop(state);
  cr_assert(!session_av_state_complete(state, old));
  session_av_state_finish_stop(state);
  uint32_t next = session_av_state_begin(state);
  cr_assert(next != old && !session_av_state_complete(state, old));
  cr_assert(session_av_state_complete(state, next) && session_av_state_is_active(state));
  session_av_state_begin_stop(state);
  session_av_state_finish_stop(state);
  cr_assert_eq(session_av_state_generation(state), next);
  session_av_state_destroy(state);
}
