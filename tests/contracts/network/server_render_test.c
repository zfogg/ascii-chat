#include <criterion/criterion.h>
#include "server/render.h"
#include "server/main.h"
#include "server/connections.h"
#include "server/stream.h"
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/audio/opus.h>
#include <ascii-chat/audio/limits.h>
#include <ascii-chat/util/endian.h>
#include <math.h>
#include <string.h>

atomic_t g_should_exit;
mixer_t *volatile g_audio_mixer;
static client_info_t client;
static server_context_t server;
static unsigned starts, joins, fail_start, fail_join, sleeps, mixes, queued, video_frames, waits;
static unsigned stop_after = 2, capability_wait;
static bool handles[2], fail_encoder, reject_audio, no_frame, exit_wait;
static uint64_t clock_ns;
static uint64_t render_clock(void) {
  return clock_ns += NS_PER_MS_INT;
}
static void render_sleep(uint64_t ns) {
  clock_ns += ns;
  if (++sleeps >= stop_after)
    atomic_store_bool(&client.audio_render_thread_running, false);
}
static void render_wait(unsigned ms) {
  clock_ns += ms * NS_PER_MS_INT;
  ++waits;
  if (exit_wait)
    atomic_store_bool(&g_should_exit, true);
  else if (waits >= capability_wait)
    client.has_terminal_caps = true;
}
static int render_create(asciichat_thread_t *thread, const char *name, void *(*fn)(void *), void *context) {
  ++starts;
  cr_assert_eq(context, &client);
  cr_assert_not_null(fn);
  cr_assert(strstr(name, client.client_id));
  if (starts == fail_start)
    return -1;
  handles[thread == &client.audio_render_thread] = true;
  return 0;
}
static bool render_initialized(const asciichat_thread_t *thread) {
  return handles[thread == &client.audio_render_thread];
}
static void render_init(asciichat_thread_t *thread) {
  handles[thread == &client.audio_render_thread] = false;
}
static int render_join(asciichat_thread_t *thread, void **result) {
  (void)thread;
  (void)result;
  ++joins;
  cr_assert_not(atomic_load_bool(&client.video_render_thread_running));
  cr_assert_not(atomic_load_bool(&client.audio_render_thread_running));
  return joins == fail_join ? ERROR_THREAD : ASCIICHAT_OK;
}
static opus_codec_t *render_encoder(opus_application_t application, int rate, int bitrate, int channels) {
  return fail_encoder ? NULL : opus_codec_create_encoder_channels(application, rate, bitrate, channels);
}
static int render_mix(mixer_t *mixer, float *output, int count, uint32_t exclude) {
  cr_assert_eq(mixer, g_audio_mixer);
  cr_assert_eq(exclude, fnv1a_hash_string(client.client_id));
  cr_assert_eq(count, 480 * AUDIO_CHANNELS);
  for (int i = 0; i < count; ++i)
    output[i] = (float)(.3 * sin(2 * 3.141592653589793 * ((mixes * count + i) / AUDIO_CHANNELS) / 48.0));
  ++mixes;
  return count;
}
static asciichat_error_t render_audio(client_info_t *recipient, const void *data, size_t length) {
  cr_assert_eq(recipient, &client);
  const uint8_t *wire = data;
  cr_assert_geq(length, 18);
  cr_assert_eq(NET_TO_HOST_U32(bytes_read_u32_unaligned(wire)), AUDIO_SAMPLE_RATE);
  cr_assert_eq(NET_TO_HOST_U32(bytes_read_u32_unaligned(wire + 4)), 20);
  cr_assert_eq(NET_TO_HOST_U32(bytes_read_u32_unaligned(wire + 8)), 1);
  cr_assert_eq(bytes_read_u32_unaligned(wire + 12), 0);
  uint16_t frame_size;
  memcpy(&frame_size, wire + 16, sizeof(frame_size));
  frame_size = NET_TO_HOST_U16(frame_size);
  cr_assert_eq(length, 18 + frame_size);
  opus_codec_t *decoder = opus_codec_create_decoder_channels(AUDIO_SAMPLE_RATE, AUDIO_CHANNELS);
  cr_assert_not_null(decoder);
  float decoded[OPUS_FRAME_SAMPLES * AUDIO_CHANNELS];
  cr_assert_eq(opus_codec_decode(decoder, wire + 18, frame_size, decoded, ARRAY_SIZE(decoded)), ARRAY_SIZE(decoded));
  double energy = 0;
  for (size_t i = 0; i < ARRAY_SIZE(decoded); ++i)
    energy += decoded[i] * decoded[i];
  cr_assert_gt(energy, 1.0);
  opus_codec_destroy(decoder);
  ++queued;
  return reject_audio ? ERROR_NETWORK : ASCIICHAT_OK;
}
static bool render_sources(void) {
  return true;
}
static char *render_ascii(const char *id, unsigned short width, unsigned short height, bool stretch, size_t *size,
                          bool *changed, int *sources, acip_render_settings_t *settings) {
  (void)changed;
  cr_assert_str_eq(id, client.client_id);
  cr_assert_not(stretch);
  cr_assert_eq(width, client.width ? client.width : 80);
  cr_assert_eq(height, client.height ? client.height : 25);
  atomic_store_bool(&client.video_render_thread_running, false);
  *sources = 1;
  settings->width = width;
  settings->height = height;
  *size = no_frame ? 0 : 2;
  if (no_frame)
    return NULL;
  char *text;
  SAFE_STRDUP(text, "X\n");
  return text;
}
static void render_video(client_info_t *recipient, acip_frame_workspace_t *workspace, const char *text, size_t size,
                         uint32_t width, uint32_t height, int sources, const acip_render_settings_t *settings) {
  cr_assert_eq(recipient, &client);
  cr_assert_not_null(workspace);
  cr_assert_str_eq(text, "X\n");
  cr_assert_eq(size, 2);
  cr_assert_eq(width, settings->width);
  cr_assert_eq(height, settings->height);
  cr_assert_eq(sources, 1);
  ++video_frames;
}
#define time_get_ns render_clock
#define platform_sleep_ns render_sleep
#define platform_sleep_ms render_wait
#define acthread_create render_create
#define thread_join render_join
#define thread_init render_init
#define thread_is_initialized render_initialized
#define opus_codec_create_encoder_channels render_encoder
#define mixer_process_excluding_source render_mix
#define server_send_audio render_audio
#define server_send_video render_video
#define create_mixed_ascii_frame_for_client render_ascii
#define any_clients_sending_video render_sources
#include "../../../src/server/render.c"
#undef time_get_ns
#undef platform_sleep_ns
#undef platform_sleep_ms
#undef acthread_create
#undef thread_join
#undef thread_init
#undef thread_is_initialized
#undef opus_codec_create_encoder_channels
#undef mixer_process_excluding_source
#undef server_send_audio
#undef server_send_video
#undef create_mixed_ascii_frame_for_client
#undef any_clients_sending_video

