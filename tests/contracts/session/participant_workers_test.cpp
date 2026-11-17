#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
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
static int worker_create(platform_thread_t *thread, const char *name, void *(*fn)(void *), void *arg) {
  (void)thread;
  REQUIRE((name) != nullptr);
  REQUIRE((fn) != nullptr);
  REQUIRE((arg) != nullptr);
  ++created;
  return fail_thread ? -1 : 0;
}
static int worker_join(platform_thread_t *thread, void **result) {
  (void)thread;
  (void)result;
  ++joined;
  return fail_join ? -1 : 0;
}
static asciichat_error_t worker_audio_start(session_audio_ctx_t *audio) {
  REQUIRE((audio) != nullptr);
  return ASCIICHAT_OK;
}
static image_t *worker_read_video(session_capture_ctx_t *capture) {
  REQUIRE((capture) != nullptr);
  ++reads;
  return reads == 1 ? NULL : worker_frame;
}
static void worker_pace(session_capture_ctx_t *capture) {
  REQUIRE((capture) != nullptr);
  stop_worker();
}
static void worker_sleep(unsigned ms) {
  REQUIRE((ms) == (1));
  ++waits;
}
static size_t worker_read_audio(session_audio_ctx_t *audio, float *output, size_t capacity) {
  REQUIRE((audio) != nullptr);
  ++reads;
  if (reads == 1)
    return 0;
  size_t count = OPUS_FRAME_SAMPLES;
  REQUIRE((capacity) >= (count));
  size_t offset = (reads - 2) * count;
  for (size_t i = 0; i < count; ++i) {
    double wave = sin(2 * 3.141592653589793 * ((offset + i) / AUDIO_CHANNELS) / 48.0);
    output[i] = (float)(wave * ((i % AUDIO_CHANNELS) ? .6 : .2));
  }
  return count;
}
#define session_capture_create worker_capture
#define platform_thread_create worker_create
#define thread_join worker_join
#define session_audio_start_duplex worker_audio_start
#define session_capture_read_frame worker_read_video
#define session_capture_sleep_for_fps worker_pace
#define session_audio_read_captured worker_read_audio
#define platform_sleep_ms worker_sleep
#include "../../../src/common/session/participant.c"
#undef session_capture_create
#undef platform_thread_create
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
  REQUIRE((length) >= (sizeof(packet)));
  memcpy(&packet, data, sizeof(packet));
  const uint8_t *payload = (const uint8_t *)data + sizeof(packet);
  REQUIRE((NET_TO_HOST_U32(packet.length)) == (length - sizeof(packet)));
  if (NET_TO_HOST_U16(packet.type) == PACKET_TYPE_IMAGE_FRAME) {
    image_frame_packet_t header;
    REQUIRE((length) >= (sizeof(packet) + sizeof(header)));
    memcpy(&header, payload, sizeof(header));
    REQUIRE((NET_TO_HOST_U32(header.width)) == (8));
    REQUIRE((NET_TO_HOST_U32(header.height)) == (4));
    REQUIRE(std::memcmp((payload + sizeof(header)), (worker_frame->pixels), (8 * 4 * sizeof(rgb_pixel_t))) == 0);
  } else {
    REQUIRE((NET_TO_HOST_U16(packet.type)) == (PACKET_TYPE_AUDIO_OPUS_BATCH));
    REQUIRE((bytes_read_u32_unaligned(payload)) == (HOST_TO_NET_U32(AUDIO_SAMPLE_RATE)));
    REQUIRE((NET_TO_HOST_U32(bytes_read_u32_unaligned(payload + 8))) == (1));
    uint16_t frame_size;
    memcpy(&frame_size, payload + 16, sizeof(frame_size));
    frame_size = NET_TO_HOST_U16(frame_size);
    REQUIRE((length) == (sizeof(packet) + 18 + frame_size));
    opus_codec_t *decoder = opus_codec_create_decoder_channels(AUDIO_SAMPLE_RATE, participant->opus_encoder->channels);
    REQUIRE((decoder) != nullptr);
    float decoded[OPUS_FRAME_SAMPLES * AUDIO_CHANNELS];
    int expected_samples = OPUS_FRAME_SAMPLES * participant->opus_encoder->channels;
    REQUIRE((opus_codec_decode(decoder, payload + 18, frame_size, decoded, ARRAY_SIZE(decoded))) == (expected_samples));
    double energy = 0;
    for (size_t i = 0; i < (size_t)expected_samples; ++i)
      energy += decoded[i] * decoded[i];
    REQUIRE((energy) > (1.0));
    if (participant->opus_encoder->channels == 1) {
      // The fixture's channels have amplitudes 0.2 and 0.6, so their mono
      // average is 0.4. Ignore Opus startup delay in the first half frame.
      double tail_energy = 0;
      for (size_t i = (size_t)expected_samples / 2; i < (size_t)expected_samples; ++i)
        tail_energy += decoded[i] * decoded[i];
      double rms = sqrt(tail_energy / ((size_t)expected_samples - (size_t)expected_samples / 2));
      REQUIRE((rms) > (0.20));
      REQUIRE((rms) < (0.37));
    }
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
  REQUIRE((options_state_init()) == (ASCIICHAT_OK));
  session_participant_config_t config = {.enable_video = true, .enable_audio = true};
  participant = session_participant_create(&config);
  REQUIRE((participant) != nullptr);
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
TEST_CASE("participant_workers/capture_source_selection_preserves_urls_files_stdin_and_webcam_index", "[participant_workers]") {
  const char *files[] = {"fixture.ppm", "-", ""};
  for (size_t i = 0; i < ARRAY_SIZE(files); ++i) {
    options_t *values = SAFE_MALLOC(sizeof(*values), options_t *);
    *values = *options_get();
    SAFE_STRNCPY(values->media_file, files[i], sizeof(values->media_file));
    values->webcam_index = 3;
    REQUIRE((options_state_set(values)) == (ASCIICHAT_OK));
    SAFE_FREE(values);
    REQUIRE((session_participant_start_video(participant)) == (ASCIICHAT_OK));
    REQUIRE((requested_capture.type) == (i == 0 ? MEDIA_SOURCE_FILE : i == 1 ? MEDIA_SOURCE_STDIN : MEDIA_SOURCE_WEBCAM));
    if (i != 1)
      REQUIRE(std::strcmp((requested_capture.path), (i == 0 ? files[i] : "3")) == 0);
    else
      REQUIRE((requested_capture.path) == nullptr);
    REQUIRE(requested_capture.resize_for_network);
    session_participant_stop_video(participant);
  }
  options_t *values = SAFE_MALLOC(sizeof(*values), options_t *);
  *values = *options_get();
  SAFE_STRNCPY(values->media_url, "https://example.invalid/fixture", sizeof(values->media_url));
  REQUIRE((options_state_set(values)) == (ASCIICHAT_OK));
  SAFE_FREE(values);
  REQUIRE((session_participant_start_video(participant)) == (ASCIICHAT_OK));
  REQUIRE((requested_capture.type) == (MEDIA_SOURCE_FILE));
  REQUIRE(std::strcmp((requested_capture.path), ("https://example.invalid/fixture")) == 0);
  REQUIRE((created) == (4));
  REQUIRE((joined) == (3));
}
TEST_CASE("participant_workers/failed_start_is_retryable_and_failed_join_retains_the_live_workers_capture", "[participant_workers]") {
  fail_capture = true;
  REQUIRE((session_participant_start_video(participant)) == (ERROR_MEDIA_INIT));
  REQUIRE((participant->video_capture) == nullptr);
  REQUIRE(!(session_participant_is_video_active(participant)));
  fail_capture = false;
  fail_thread = true;
  REQUIRE((session_participant_start_video(participant)) == (ERROR_THREAD));
  REQUIRE((participant->video_capture) == nullptr);
  fail_thread = false;
  REQUIRE((session_participant_start_video(participant)) == (ASCIICHAT_OK));
  session_capture_ctx_t *capture = participant->video_capture;
  fail_join = true;
  session_participant_stop_video(participant);
  REQUIRE(participant->video_thread_created);
  REQUIRE((participant->video_capture) == (capture));
  REQUIRE((session_participant_start_video(participant)) == (ERROR_INVALID_STATE));
  fail_join = false;
  session_participant_stop_video(participant);
  REQUIRE((participant->video_capture) == nullptr);
  REQUIRE(!(participant->video_thread_created));
  REQUIRE((session_participant_start_video(participant)) == (ASCIICHAT_OK));
}
TEST_CASE("participant_workers/disabled_or_transitioning_media_cannot_start_or_replace_transport", "[participant_workers]") {
  participant->enable_video = participant->enable_audio = false;
  REQUIRE((session_participant_start_video(participant)) == (ERROR_INVALID_STATE));
  REQUIRE((session_participant_start_audio(participant)) == (ERROR_INVALID_STATE));
  participant->enable_video = participant->enable_audio = true;
  REQUIRE((session_av_state_begin(&participant->video)) != (0));
  REQUIRE((session_av_state_begin(&participant->audio)) != (0));
  REQUIRE((session_participant_start_video(participant)) == (ERROR_INVALID_STATE));
  REQUIRE((session_participant_start_audio(participant)) == (ERROR_INVALID_STATE));
  REQUIRE((session_participant_set_transport(participant, NULL, ACIP_TRANSPORT_BORROWED)) == (ERROR_INVALID_STATE));
  REQUIRE((created) == (0));
}
TEST_CASE("participant_workers/borrowed_transport_cannot_silently_change_ownership", "[participant_workers]") {
  REQUIRE((session_participant_set_transport(participant, &transport, ACIP_TRANSPORT_BORROWED)) == (ASCIICHAT_OK));
  REQUIRE((session_participant_set_transport(participant, &transport, ACIP_TRANSPORT_OWNED)) == (ERROR_INVALID_STATE));
  REQUIRE((session_participant_set_transport(participant, &transport, (acip_transport_ownership_t)99)) == (ERROR_INVALID_PARAM));
  REQUIRE((session_participant_get_transport(participant)) == (&transport));
  network_connection_t *connection = NULL;
  REQUIRE((session_participant_take_connection(participant, &connection)) == (ERROR_INVALID_STATE));
  REQUIRE((connection) == nullptr);
  REQUIRE((session_participant_create(NULL)) == nullptr);
}
TEST_CASE("participant_workers/video_worker_retries_missing_frames_and_keeps_capture_on_failed_delivery", "[participant_workers]") {
  REQUIRE((session_participant_start_video(participant)) == (ASCIICHAT_OK));
  worker_frame = image_new(8, 4);
  REQUIRE((worker_frame) != nullptr);
  memset(worker_frame->pixels, 120, 8 * 4 * sizeof(rgb_pixel_t));
  reject_packet = true;
  stop_worker = stop_video_worker;
  REQUIRE((participant_video_capture_thread(participant)) == nullptr);
  REQUIRE((reads) == (2));
  REQUIRE((waits) == (1));
  REQUIRE((packets) == (1));
  REQUIRE((participant->video_capture) != nullptr);
  REQUIRE((participant_video_capture_thread(NULL)) == nullptr);
}
static void run_audio_worker(bool legacy_mono) {
  REQUIRE((session_participant_start_audio(participant)) == (ASCIICHAT_OK));
  REQUIRE((participant->opus_encoder->channels) == (AUDIO_CHANNELS));
  if (legacy_mono) {
    opus_codec_destroy(participant->opus_encoder);
    participant->opus_encoder = opus_codec_create_encoder_channels(OPUS_APPLICATION_VOIP, AUDIO_SAMPLE_RATE, 24000, 1);
    REQUIRE((participant->opus_encoder) != nullptr);
  }
  reject_packet = true;
  stop_worker = stop_audio_worker;
  REQUIRE((participant_audio_capture_thread(participant)) == nullptr);
  REQUIRE((reads) == (3));
  REQUIRE((waits) == (1));
  REQUIRE((packets) == (1));
  REQUIRE((participant->audio_capture) != nullptr);
  REQUIRE((participant_audio_capture_thread(NULL)) == nullptr);
}

TEST_CASE("participant_workers/audio_worker_accumulates_partial_stereo_reads_before_sending_one_frame", "[participant_workers]") {
  run_audio_worker(false);
}
TEST_CASE("participant_workers/audio_worker_downmixes_stereo_capture_for_an_existing_mono_encoder", "[participant_workers]") {
  run_audio_worker(true);
}

static unsigned settings_events, error_events;
static void settings_changed(session_participant_t *p, const session_settings_t *settings, void *context) {
  REQUIRE((p) == (participant));
  REQUIRE((context) == (&settings_events));
  REQUIRE((settings->width) == (42));
  REQUIRE((settings->version) == (5));
  ++settings_events;
}
static void settings_error(session_participant_t *p, asciichat_error_t error, const char *message, void *context) {
  REQUIRE((p) == (participant));
  REQUIRE((context) == (&settings_events));
  REQUIRE((error) == (ERROR_CONFIG));
  REQUIRE(std::strcmp((message), ("Remote rendering update rejected")) == 0);
  ++error_events;
}
TEST_CASE("participant_workers/rejected_settings_notify_the_caller_but_invalid_payloads_preserve_committed_values", "[participant_workers]") {
  participant->callbacks.on_settings_changed = settings_changed;
  participant->callbacks.on_error = settings_error;
  participant->user_data = &settings_events;
  uint8_t payload[1024];
  size_t length;
  acip_settings_header_t header = {.schema = 1, .revision = 5, .status = ERROR_CONFIG};
  REQUIRE((acip_settings_encode(&header, "{\"width\":42}", payload, sizeof(payload), &length)) == (ASCIICHAT_OK));
  REQUIRE((session_participant_receive_settings(participant, payload, length)) == (ASCIICHAT_OK));
  REQUIRE((settings_events) == (1));
  REQUIRE((error_events) == (1));
  REQUIRE((session_participant_receive_settings(participant, payload, 1)) != (ASCIICHAT_OK));
  header.revision = 6;
  REQUIRE((acip_settings_encode(&header, "{not-json}", payload, sizeof(payload), &length)) == (ASCIICHAT_OK));
  REQUIRE((session_participant_receive_settings(participant, payload, length)) != (ASCIICHAT_OK));
  REQUIRE((participant->settings.version) == (5));
  REQUIRE((participant->settings.width) == (42));
  REQUIRE((settings_events) == (1));
  REQUIRE((error_events) == (1));
}
TEST_CASE("participant_workers/failed_audio_join_retains_encoder_and_rejects_restart_until_the_worker_is_joined", "[participant_workers]") {
  REQUIRE((session_participant_start_audio(participant)) == (ASCIICHAT_OK));
  session_audio_ctx_t *audio = participant->audio_capture;
  opus_codec_t *encoder = participant->opus_encoder;
  fail_join = true;
  session_participant_stop_audio(participant);
  REQUIRE(participant->audio_thread_created);
  REQUIRE((participant->audio_capture) == (audio));
  REQUIRE((participant->opus_encoder) == (encoder));
  REQUIRE((session_participant_start_audio(participant)) == (ERROR_INVALID_STATE));
  fail_join = false;
  session_participant_stop_audio(participant);
  REQUIRE((participant->audio_capture) == nullptr);
  REQUIRE((participant->opus_encoder) == nullptr);
  REQUIRE((session_participant_start_audio(participant)) == (ASCIICHAT_OK));
}
TEST_CASE("participant_workers/constructor_owns_identity_strings_and_rejects_an_out_of_range_endpoint_before_connecting", "[participant_workers]") {
  char address[] = "127.0.0.9", password[] = "fixture-password", key[] = "fixture-server-key";
  session_participant_config_t config = {.address = address, .port = 65536, .password = password, .server_key = key, .encryption_enabled = true};
  session_participant_t *other = session_participant_create(&config);
  REQUIRE((other) != nullptr);
  memset(address, 0, sizeof(address));
  memset(password, 0, sizeof(password));
  memset(key, 0, sizeof(key));
  REQUIRE(std::strcmp((other->address), ("127.0.0.9")) == 0);
  REQUIRE(std::strcmp((other->password), ("fixture-password")) == 0);
  REQUIRE(std::strcmp((other->server_key), ("fixture-server-key")) == 0);
  REQUIRE((session_participant_connect(other)) == (ERROR_INVALID_PARAM));
  REQUIRE((other->network) == nullptr);
  participant->client_id = 17;
  REQUIRE((session_participant_get_client_id(participant)) == (17));
  session_participant_destroy(other);
}
