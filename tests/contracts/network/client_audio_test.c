#include <criterion/criterion.h>
#include "../../../src/client/connection_internal.h"
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/thread_pool.h>
#include <ascii-chat/audio/opus.h>
#include <ascii-chat/network/acip/backpressure.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>

static const char *fail_checkpoint, *fail_worker;
static unsigned scheduled;
static asciichat_error_t schedule_worker(thread_pool_t *pool, void *(*fn)(void *), void *arg, int order,
                                         const char *name) {
  (void)pool;
  (void)fn;
  (void)arg;
  (void)order;
  ++scheduled;
  return fail_worker && strcmp(fail_worker, name) == 0 ? ERROR_THREAD : ASCIICHAT_OK;
}
asciichat_error_t client_connection_checkpoint(const char *stage) {
  return fail_checkpoint && strcmp(stage, fail_checkpoint) == 0 ? ERROR_THREAD : ASCIICHAT_OK;
}
atomic_t g_should_exit;
bool should_exit(void) {
  return atomic_load_bool(&g_should_exit);
}
void signal_exit(void) {
  atomic_store_bool(&g_should_exit, true);
}
#define thread_pool_spawn schedule_worker
#include "../../../src/client/audio.c"
#undef thread_pool_spawn
static int capture_start_result, keepalive_start_result;
static unsigned capture_starts, keepalive_starts;
static int protocol_capture_start(client_connection_t *connection) {
  (void)connection;
  ++capture_starts;
  return capture_start_result;
}
static int protocol_keepalive_start(client_connection_t *connection) {
  (void)connection;
  ++keepalive_starts;
  return keepalive_start_result;
}
#define thread_pool_spawn schedule_worker
#define capture_start_thread protocol_capture_start
#define keepalive_start_thread protocol_keepalive_start
#include "../../../src/client/protocol_adapter.c"
#undef thread_pool_spawn
#undef capture_start_thread
#undef keepalive_start_thread
webrtc_peer_manager_t *g_peer_manager;

static client_connection_t audio_connection;
static atomic_t sent;
static unsigned stop_after, send_fail_on, receive_calls;
static bool receive_remote_error;
static asciichat_error_t network_result;
static bool first_local_failure;
static uint8_t sent_payload[16384];
static size_t sent_length;
static asciichat_error_t audio_send(acip_transport_t *t, const void *data, size_t length) {
  (void)t;
  unsigned n = atomic_fetch_add_int(&sent, 1) + 1;
  if (length < sizeof(packet_header_t) || length - sizeof(packet_header_t) > sizeof(sent_payload))
    return ERROR_NETWORK_SIZE;
  sent_length = length - sizeof(packet_header_t);
  memcpy(sent_payload, (const uint8_t *)data + sizeof(packet_header_t), sent_length);
  if (stop_after && n >= stop_after) {
    atomic_store_bool(&audio_connection.audio_sender_should_exit, true);
    atomic_store_bool(&g_should_exit, true);
  }
  if (first_local_failure && n == 1)
    return ERROR_INVALID_PARAM;
  return send_fail_on && n != send_fail_on ? ASCIICHAT_OK : network_result;
}
static bool audio_connected(acip_transport_t *t) {
  (void)t;
  return true;
}
static asciichat_error_t audio_receive(acip_transport_t *t, void **data, size_t *length, void **allocation) {
  (void)t;
  ++receive_calls;
  if (!receive_remote_error)
    return ERROR_NETWORK;
  error_packet_t error = {.error_code = HOST_TO_NET_U32(ERROR_INVALID_PASSWORD), .message_length = 0};
  packet_header_t header = {.magic = HOST_TO_NET_U64(PACKET_MAGIC),
                            .type = HOST_TO_NET_U16(PACKET_TYPE_ERROR_MESSAGE),
                            .length = HOST_TO_NET_U32(sizeof(error)),
                            .crc32 = HOST_TO_NET_U32(asciichat_crc32(&error, sizeof(error)))};
  *length = sizeof(header) + sizeof(error);
  *allocation = buffer_alloc(NULL, *length);
  if (!*allocation)
    return ERROR_MEMORY;
  memcpy(*allocation, &header, sizeof(header));
  memcpy((uint8_t *)*allocation + sizeof(header), &error, sizeof(error));
  *data = *allocation;
  return ASCIICHAT_OK;
}
static const acip_transport_methods_t audio_methods = {
    .send = audio_send, .recv = audio_receive, .is_connected = audio_connected};
