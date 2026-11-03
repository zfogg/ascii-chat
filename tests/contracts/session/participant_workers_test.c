#include <criterion/criterion.h>
#include "session/participant.h"
#include "session/capture.h"
#include "session/audio.h"
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/audio/opus.h>
#include <ascii-chat/audio/limits.h>
#include <ascii-chat/util/endian.h>
#include <math.h>
#include <string.h>

static bool fail_capture, fail_thread, fail_join;
static unsigned created, joined, reads, waits, packets;
static session_capture_config_t requested_capture;
static char requested_path[256];
static image_t *worker_frame;
static void (*stop_worker)(void);
static session_capture_ctx_t *worker_capture(const session_capture_config_t *config) {
  requested_capture = *config;
  if (config->path) {
    SAFE_STRNCPY(requested_path, config->path, sizeof(requested_path));
    requested_capture.path = requested_path;
  }
  if (fail_capture) {
    SET_ERRNO(ERROR_MEDIA_INIT, "Injected capture factory failure");
    return NULL;
  }
  return session_network_capture_create(config->target_fps);
}
static int worker_create(asciichat_thread_t *thread, const char *name, void *(*fn)(void *), void *arg) {
  (void)thread;
  cr_assert_not_null(name);
  cr_assert_not_null(fn);
  cr_assert_not_null(arg);
  ++created;
  return fail_thread ? -1 : 0;
}
static int worker_join(asciichat_thread_t *thread, void **result) {
  (void)thread;
  (void)result;
  ++joined;
  return fail_join ? -1 : 0;
}
static asciichat_error_t worker_audio_start(session_audio_ctx_t *audio) {
  cr_assert_not_null(audio);
  return ASCIICHAT_OK;
}
static image_t *worker_read_video(session_capture_ctx_t *capture) {
  cr_assert_not_null(capture);
  ++reads;
  return reads == 1 ? NULL : worker_frame;
}
static void worker_pace(session_capture_ctx_t *capture) {
  cr_assert_not_null(capture);
  stop_worker();
}
static void worker_sleep(unsigned ms) {
  cr_assert_eq(ms, 1);
  ++waits;
}
static size_t worker_read_audio(session_audio_ctx_t *audio, float *output, size_t capacity) {
  cr_assert_not_null(audio);
  ++reads;
  if (reads == 1)
    return 0;
  size_t count = OPUS_FRAME_SAMPLES;
  cr_assert_geq(capacity, count);
  size_t offset = (reads - 2) * count;
  for (size_t i = 0; i < count; ++i) {
    double wave = sin(2 * 3.141592653589793 * ((offset + i) / AUDIO_CHANNELS) / 48.0);
    output[i] = (float)(wave * ((i % AUDIO_CHANNELS) ? .6 : .2));
  }
  return count;
}
#define session_capture_create worker_capture
#define acthread_create worker_create
#define thread_join worker_join
#define session_audio_start_duplex worker_audio_start
#define session_capture_read_frame worker_read_video
#define session_capture_sleep_for_fps worker_pace
#define session_audio_read_captured worker_read_audio
#define platform_sleep_ms worker_sleep
#include "../../../src/common/session/participant.c"
#undef session_capture_create
#undef acthread_create
#undef thread_join
#undef session_audio_start_duplex
#undef session_capture_read_frame
#undef session_capture_sleep_for_fps
#undef session_audio_read_captured
#undef platform_sleep_ms