static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  SAFE_STRNCPY(client.client_id, "render-contract", sizeof(client.client_id));
  cr_assert_eq(mutex_init(&client.client_state_mutex, "render-contract"), ASCIICHAT_OK);
  atomic_store_bool(&client.active, true);
  atomic_store_bool(&client.video_render_thread_running, true);
  atomic_store_bool(&client.audio_render_thread_running, true);
  client.has_terminal_caps = true;
}
static void teardown(void) {
  mutex_destroy(&client.client_state_mutex);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(server_render, .init = setup, .fini = teardown, .timeout = 15);

Test(server_render, failed_worker_creation_rolls_back_flags_and_preserves_joinable_handles) {
  fail_start = 1;
  cr_assert_eq(create_client_render_threads(&server, &client), -1);
  cr_assert_not(atomic_load_bool(&client.video_render_thread_running));
  cr_assert_not(atomic_load_bool(&client.audio_render_thread_running));
  cr_assert_not(handles[0] || handles[1]);
  starts = 0;
  fail_start = 2;
  cr_assert_eq(create_client_render_threads(&server, &client), -1);
  cr_assert_not(atomic_load_bool(&client.video_render_thread_running));
  cr_assert_not(atomic_load_bool(&client.audio_render_thread_running));
  cr_assert(handles[0]);
  cr_assert_not(handles[1]);
  cr_assert_eq(stop_client_render_threads(&client), ASCIICHAT_OK);
  cr_assert_eq(joins, 1);
  cr_assert_not(handles[0]);
}
Test(server_render, join_failures_retain_handles_until_successful_retry_and_stop_is_idempotent) {
  cr_assert_eq(create_client_render_threads(&server, &client), 0);
  fail_join = 1;
  cr_assert_eq(stop_client_render_threads(&client), ERROR_THREAD);
  cr_assert(handles[0] && handles[1]);
  fail_join = 3;
  cr_assert_eq(stop_client_render_threads(&client), ERROR_THREAD);
  cr_assert_not(handles[0]);
  cr_assert(handles[1]);
  fail_join = 0;
  cr_assert_eq(stop_client_render_threads(&client), ASCIICHAT_OK);
  cr_assert_not(handles[0] || handles[1]);
  unsigned before = joins;
  cr_assert_eq(stop_client_render_threads(&client), ASCIICHAT_OK);
  cr_assert_eq(joins, before);
}
Test(server_render, missing_contexts_and_encoder_failure_do_not_start_rendering) {
  cr_assert_eq(create_client_render_threads(NULL, &client), -1);
  cr_assert_eq(create_client_render_threads(&server, NULL), -1);
  cr_assert_eq(stop_client_render_threads(NULL), ERROR_INVALID_PARAM);
  cr_assert_null(client_video_render_thread(NULL));
  cr_assert_null(client_audio_render_thread(NULL));
  fail_encoder = true;
  cr_assert_null(client_audio_render_thread(&client));
  cr_assert_eq(mixes, 0);
  cr_assert_eq(queued, 0);
}
Test(server_render, video_waits_for_capabilities_and_supplies_default_dimensions_before_delivery) {
  client.has_terminal_caps = false;
  capability_wait = 502;
  cr_assert_null(client_video_render_thread(&client));
  cr_assert_eq(waits, capability_wait);
  cr_assert_eq(video_frames, 1);
}
Test(server_render, shutdown_during_capability_wait_and_absent_frames_do_not_queue_video) {
  client.has_terminal_caps = false;
  exit_wait = true;
  cr_assert_null(client_video_render_thread(&client));
  cr_assert_eq(waits, 1);
  cr_assert_eq(video_frames, 0);
  atomic_store_bool(&g_should_exit, false);
  client.has_terminal_caps = true;
  client.width = 40;
  client.height = 12;
  client.terminal_caps.desired_fps = 30;
  no_frame = true;
  cr_assert_null(client_video_render_thread(&client));
  cr_assert_eq(video_frames, 0);
}
Test(server_render, absent_mixer_yields_without_queuing_or_leaking_the_encoder) {
  cr_assert_null(client_audio_render_thread(&client));
  cr_assert_eq(sleeps, stop_after);
  cr_assert_eq(mixes, 0);
  cr_assert_eq(queued, 0);
}
Test(server_render, partial_mixes_form_one_decodable_stereo_packet_and_failed_delivery_is_recoverable) {
  g_audio_mixer = (mixer_t *)&server;
  reject_audio = true;
  cr_assert_null(client_audio_render_thread(&client));
  cr_assert_eq(mixes, 2);
  cr_assert_eq(queued, 1);
  atomic_store_bool(&client.audio_render_thread_running, true);
  mixes = sleeps = 0;
  reject_audio = false;
  cr_assert_null(client_audio_render_thread(&client));
  cr_assert_eq(mixes, 2);
  cr_assert_eq(queued, 2);
}