static acip_transport_t audio_transport = {.methods = &audio_methods};
static void audio_contract_setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = options_t_new();
  v->audio_enabled = true;
  v->test_audio = true;
  v->audio_no_playback = true;
  cr_assert_eq(options_state_set(v), ASCIICHAT_OK);
  SAFE_FREE(v);
  cr_assert_eq(server_connection_init(&audio_connection), ASCIICHAT_OK);
  server_connection_set_transport(&audio_connection, &audio_transport);
  network_result = ASCIICHAT_OK;
}
static void audio_contract_teardown(void) {
  audio_sender_cleanup(&audio_connection);
  audio_sender_finalize(&audio_connection);
  if (audio_connection.audio_pipeline) {
    audio_set_pipeline(audio_connection.audio_context, NULL);
    client_audio_pipeline_destroy(audio_connection.audio_pipeline);
  }
  if (audio_connection.capture_context)
    session_capture_destroy(audio_connection.capture_context);
  audio_cleanup();
  mutex_destroy(&audio_connection.send_mutex);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(client_audio_contract, .init = audio_contract_setup, .fini = audio_contract_teardown, .timeout = 20);
static void queue_failure(const char *stage) {
  fail_checkpoint = stage;
  cr_assert_eq(audio_sender_init(&audio_connection), ERROR_THREAD);
  cr_assert(audio_connection.audio_queue_mutex_initialized);
  audio_sender_cleanup(&audio_connection);
  audio_sender_finalize(&audio_connection);
  cr_assert_not(audio_connection.audio_queue_mutex_initialized);
  cr_assert_not(audio_connection.audio_queue_cond_initialized);
  audio_sender_finalize(&audio_connection);
}
Test(client_audio_contract, queue_partial_mutex_initialization_is_releasable) {
  queue_failure("audio-queue-mutex");
}
Test(client_audio_contract, queue_partial_condition_initialization_is_releasable) {
  queue_failure("audio-queue");
}
Test(client_audio_contract, sender_start_failure_retains_queue_until_owner_finalization) {
  fail_worker = "audio_sender";
  cr_assert_eq(audio_sender_init(&audio_connection), ERROR_THREAD);
  cr_assert_not(audio_connection.audio_sender_thread_created);
  cr_assert(audio_connection.audio_queue_cond_initialized);
}
Test(client_audio_contract, queue_owns_payload_and_frame_sizes_and_applies_bounded_backpressure) {
  uint8_t encoded[3] = {1, 2, 3};
  uint16_t sizes[2] = {1, 2};
  cr_assert_eq(audio_queue_packet(&audio_connection, encoded, 3, sizes, 2), -1);
  cr_assert_eq(audio_sender_init(&audio_connection), ASCIICHAT_OK);
  cr_assert_eq(audio_queue_packet(&audio_connection, NULL, 3, sizes, 2), -1);
  cr_assert_eq(audio_queue_packet(&audio_connection, encoded, 0, sizes, 2), -1);
  cr_assert_eq(
      audio_queue_packet(&audio_connection, encoded, sizeof(audio_connection.audio_send_queue[0].data) + 1, sizes, 2),
      -1);
  cr_assert_eq(audio_connection.audio_send_queue_head, 0);
  for (unsigned i = 0; i < AUDIO_SEND_QUEUE_SIZE - 1; ++i)
    cr_assert_eq(audio_queue_packet(&audio_connection, encoded, 3, sizes, 2), 0);
  cr_assert_eq(audio_queue_packet(&audio_connection, encoded, 3, sizes, 2), -1);
  memset(encoded, 0, sizeof(encoded));
  memset(sizes, 0, sizeof(sizes));
  stop_after = 1;
  audio_sender_thread_func(&audio_connection);
  cr_assert_eq(atomic_load_int(&sent), 1);
  cr_assert(atomic_load_bool(&audio_connection.audio_sender_exited));
  cr_assert_eq(sent_length, 23);
  cr_assert_arr_eq(sent_payload + 20, ((uint8_t[]){1, 2, 3}), 3);
  uint16_t wire[2];
  memcpy(wire, sent_payload + 16, sizeof(wire));
  cr_assert_eq(NET_TO_HOST_U16(wire[0]), 1);
  cr_assert_eq(NET_TO_HOST_U16(wire[1]), 2);
  atomic_store_bool(&audio_connection.audio_sender_should_exit, false);
  atomic_store_bool(&g_should_exit, false);
  cr_assert_eq(audio_queue_packet(&audio_connection, (uint8_t[]){4}, 1, (uint16_t[]){1}, 1), 0);
  cr_assert_eq(audio_connection.audio_send_queue_head, 0);
}
Test(client_audio_contract, sender_continues_after_local_rejection_and_exits_after_network_failure) {
  cr_assert_eq(audio_sender_init(&audio_connection), ASCIICHAT_OK);
  for (unsigned i = 0; i < 2; ++i)
    cr_assert_eq(audio_queue_packet(&audio_connection, (uint8_t[]){1}, 1, (uint16_t[]){1}, 1), 0);
  first_local_failure = true;
  stop_after = 2;
  audio_sender_thread_func(&audio_connection);
  cr_assert_eq(atomic_load_int(&sent), 2);
  cr_assert_not(server_connection_is_lost(&audio_connection));
  first_local_failure = false;
  stop_after = 0;
  network_result = ERROR_NETWORK;
  atomic_store_bool(&audio_connection.audio_sender_should_exit, false);
  atomic_store_bool(&g_should_exit, false);
  cr_assert_eq(audio_queue_packet(&audio_connection, (uint8_t[]){1}, 1, (uint16_t[]){1}, 1), 0);
  audio_sender_thread_func(&audio_connection);
  cr_assert(server_connection_is_lost(&audio_connection));
  cr_assert(atomic_load_bool(&audio_connection.audio_sender_exited));
}
Test(client_audio_contract, received_pcm_checks_bounds_and_delivers_non_silent_audio_to_owned_playback) {
  cr_assert_eq(audio_client_init(), 0);
  audio_connection.audio_context = audio_get_context();
  cr_assert(audio_connection.audio_context->initialized);
  cr_assert_eq(audio_client_init(), 0);
  float samples[512];
  for (unsigned i = 0; i < ARRAY_SIZE(samples); ++i)
    samples[i] = i & 1 ? 0.25f : -0.25f;
  audio_process_received_samples(NULL, samples, 512);
  cr_assert_eq(GET_ERRNO(), ERROR_INVALID_PARAM);
  CLEAR_ERRNO_ALL();
  audio_process_received_samples(&audio_connection, NULL, 512);
  cr_assert_eq(GET_ERRNO(), ERROR_INVALID_PARAM);
  CLEAR_ERRNO_ALL();
  audio_process_received_samples(&audio_connection, samples, 0);
  cr_assert_eq(GET_ERRNO(), ERROR_INVALID_PARAM);
  CLEAR_ERRNO_ALL();
  audio_process_received_samples(&audio_connection, samples, AUDIO_BATCH_SAMPLES + 1);
  cr_assert_eq(audio_ring_buffer_available_read(audio_connection.audio_context->playback_buffer), 0);
  audio_ring_buffer_set_jitter_target(audio_connection.audio_context->playback_buffer, 960);
  audio_process_received_samples(&audio_connection, samples, 512);
  cr_assert_eq(audio_connection.audio_receive_count, 1);
  audio_process_received_samples(&audio_connection, samples, 512);
  cr_assert_eq(audio_connection.audio_receive_count, 2);
  cr_assert_gt(audio_ring_buffer_available_read(audio_connection.audio_context->playback_buffer), 0);
  float actual[512];
  cr_assert_eq(audio_ring_buffer_read(audio_connection.audio_context->playback_buffer, actual, 512), 512);
  double energy = 0;
  for (unsigned i = 0; i < ARRAY_SIZE(actual); ++i)
    energy += actual[i] * actual[i];
  cr_assert_gt(energy, 0.001);
}
Test(client_audio_contract, pipeline_decodes_real_opus_and_capture_stop_preserves_application_audio_until_cleanup) {
  cr_assert_eq(audio_client_init(), 0);
  audio_connection.audio_context = audio_get_context();
  session_capture_config_t config = {.type = MEDIA_SOURCE_TEST, .target_fps = 60};
  audio_connection.capture_context = session_mirror_capture_create(&config);
  cr_assert_not_null(audio_connection.capture_context);
  float decoded[OPUS_FRAME_SAMPLES];
  uint8_t encoded[1024];
  cr_assert_eq(audio_decode_opus(&audio_connection, encoded, 1, decoded, ARRAY_SIZE(decoded)), -1);
  cr_assert_eq(audio_connection_init(&audio_connection), ASCIICHAT_OK);
  cr_assert_not_null(audio_get_pipeline(&audio_connection));
  cr_assert_null(audio_get_pipeline(NULL));
  cr_assert_eq(audio_decode_opus(&audio_connection, encoded, 1, NULL, 960), -1);
  cr_assert_eq(audio_decode_opus(&audio_connection, encoded, 1, decoded, 0), -1);
  opus_codec_t *encoder = opus_codec_create_encoder(OPUS_APPLICATION_VOIP, 48000, 24000);
  cr_assert_not_null(encoder);
  float pcm[960];
  for (unsigned i = 0; i < ARRAY_SIZE(pcm); ++i)
    pcm[i] = i % 32 < 16 ? 0.2f : -0.2f;
  int length = opus_codec_encode(encoder, pcm, 960, encoded, sizeof(encoded));
  cr_assert_gt(length, 0);
  cr_assert_eq(audio_decode_opus(&audio_connection, encoded, length, decoded, 960), 960);
  opus_codec_destroy(encoder);
  fail_worker = "audio_capture";
  cr_assert_eq(audio_start_thread(&audio_connection), -1);
  cr_assert_not(audio_connection.audio_capture_thread_created);
  fail_worker = NULL;
  cr_assert_eq(audio_start_thread(&audio_connection), 0);
  unsigned before = scheduled;
  cr_assert_eq(audio_start_thread(&audio_connection), 0);
  cr_assert_eq(scheduled, before);
  atomic_store_bool(&audio_connection.audio_capture_thread_exited, true);
  audio_stop_thread(&audio_connection);
  cr_assert_not(audio_connection.audio_capture_thread_created);
  cr_assert(audio_connection.audio_context->initialized);
  cr_assert(audio_thread_exited(&audio_connection));
  audio_stop_thread(NULL);
}
Test(client_audio_contract, capture_worker_encodes_oversized_input_and_flushes_a_partial_batch_after_idle) {
  cr_assert_eq(audio_client_init(), 0);
  audio_connection.audio_context = audio_get_context();
  session_capture_config_t config = {.type = MEDIA_SOURCE_TEST, .target_fps = 60};
  audio_connection.capture_context = session_mirror_capture_create(&config);
  cr_assert_not_null(audio_connection.capture_context);
  cr_assert_eq(audio_connection_init(&audio_connection), ASCIICHAT_OK);
  audio_connection.audio_send_direct = true;
  float pcm[OPUS_FRAME_SAMPLES];
  for (unsigned i = 0; i < ARRAY_SIZE(pcm); ++i)
    pcm[i] = i % 32 < 16 ? 2.0f : -2.0f;
  cr_assert_eq(audio_ring_buffer_write(audio_connection.audio_context->capture_buffer, pcm, ARRAY_SIZE(pcm)),
               ASCIICHAT_OK);
  stop_after = 1;
  asciichat_thread_t worker;
  cr_assert_eq(acthread_create(&worker, "audio-contract-capture", audio_capture_thread_func, &audio_connection),
               ASCIICHAT_OK);
  uint64_t deadline = time_get_ns() + 3 * NS_PER_SEC_INT;
  while (!atomic_load_bool(&audio_connection.audio_capture_thread_exited) && time_get_ns() < deadline)
    platform_sleep_ms(1);
  atomic_store_bool(&g_should_exit, true);
  cr_assert_eq(thread_join(&worker, NULL), ASCIICHAT_OK);
  cr_assert_eq(atomic_load_int(&sent), 1);
  cr_assert_geq(sent_length, 19);
  uint32_t frames;
  memcpy(&frames, sent_payload + 8, sizeof(frames));
  cr_assert_eq(NET_TO_HOST_U32(frames), 1);
  cr_assert_not(server_connection_is_lost(&audio_connection));
}

Test(client_audio_contract, protocol_startup_retains_resources_for_owner_cleanup_at_each_checkpoint) {
  const char *stages[] = {"audio-pipeline", "receive",      "capture",       "audio-duplex", "audio-queue-mutex",
                          "audio-queue",    "audio-sender", "audio-capture", "keepalive"};
  for (unsigned i = 0; i < ARRAY_SIZE(stages); ++i) {
    fail_checkpoint = stages[i];
    cr_assert_eq(client_protocol_start(&audio_connection), ERROR_THREAD, "stage %s", stages[i]);
    audio_sender_cleanup(&audio_connection);
    audio_sender_finalize(&audio_connection);
    cr_assert_not(audio_connection.audio_queue_mutex_initialized);
    cr_assert_not(audio_connection.audio_queue_cond_initialized);
    audio_connection.audio_send_queue_lc = (lifecycle_t)LIFECYCLE_INIT;
    CLEAR_ERRNO_ALL();
  }
  fail_checkpoint = NULL;
  cr_assert_eq(client_protocol_start(&audio_connection), ASCIICHAT_OK);
  cr_assert_gt(capture_starts, 0);
  cr_assert_gt(keepalive_starts, 0);
  cr_assert(audio_connection.audio_sender_thread_created);
}
Test(client_audio_contract, protocol_startup_reports_each_worker_failure) {
  fail_worker = "data_reception";
  cr_assert_eq(client_protocol_start(&audio_connection), ERROR_THREAD);
  CLEAR_ERRNO_ALL();
  fail_worker = NULL;
  capture_start_result = -1;
  cr_assert_eq(client_protocol_start(&audio_connection), ERROR_THREAD);
  CLEAR_ERRNO_ALL();
  capture_start_result = 0;
  fail_worker = "audio_sender";
  cr_assert_eq(client_protocol_start(&audio_connection), ERROR_THREAD);
  CLEAR_ERRNO_ALL();
  audio_sender_cleanup(&audio_connection);
  audio_sender_finalize(&audio_connection);
  audio_connection.audio_send_queue_lc = (lifecycle_t)LIFECYCLE_INIT;
  fail_worker = NULL;
  keepalive_start_result = -1;
  cr_assert_eq(client_protocol_start(&audio_connection), ERROR_THREAD);
}
Test(client_audio_contract, startup_prefers_a_received_rejection_and_restores_transport_timeout) {
  audio_transport.receive_timeout_ns = 12345;
  receive_remote_error = true;
  cr_assert_eq(client_startup_peer_error(&audio_connection, ERROR_NETWORK), ERROR_INVALID_PASSWORD);
  cr_assert_eq(receive_calls, 1);
  cr_assert_eq(audio_transport.receive_timeout_ns, 12345);
  cr_assert_eq(client_startup_peer_error(&audio_connection, ERROR_INVALID_PARAM), ERROR_INVALID_PARAM);
  cr_assert_eq(receive_calls, 1);
  receive_remote_error = false;
  cr_assert_eq(client_startup_peer_error(&audio_connection, ERROR_NETWORK_CONNECT), ERROR_NETWORK_CONNECT);
  cr_assert_eq(receive_calls, 2);
  cr_assert_eq(audio_transport.receive_timeout_ns, 12345);
}
Test(client_audio_contract, startup_control_send_failures_stop_before_spawning_receivers) {
  network_result = ERROR_NETWORK;
  receive_remote_error = true;
  cr_assert_eq(client_protocol_start(&audio_connection), ERROR_INVALID_PASSWORD);
  cr_assert_eq(scheduled, 0);
  cr_assert(server_connection_is_lost(&audio_connection));
  server_connection_set_transport(&audio_connection, &audio_transport);
  atomic_store_int(&sent, 0);
  send_fail_on = 2;
  cr_assert_eq(client_protocol_start(&audio_connection), ERROR_INVALID_PASSWORD);
  cr_assert_eq(atomic_load_int(&sent), 2);
  cr_assert_eq(scheduled, 0);
}
Test(client_audio_contract, server_error_callbacks_distinguish_audio_degradation_from_connection_rejection) {
  cr_assert_not(handle_error_message_packet(&audio_connection, NULL, 0));
  error_packet_t error = {.error_code = HOST_TO_NET_U32(ERROR_AUDIO), .message_length = 0};
  acip_on_error(&error, NULL, &audio_connection);
  cr_assert(server_connection_is_active(&audio_connection));
  cr_assert_eq(server_connection_get_remote_error(&audio_connection), ASCIICHAT_OK);
  error.error_code = HOST_TO_NET_U32(ERROR_SESSION_FULL);
  acip_on_error(&error, NULL, &audio_connection);
  cr_assert(server_connection_is_lost(&audio_connection));
  cr_assert_eq(server_connection_get_remote_error(&audio_connection), ERROR_SESSION_FULL);
  cr_assert_not(should_exit());
  server_connection_set_transport(&audio_connection, &audio_transport);
  error.error_code = HOST_TO_NET_U32(ERROR_INVALID_PASSWORD);
  acip_on_error(&error, NULL, &audio_connection);
  cr_assert(should_exit());
  cr_assert_eq(server_connection_get_remote_error(&audio_connection), ERROR_INVALID_PASSWORD);
}
Test(client_audio_contract, control_callbacks_do_not_replace_the_active_media_transport) {
  acip_on_ping(&audio_connection);
  cr_assert_eq(atomic_load_int(&sent), 1);
  network_result = ERROR_NETWORK;
  acip_on_ping(&audio_connection);
  CLEAR_ERRNO_ALL();
  server_connection_set_transport(&audio_connection, &audio_transport);
  acip_on_pong(&audio_connection);
  acip_on_clear_console(&audio_connection);
  acip_session_joined_t joined = {0};
  acip_on_session_joined(NULL, &audio_connection);
  acip_on_session_joined(&joined, &audio_connection);
  joined.success = 1;
  joined.session_type = SESSION_TYPE_WEBRTC;
  acip_on_session_joined(&joined, &audio_connection);
  joined.session_type = SESSION_TYPE_DIRECT_TCP;
  acip_on_session_joined(&joined, &audio_connection);
  acip_webrtc_sdp_t sdp = {0};
  acip_webrtc_ice_t ice = {0};
  acip_on_webrtc_sdp(&sdp, sizeof(sdp), &audio_connection);
  acip_on_webrtc_ice(&ice, sizeof(ice), &audio_connection);
  cr_assert_eq(server_connection_get_transport(&audio_connection), &audio_transport);
  remote_log_packet_t log = {
      .log_level = LOG_INFO, .direction = REMOTE_LOG_DIRECTION_SERVER_TO_CLIENT, .message_length = HOST_TO_NET_U32(2)};
  acip_on_remote_log(&log, "ok", &audio_connection);
  log.flags = HOST_TO_NET_U16(REMOTE_LOG_FLAG_TRUNCATED);
  acip_on_remote_log(&log, "ok", &audio_connection);
  log.direction = REMOTE_LOG_DIRECTION_CLIENT_TO_SERVER;
  acip_on_remote_log(&log, "ok", &audio_connection);
  handle_remote_log_packet(NULL, 0);
}