static session_participant_t *participant;
static bool reject_packet;
static void stop_video_worker(void) {
  session_av_state_begin_stop(&participant->video);
}
static void stop_audio_worker(void) {
  session_av_state_begin_stop(&participant->audio);
}
static asciichat_error_t worker_receive_packet(acip_transport_t *transport, const void *data, size_t length) {
  (void)transport;
  packet_header_t packet;
  cr_assert_geq(length, sizeof(packet));
  memcpy(&packet, data, sizeof(packet));
  const uint8_t *payload = (const uint8_t *)data + sizeof(packet);
  cr_assert_eq(NET_TO_HOST_U32(packet.length), length - sizeof(packet));
  if (NET_TO_HOST_U16(packet.type) == PACKET_TYPE_IMAGE_FRAME) {
    image_frame_packet_t header;
    cr_assert_geq(length, sizeof(packet) + sizeof(header));
    memcpy(&header, payload, sizeof(header));
    cr_assert_eq(NET_TO_HOST_U32(header.width), 8);
    cr_assert_eq(NET_TO_HOST_U32(header.height), 4);
    cr_assert_arr_eq(payload + sizeof(header), worker_frame->pixels, 8 * 4 * sizeof(rgb_pixel_t));
  } else {
    cr_assert_eq(NET_TO_HOST_U16(packet.type), PACKET_TYPE_AUDIO_OPUS_BATCH);
    cr_assert_eq(bytes_read_u32_unaligned(payload), HOST_TO_NET_U32(AUDIO_SAMPLE_RATE));
    cr_assert_eq(NET_TO_HOST_U32(bytes_read_u32_unaligned(payload + 8)), 1);
    uint16_t frame_size;
    memcpy(&frame_size, payload + 16, sizeof(frame_size));
    frame_size = NET_TO_HOST_U16(frame_size);
    cr_assert_eq(length, sizeof(packet) + 18 + frame_size);
    opus_codec_t *decoder = opus_codec_create_decoder_channels(AUDIO_SAMPLE_RATE, participant->opus_encoder->channels);
    cr_assert_not_null(decoder);
    float decoded[OPUS_FRAME_SAMPLES * AUDIO_CHANNELS];
    int expected_samples = OPUS_FRAME_SAMPLES * participant->opus_encoder->channels;
    cr_assert_eq(opus_codec_decode(decoder, payload + 18, frame_size, decoded, ARRAY_SIZE(decoded)), expected_samples);
    double energy = 0;
    for (size_t i = 0; i < (size_t)expected_samples; ++i)
      energy += decoded[i] * decoded[i];
    cr_assert_gt(energy, 1.0);
    opus_codec_destroy(decoder);
    stop_worker();
  }
  ++packets;
  return reject_packet ? ERROR_NETWORK : ASCIICHAT_OK;
}
static const acip_transport_methods_t methods = {.send = worker_receive_packet};
static acip_transport_t transport = {.methods = &methods};
static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  session_participant_config_t config = {.enable_video = true, .enable_audio = true};
  participant = session_participant_create(&config);
  cr_assert_not_null(participant);
  participant->connected = true;
  participant->transport = &transport;
}
static void teardown(void) {
  fail_join = false;
  session_participant_destroy(participant);
  image_destroy(worker_frame);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(participant_workers, .init = setup, .fini = teardown, .timeout = 15);

Test(participant_workers, capture_source_selection_preserves_urls_files_stdin_and_webcam_index) {
  const char *files[] = {"fixture.ppm", "-", ""};
  for (size_t i = 0; i < ARRAY_SIZE(files); ++i) {
    options_t *values = SAFE_MALLOC(sizeof(*values), options_t *);
    *values = *options_get();
    SAFE_STRNCPY(values->media_file, files[i], sizeof(values->media_file));
    values->webcam_index = 3;
    cr_assert_eq(options_state_set(values), ASCIICHAT_OK);
    SAFE_FREE(values);
    cr_assert_eq(session_participant_start_video(participant), ASCIICHAT_OK);
    cr_assert_eq(requested_capture.type, i == 0   ? MEDIA_SOURCE_FILE
                                         : i == 1 ? MEDIA_SOURCE_STDIN
                                                  : MEDIA_SOURCE_WEBCAM);
    if (i != 1)
      cr_assert_str_eq(requested_capture.path, i == 0 ? files[i] : "3");
    else
      cr_assert_null(requested_capture.path);
    cr_assert(requested_capture.resize_for_network);
    session_participant_stop_video(participant);
  }
  options_t *values = SAFE_MALLOC(sizeof(*values), options_t *);
  *values = *options_get();
  SAFE_STRNCPY(values->media_url, "https://example.invalid/fixture", sizeof(values->media_url));
  cr_assert_eq(options_state_set(values), ASCIICHAT_OK);
  SAFE_FREE(values);
  cr_assert_eq(session_participant_start_video(participant), ASCIICHAT_OK);
  cr_assert_eq(requested_capture.type, MEDIA_SOURCE_FILE);
  cr_assert_str_eq(requested_capture.path, "https://example.invalid/fixture");
  cr_assert_eq(created, 4);
  cr_assert_eq(joined, 3);
}
Test(participant_workers, failed_start_is_retryable_and_failed_join_retains_the_live_workers_capture) {
  fail_capture = true;
  cr_assert_eq(session_participant_start_video(participant), ERROR_MEDIA_INIT);
  cr_assert_null(participant->video_capture);
  cr_assert_not(session_participant_is_video_active(participant));
  fail_capture = false;
  fail_thread = true;
  cr_assert_eq(session_participant_start_video(participant), ERROR_THREAD);
  cr_assert_null(participant->video_capture);
  fail_thread = false;
  cr_assert_eq(session_participant_start_video(participant), ASCIICHAT_OK);
  session_capture_ctx_t *capture = participant->video_capture;
  fail_join = true;
  session_participant_stop_video(participant);
  cr_assert(participant->video_thread_created);
  cr_assert_eq(participant->video_capture, capture);
  cr_assert_eq(session_participant_start_video(participant), ERROR_INVALID_STATE);
  fail_join = false;
  session_participant_stop_video(participant);
  cr_assert_null(participant->video_capture);
  cr_assert_not(participant->video_thread_created);
  cr_assert_eq(session_participant_start_video(participant), ASCIICHAT_OK);
}
Test(participant_workers, disabled_or_transitioning_media_cannot_start_or_replace_transport) {
  participant->enable_video = participant->enable_audio = false;
  cr_assert_eq(session_participant_start_video(participant), ERROR_INVALID_STATE);
  cr_assert_eq(session_participant_start_audio(participant), ERROR_INVALID_STATE);
  participant->enable_video = participant->enable_audio = true;
  cr_assert_neq(session_av_state_begin(&participant->video), 0);
  cr_assert_neq(session_av_state_begin(&participant->audio), 0);
  cr_assert_eq(session_participant_start_video(participant), ERROR_INVALID_STATE);
  cr_assert_eq(session_participant_start_audio(participant), ERROR_INVALID_STATE);
  cr_assert_eq(session_participant_set_transport(participant, NULL, ACIP_TRANSPORT_BORROWED), ERROR_INVALID_STATE);
  cr_assert_eq(created, 0);
}
Test(participant_workers, borrowed_transport_cannot_silently_change_ownership) {
  cr_assert_eq(session_participant_set_transport(participant, &transport, ACIP_TRANSPORT_BORROWED), ASCIICHAT_OK);
  cr_assert_eq(session_participant_set_transport(participant, &transport, ACIP_TRANSPORT_OWNED), ERROR_INVALID_STATE);
  cr_assert_eq(session_participant_set_transport(participant, &transport, (acip_transport_ownership_t)99),
               ERROR_INVALID_PARAM);
  cr_assert_eq(session_participant_get_transport(participant), &transport);
  network_connection_t *connection = NULL;
  cr_assert_eq(session_participant_take_connection(participant, &connection), ERROR_INVALID_STATE);
  cr_assert_null(connection);
  cr_assert_null(session_participant_create(NULL));
}
Test(participant_workers, video_worker_retries_missing_frames_and_keeps_capture_on_failed_delivery) {
  cr_assert_eq(session_participant_start_video(participant), ASCIICHAT_OK);
  worker_frame = image_new(8, 4);
  cr_assert_not_null(worker_frame);
  memset(worker_frame->pixels, 120, 8 * 4 * sizeof(rgb_pixel_t));
  reject_packet = true;
  stop_worker = stop_video_worker;
  cr_assert_null(participant_video_capture_thread(participant));
  cr_assert_eq(reads, 2);
  cr_assert_eq(waits, 1);
  cr_assert_eq(packets, 1);
  cr_assert_not_null(participant->video_capture);
  cr_assert_null(participant_video_capture_thread(NULL));
}
static void run_audio_worker(bool legacy_mono) {
  cr_assert_eq(session_participant_start_audio(participant), ASCIICHAT_OK);
  cr_assert_eq(participant->opus_encoder->channels, AUDIO_CHANNELS);
  if (legacy_mono) {
    opus_codec_destroy(participant->opus_encoder);
    participant->opus_encoder = opus_codec_create_encoder_channels(OPUS_APPLICATION_VOIP, AUDIO_SAMPLE_RATE, 24000, 1);
    cr_assert_not_null(participant->opus_encoder);
  }
  reject_packet = true;
  stop_worker = stop_audio_worker;
  cr_assert_null(participant_audio_capture_thread(participant));
  cr_assert_eq(reads, 3);
  cr_assert_eq(waits, 1);
  cr_assert_eq(packets, 1);
  cr_assert_not_null(participant->audio_capture);
  cr_assert_null(participant_audio_capture_thread(NULL));
}

Test(participant_workers, audio_worker_accumulates_partial_stereo_reads_before_sending_one_frame) {
  run_audio_worker(false);
}
Test(participant_workers, audio_worker_downmixes_stereo_capture_for_an_existing_mono_encoder) {
  run_audio_worker(true);
}

static unsigned settings_events, error_events;
static void settings_changed(session_participant_t *p, const session_settings_t *settings, void *context) {
  cr_assert_eq(p, participant);
  cr_assert_eq(context, &settings_events);
  cr_assert_eq(settings->width, 42);
  cr_assert_eq(settings->version, 5);
  ++settings_events;
}
static void settings_error(session_participant_t *p, asciichat_error_t error, const char *message, void *context) {
  cr_assert_eq(p, participant);
  cr_assert_eq(context, &settings_events);
  cr_assert_eq(error, ERROR_CONFIG);
  cr_assert_str_eq(message, "Remote rendering update rejected");
  ++error_events;
}
Test(participant_workers, rejected_settings_notify_the_caller_but_invalid_payloads_preserve_committed_values) {
  participant->callbacks.on_settings_changed = settings_changed;
  participant->callbacks.on_error = settings_error;
  participant->user_data = &settings_events;
  uint8_t payload[1024];
  size_t length;
  acip_settings_header_t header = {.schema = 1, .revision = 5, .status = ERROR_CONFIG};
  cr_assert_eq(acip_settings_encode(&header, "{\"width\":42}", payload, sizeof(payload), &length), ASCIICHAT_OK);
  cr_assert_eq(session_participant_receive_settings(participant, payload, length), ASCIICHAT_OK);
  cr_assert_eq(settings_events, 1);
  cr_assert_eq(error_events, 1);
  cr_assert_neq(session_participant_receive_settings(participant, payload, 1), ASCIICHAT_OK);
  header.revision = 6;
  cr_assert_eq(acip_settings_encode(&header, "{not-json}", payload, sizeof(payload), &length), ASCIICHAT_OK);
  cr_assert_neq(session_participant_receive_settings(participant, payload, length), ASCIICHAT_OK);
  cr_assert_eq(participant->settings.version, 5);
  cr_assert_eq(participant->settings.width, 42);
  cr_assert_eq(settings_events, 1);
  cr_assert_eq(error_events, 1);
}
Test(participant_workers, failed_audio_join_retains_encoder_and_rejects_restart_until_the_worker_is_joined) {
  cr_assert_eq(session_participant_start_audio(participant), ASCIICHAT_OK);
  session_audio_ctx_t *audio = participant->audio_capture;
  opus_codec_t *encoder = participant->opus_encoder;
  fail_join = true;
  session_participant_stop_audio(participant);
  cr_assert(participant->audio_thread_created);
  cr_assert_eq(participant->audio_capture, audio);
  cr_assert_eq(participant->opus_encoder, encoder);
  cr_assert_eq(session_participant_start_audio(participant), ERROR_INVALID_STATE);
  fail_join = false;
  session_participant_stop_audio(participant);
  cr_assert_null(participant->audio_capture);
  cr_assert_null(participant->opus_encoder);
  cr_assert_eq(session_participant_start_audio(participant), ASCIICHAT_OK);
}
Test(participant_workers, constructor_owns_identity_strings_and_rejects_an_out_of_range_endpoint_before_connecting) {
  char address[] = "127.0.0.9", password[] = "fixture-password", key[] = "fixture-server-key";
  session_participant_config_t config = {
      .address = address, .port = 65536, .password = password, .server_key = key, .encryption_enabled = true};
  session_participant_t *other = session_participant_create(&config);
  cr_assert_not_null(other);
  memset(address, 0, sizeof(address));
  memset(password, 0, sizeof(password));
  memset(key, 0, sizeof(key));
  cr_assert_str_eq(other->address, "127.0.0.9");
  cr_assert_str_eq(other->password, "fixture-password");
  cr_assert_str_eq(other->server_key, "fixture-server-key");
  cr_assert_eq(session_participant_connect(other), ERROR_INVALID_PARAM);
  cr_assert_null(other->network);
  participant->client_id = 17;
  cr_assert_eq(session_participant_get_client_id(participant), 17);
  session_participant_destroy(other);
}
