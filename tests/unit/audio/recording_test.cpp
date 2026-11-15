#include <catch2/catch_test_macros.hpp>
#include <ascii-chat/common.h>

extern "C" uint64_t g_snapshot_last_capture_elapsed_ns;
#include <catch2/catch_all.hpp>
#include <ascii-chat/audio/recording.h>
#include <ascii-chat/audio/audio.h>
#include <libplatform/atomic.h>
#include <math.h>
#include <limits.h>
#include <ascii-chat/audio/wav_writer.h>
#include <ascii-chat/media/source.h>
#include <libplatform/filesystem.h>
#include <libplatform/memory.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/video/webcam/webcam.h>
#include <ascii-chat/media/render/renderer.h>
#include <ascii-chat/media/ffmpeg_decoder.h>
#include <ascii-chat/options/rcu.h>
#include "../../../src/common/session/audio.h"

// Recording workers consume injected PCM; no physical device or callback clock is needed.
static unsigned device_stream;
static const PaDeviceInfo recording_device = {.structVersion = 2, .name = "recording speaker", .maxOutputChannels = 1, .defaultSampleRate = AUDIO_SAMPLE_RATE};
PaError Pa_Initialize(void) {
  return paNoError;
}
PaError Pa_Terminate(void) {
  return paNoError;
}
PaDeviceIndex Pa_GetDeviceCount(void) {
  return 1;
}
PaDeviceIndex Pa_GetDefaultInputDevice(void) {
  return paNoDevice;
}
PaDeviceIndex Pa_GetDefaultOutputDevice(void) {
  return 0;
}
const PaDeviceInfo *Pa_GetDeviceInfo(PaDeviceIndex device) {
  return device == 0 ? &recording_device : NULL;
}
const char *Pa_GetErrorText(PaError error) {
  return error == paNoError ? "Success" : "Recording fixture has no such device";
}
PaError Pa_OpenStream(PaStream **stream, const PaStreamParameters *input, const PaStreamParameters *output, double rate, unsigned long frames, PaStreamFlags flags, PaStreamCallback *callback,
                      void *data) {
  (void)rate;
  (void)frames;
  (void)flags;
  (void)callback;
  (void)data;
  if (input || !output || output->device != 0)
    return paInvalidDevice;
  *stream = (PaStream *)&device_stream;
  return paNoError;
}
PaError Pa_StartStream(PaStream *stream) {
  return stream == (PaStream *)&device_stream ? paNoError : paBadStreamPtr;
}
PaError Pa_StopStream(PaStream *stream) {
  return stream == (PaStream *)&device_stream ? paNoError : paBadStreamPtr;
}
PaError Pa_CloseStream(PaStream *stream) {
  return stream == (PaStream *)&device_stream ? paNoError : paBadStreamPtr;
}
PaError Pa_WriteStream(PaStream *stream, const void *buffer, unsigned long frames) {
  (void)buffer;
  (void)frames;
  return stream == (PaStream *)&device_stream ? paNoError : paBadStreamPtr;
}

/* These tests exercise webcam selection without requiring physical camera access. */
static int webcam_starts;
static rgb_pixel_t webcam_pixel = {0, 255, 0};
static image_t webcam_frame = {.w = 1, .h = 1, .pixels = &webcam_pixel};

asciichat_error_t webcam_init_context(webcam_context_t **ctx, unsigned short int index) {
  (void)index;
  webcam_starts++;
  *ctx = (webcam_context_t *)&webcam_frame;
  return ASCIICHAT_OK;
}

image_t *webcam_read_context(webcam_context_t *ctx) {
  REQUIRE((ctx) == ((webcam_context_t *)&webcam_frame));
  return &webcam_frame;
}

void webcam_cleanup_context(webcam_context_t *ctx) {
  REQUIRE((ctx) == ((webcam_context_t *)&webcam_frame));
}

static audio_recording_t *recording;
static const uint64_t epoch = 1000000000ULL;

static void setup(void) {
  options_state_init();
  REQUIRE((audio_recording_create(&recording)) == (ASCIICHAT_OK));
  audio_recording_start(recording, epoch);
}

static void teardown(void) {
  audio_recording_destroy(recording);
  recording = NULL;
  audio_terminate_portaudio_final();
  errno_clear();
  options_state_destroy();
}
TEST_CASE("recording/mixes_local_media_microphone_and_remote_without_modifying_inputs", "[recording]") {
  const float mic[] = {0.2f, -0.2f};
  const float media[] = {0.3f, -0.3f};
  const float remote[] = {0.4f, -0.4f};
  float output[2];
  audio_recording_submit(AUDIO_RECORDING_MIC, mic, 2, epoch);
  audio_recording_submit(AUDIO_RECORDING_MEDIA, media, 2, epoch);
  audio_recording_submit(AUDIO_RECORDING_REMOTE, remote, 2, epoch);
  audio_recording_read(recording, output, 2);
  REQUIRE((output[0]) == Catch::Approx(0.9f));
  REQUIRE((output[1]) == Catch::Approx(-0.9f));
  REQUIRE((mic[0]) == Catch::Approx(0.2f));
}

TEST_CASE("recording/clips_final_mix_and_rejects_nonfinite_samples", "[recording]") {
  const float a[] = {0.8f, -0.8f, NAN, INFINITY};
  float output[4];
  audio_recording_submit(AUDIO_RECORDING_MIC, a, 4, epoch);
  audio_recording_submit(AUDIO_RECORDING_REMOTE, a, 4, epoch);
  audio_recording_read(recording, output, 4);
  REQUIRE((output[0]) == (1.0f));
  REQUIRE((output[1]) == (-1.0f));
  REQUIRE((output[2]) == (0.0f));
  REQUIRE((output[3]) == (0.0f));
}

TEST_CASE("recording/session_capture_does_not_encode_uninitialized_audio_when_empty", "[recording]") {
  session_audio_ctx_t *ctx = session_audio_create(false);
  REQUIRE((ctx) != nullptr);
  float samples[960];
  REQUIRE((session_audio_read_captured(ctx, samples, 960)) == (0));
  session_audio_destroy(ctx);
}

TEST_CASE("recording/empty_input_is_temporary_and_partial_intervals_are_padded", "[recording]") {
  float output[960];
  audio_recording_read(recording, output, 480);
  const float audio[] = {0.5f, 0.25f};
  audio_recording_submit(AUDIO_RECORDING_MIC, audio, 2, epoch + 10000000ULL);
  audio_recording_read(recording, output, 960);
  REQUIRE((output[0]) == (0.5f));
  REQUIRE((output[1]) == (0.25f));
  for (int i = 2; i < 960; i++)
    REQUIRE((output[i]) == (0.0f));
}

TEST_CASE("recording/callback_jitter_does_not_duplicate_or_skip_samples", "[recording]") {
  const float audio[] = {0.5f, 0.25f};
  float output[4];
  audio_recording_submit(AUDIO_RECORDING_MEDIA, audio, 2, epoch);
  audio_recording_submit(AUDIO_RECORDING_MEDIA, audio, 2, epoch + 1000);
  audio_recording_read(recording, output, 4);
  REQUIRE((output[0]) == (0.5f));
  REQUIRE((output[1]) == (0.25f));
  REQUIRE((output[2]) == (0.5f));
  REQUIRE((output[3]) == (0.25f));
}

TEST_CASE("recording/subsequent_recording_has_no_old_timing_or_eof_state", "[recording]") {
  float output[1];
  const float audio[] = {0.5f};
  audio_recording_submit(AUDIO_RECORDING_MEDIA, audio, 1, epoch);
  audio_recording_destroy(recording);
  recording = NULL;
  REQUIRE((audio_recording_create(&recording)) == (ASCIICHAT_OK));
  audio_recording_start(recording, epoch + 1000000000ULL);
  audio_recording_submit(AUDIO_RECORDING_MIC, audio, 1, epoch + 1000000000ULL);
  audio_recording_read(recording, output, 1);
  REQUIRE((output[0]) == (0.5f));
}

TEST_CASE("recording/playback_and_recording_are_independent_consumers", "[recording]") {
  audio_context_t ctx = {0};
  ctx.initialized = true;
  ctx.playback_buffer = audio_ring_buffer_create_for_capture();
  REQUIRE((ctx.playback_buffer) != nullptr);
  const float audio[] = {0.5f, 0.25f};
  audio_recording_destroy(recording);
  REQUIRE((audio_recording_create(&recording)) == (ASCIICHAT_OK));
  audio_recording_start(recording, time_get_ns());
  REQUIRE((audio_write_samples(&ctx, audio, 2)) == (ASCIICHAT_OK));
  REQUIRE((audio_ring_buffer_available_read(ctx.playback_buffer)) == (2));
  float output[2];
  audio_recording_read(recording, output, 2);
  REQUIRE((audio_ring_buffer_available_read(ctx.playback_buffer)) == (2));
  REQUIRE((audio_ring_buffer_read(ctx.playback_buffer, output, 2)) == (2));
  audio_ring_buffer_destroy(ctx.playback_buffer);
}

static void create_audio_fixture(char *path, size_t size) {
  int fd = -1;
  REQUIRE((platform_create_temp_file(path, size, "recording-audio", &fd)) == (0));
  if (fd >= 0)
    platform_close(fd);
  wav_writer_t *writer = wav_writer_open(path, 48000, 1);
  REQUIRE((writer) != nullptr);
  float tone[48000];
  for (int i = 0; i < 48000; i++)
    tone[i] = 0.25f * sinf((float)i * 0.057595865f);
  REQUIRE((wav_writer_write(writer, tone, 48000)) == (0));
  wav_writer_close(writer);
}

TEST_CASE("recording/file_source_can_be_read_destroyed_and_reopened_repeatedly", "[recording]") {
  char path[1024];
  create_audio_fixture(path, sizeof(path));
  for (int iteration = 0; iteration < 8; iteration++) {
    media_source_t *source = media_source_create(MEDIA_SOURCE_FILE, path);
    REQUIRE((source) != nullptr);
    float samples[960];
    REQUIRE((media_source_read_audio(source, samples, 960)) > (0));
    media_source_destroy(source);
  }
  platform_delete_temp_file(path);
}

TEST_CASE("recording/small_audio_blocks_preserve_every_decoded_sample_until_end", "[recording]") {
  char path[1024];
  create_audio_fixture(path, sizeof(path));
  media_source_t *source = media_source_create(MEDIA_SOURCE_FILE, path);
  REQUIRE((source) != nullptr);
  float samples[288];
  size_t total = 0;
  while (!media_source_at_end(source)) {
    size_t count = media_source_read_audio(source, samples, 288);
    total += count / AUDIO_CHANNELS;
    REQUIRE((total) <= (48000));
    if (total < 48000)
      REQUIRE(!(media_source_at_end(source)));
  }
  REQUIRE((total) == (48000));
  media_source_destroy(source);
  platform_delete_temp_file(path);
}

TEST_CASE("recording/small_audio_blocks_preserve_resampled_duration_and_tone", "[recording]") {
  char path[1024];
  int fd = -1;
  REQUIRE((platform_create_temp_file(path, sizeof(path), "recording-resampled", &fd)) == (0));
  if (fd >= 0)
    platform_close(fd);
  wav_writer_t *writer = wav_writer_open(path, 44100, 1);
  REQUIRE((writer) != nullptr);
  float tone[44100];
  for (int i = 0; i < 44100; i++)
    tone[i] = 0.25f * sinf((float)i * (float)(2.0 * M_PI * 440.0 / 44100.0));
  REQUIRE((wav_writer_write(writer, tone, 44100)) == (0));
  wav_writer_close(writer);
  ffmpeg_decoder_t *decoder = ffmpeg_decoder_create(path);
  REQUIRE((decoder) != nullptr);
  float samples[288];
  size_t total = 0, count;
  double real = 0, imag = 0;
  while ((count = ffmpeg_decoder_read_audio_samples(decoder, samples, 288)) > 0) {
    for (size_t i = 0; i < count; i++) {
      double phase = 2.0 * M_PI * 440.0 * (double)(total + i / AUDIO_CHANNELS) / 48000.0;
      real += samples[i] * cos(phase);
      imag += samples[i] * sin(phase);
    }
    total += count / AUDIO_CHANNELS;
    REQUIRE((total) <= (48100));
  }
  REQUIRE((total) == (48000));
  REQUIRE((2.0 * hypot(real, imag) / (double)total) > (0.24));
  ffmpeg_decoder_destroy(decoder);
  platform_delete_temp_file(path);
}

TEST_CASE("recording/audio_only_file_selects_webcam_without_opening_it_during_probe", "[recording]") {
  webcam_starts = 0;
  char path[1024];
  create_audio_fixture(path, sizeof(path));
  media_source_t *source = media_source_create(MEDIA_SOURCE_FILE, path);
  REQUIRE((source) != nullptr);
  REQUIRE(media_source_has_audio(source));
  REQUIRE(media_source_has_video(source));
  REQUIRE(media_source_uses_webcam(source));
  REQUIRE(!(media_source_at_end(source)));
  REQUIRE((webcam_starts) == (0));
#ifndef __APPLE__
  /* ELF resolves the shared library's webcam calls to the mock in this executable. */
  REQUIRE((media_source_start_video(source)) == (ASCIICHAT_OK));
  REQUIRE((webcam_starts) == (1));
  REQUIRE((media_source_read_video(source)) == (&webcam_frame));
  REQUIRE((media_source_start_video(source)) == (ASCIICHAT_OK));
  REQUIRE((webcam_starts) == (1));
#endif
  float samples[960];
  REQUIRE((media_source_read_audio(source, samples, 960)) > (0));
  REQUIRE((media_source_get_position(source)) > (0));
  media_source_destroy(source);
  platform_delete_temp_file(path);
}

TEST_CASE("recording/audio_only_file_uses_test_pattern_without_opening_webcam", "[recording]") {
  webcam_starts = 0;
  options_t options = *options_get();
  options.test_pattern = true;
  options.width = 320;
  options.height = 120;
  REQUIRE((options_state_set(&options)) == (ASCIICHAT_OK));
  char path[1024];
  create_audio_fixture(path, sizeof(path));
  media_source_t *source = media_source_create(MEDIA_SOURCE_FILE, path);
  REQUIRE((source) != nullptr);
  REQUIRE(!(media_source_uses_webcam(source)));
  REQUIRE((media_source_start_video(source)) == (ASCIICHAT_OK));
  image_t *frame = media_source_read_video(source);
  REQUIRE((frame) != nullptr);
  REQUIRE((frame->w) == (320));
  REQUIRE((frame->h) == (240));
  REQUIRE((webcam_starts) == (0));
  float samples[960];
  REQUIRE((media_source_read_audio(source, samples, 960)) > (0));
  REQUIRE((media_source_get_position(source)) > (0));
  media_source_destroy(source);
  platform_delete_temp_file(path);
}

static void wait_for_captured_samples(audio_context_t *ctx, size_t count) {
  uint64_t deadline = time_get_ns() + 2000000000ULL;
  while (audio_ring_buffer_available_read(ctx->capture_buffer) < count && time_get_ns() < deadline)
    platform_sleep_ns(10000000ULL);
  REQUIRE((audio_ring_buffer_available_read(ctx->capture_buffer)) >= (count));
}

static float recorded_energy_since(uint64_t started) {
  size_t count = (size_t)((time_get_ns() - started) * 48000 / 1000000000ULL) + 960;
  float *output = SAFE_CALLOC(count, sizeof(float), float *);
  audio_recording_read(recording, output, count);
  float energy = 0;
  for (size_t i = 0; i < count; i++)
    energy += output[i] * output[i];
  SAFE_FREE(output);
  return energy;
}

TEST_CASE("recording/microphone_worker_copies_audio_without_draining_transmission", "[recording]") {
  audio_context_t ctx = {0};
  REQUIRE((audio_init(&ctx)) == (ASCIICHAT_OK));
  ctx.playback_only = true;
  ctx.monitor_local_media = true;
  float mic[960];
  for (int i = 0; i < 960; i++)
    mic[i] = 0.25f * sinf(2.0f * 3.14159265f * 440.0f * i / 48000.0f);
  audio_recording_destroy(recording);
  REQUIRE((audio_recording_create(&recording)) == (ASCIICHAT_OK));
  REQUIRE((audio_start_duplex(&ctx)) == (ASCIICHAT_OK));
  uint64_t started = time_get_ns();
  audio_recording_start(recording, started);
  REQUIRE((audio_ring_buffer_write(ctx.raw_capture_rb, mic, 960)) == (ASCIICHAT_OK));
  wait_for_captured_samples(&ctx, 960);
  for (unsigned i = 0; i < 25; ++i) {
    REQUIRE((audio_ring_buffer_write(ctx.raw_capture_rb, mic, 480)) == (ASCIICHAT_OK));
    platform_sleep_ns(10 * NS_PER_MS_INT);
  }
  REQUIRE((audio_ring_buffer_available_read(ctx.capture_buffer)) <= (AUDIO_JITTER_HIGH_WATER_MARK));
  float energy = recorded_energy_since(started);
  REQUIRE((energy) > (1.0f));
  REQUIRE((audio_ring_buffer_available_read(ctx.capture_buffer)) > (0));
  audio_stop_duplex(&ctx);
  audio_destroy(&ctx);
}

TEST_CASE("recording/file_audio_worker_feeds_transmission_playback_and_recording", "[recording]") {
  char path[1024];
  create_audio_fixture(path, sizeof(path));
  media_source_t *source = media_source_create(MEDIA_SOURCE_FILE, path);
  REQUIRE((source) != nullptr);
  audio_context_t ctx = {0};
  REQUIRE((audio_init(&ctx)) == (ASCIICHAT_OK));
  ctx.capture_media_source = source;
  ctx.monitor_local_media = true;
  ctx.playback_only = true;
  audio_recording_destroy(recording);
  REQUIRE((audio_recording_create(&recording)) == (ASCIICHAT_OK));
  REQUIRE((audio_start_duplex(&ctx)) == (ASCIICHAT_OK));
  REQUIRE(!(ctx.playback_buffer->jitter_buffer_enabled));
  uint64_t started = time_get_ns();
  audio_recording_start(recording, started);
  wait_for_captured_samples(&ctx, 960);
  platform_sleep_ns(300 * NS_PER_MS_INT);
  REQUIRE((audio_ring_buffer_available_read(ctx.capture_buffer)) <= (AUDIO_JITTER_HIGH_WATER_MARK));
  float energy = recorded_energy_since(started);
  REQUIRE((energy) > (1.0f));
  REQUIRE((audio_stop_duplex(&ctx)) == (ASCIICHAT_OK));
  audio_destroy(&ctx);
  media_source_destroy(source);
  platform_delete_temp_file(path);
}

TEST_CASE("recording/remote_audio_selection_retains_network_jitter_buffer", "[recording]") {
  char path[1024];
  create_audio_fixture(path, sizeof(path));
  media_source_t *source = media_source_create(MEDIA_SOURCE_FILE, path);
  REQUIRE((source) != nullptr);
  options_t options = *options_get();
  options.audio_capture_source = AUDIO_CAPTURE_SOURCE_REMOTE;
  options_state_set(&options);
  audio_context_t ctx = {0};
  REQUIRE((audio_init(&ctx)) == (ASCIICHAT_OK));
  ctx.capture_media_source = source;
  ctx.monitor_local_media = true;
  ctx.playback_only = true;
  REQUIRE((audio_start_duplex(&ctx)) == (ASCIICHAT_OK));
  bool jitter_enabled = ctx.playback_buffer->jitter_buffer_enabled;
  REQUIRE((audio_stop_duplex(&ctx)) == (ASCIICHAT_OK));
  audio_destroy(&ctx);
  media_source_destroy(source);
  platform_delete_temp_file(path);
  REQUIRE(jitter_enabled);
}

TEST_CASE("recording/media_audio_recording_runs_without_input_or_output_devices", "[recording]") {
  options_t options = *options_get();
  options.audio_capture_source = AUDIO_CAPTURE_SOURCE_MEDIA;
  options.speakers_index = INT_MAX;
  REQUIRE((options_state_set(&options)) == (ASCIICHAT_OK));

  char path[1024];
  create_audio_fixture(path, sizeof(path));
  media_source_t *source = media_source_create(MEDIA_SOURCE_FILE, path);
  REQUIRE((source) != nullptr);
  audio_context_t ctx = {0};
  REQUIRE((audio_init(&ctx)) == (ASCIICHAT_OK));
  ctx.capture_media_source = source;
  ctx.playback_only = true;
  audio_recording_destroy(recording);
  REQUIRE((audio_recording_create(&recording)) == (ASCIICHAT_OK));
  REQUIRE((audio_start_duplex(&ctx)) == (ASCIICHAT_OK));
  REQUIRE((ctx.duplex_stream) == nullptr);
  REQUIRE((ctx.input_stream) == nullptr);
  REQUIRE((ctx.output_stream) == nullptr);

  uint64_t started = time_get_ns();
  audio_recording_start(recording, started);
  wait_for_captured_samples(&ctx, 960);
  REQUIRE((recorded_energy_since(started)) > (1.0f));

  REQUIRE((audio_stop_duplex(&ctx)) == (ASCIICHAT_OK));
  audio_destroy(&ctx);
  media_source_destroy(source);
  platform_delete_temp_file(path);
}

TEST_CASE("recording/session_audio_stops_worker_before_releasing_borrowed_media", "[recording]") {
  char path[1024];
  create_audio_fixture(path, sizeof(path));
  media_source_t *source = media_source_create(MEDIA_SOURCE_FILE, path);
  REQUIRE((source) != nullptr);
  session_audio_ctx_t *ctx = session_audio_create(false);
  REQUIRE((ctx) != nullptr);
  session_audio_set_capture_source(ctx, source);
  REQUIRE((session_audio_start_duplex(ctx)) == (ASCIICHAT_OK));
  float samples[480];
  uint64_t deadline = time_get_ns() + 2000000000ULL;
  size_t count = 0;
  while (count == 0 && time_get_ns() < deadline) {
    count = session_audio_read_captured(ctx, samples, 480);
    if (count == 0)
      platform_sleep_ns(10000000ULL);
  }
  REQUIRE((count) > (0));
  session_audio_destroy(ctx);
  media_source_destroy(source);
  platform_delete_temp_file(path);
}

TEST_CASE("recording/rendered_video_contains_mixed_audio_and_flushes_the_tail", "[recording]") {
  char path[1024];
  int fd = -1;
  REQUIRE((platform_create_temp_file(path, sizeof(path), "recording-video", &fd)) == (0));
  if (fd >= 0)
    platform_close(fd);
  audio_recording_destroy(recording);
  recording = NULL;
  render_file_ctx_t *render = NULL;
  REQUIRE((render_file_create(path, 8, 4, 10, 0, &render)) == (ASCIICHAT_OK));
  const char *frame = "\033[Hhello\nworld\nhello\nworld";
  REQUIRE((render_file_write_frame(render, frame, epoch)) == (ASCIICHAT_OK));
  float mic[4800 * AUDIO_CHANNELS], remote[4800 * AUDIO_CHANNELS];
  for (int i = 0; i < 4800 * AUDIO_CHANNELS; i++) {
    mic[i] = 0.2f * sinf((float)(i / AUDIO_CHANNELS) * 0.057595865f);
    remote[i] = 0.15f * sinf((float)(i / AUDIO_CHANNELS) * 0.11519173f);
  }
  for (int i = 0; i < 9; i++) {
    audio_recording_submit(AUDIO_RECORDING_MIC, mic, 4800 * AUDIO_CHANNELS, epoch + (uint64_t)i * 100000000ULL);
    audio_recording_submit(AUDIO_RECORDING_REMOTE, remote, 4800 * AUDIO_CHANNELS, epoch + (uint64_t)i * 100000000ULL);
    REQUIRE((render_file_write_frame(render, frame, epoch + (uint64_t)(i + 1) * 100000000ULL)) == (ASCIICHAT_OK));
  }
  REQUIRE((render_file_destroy(render)) == (ASCIICHAT_OK));
  ffmpeg_decoder_t *decoder = ffmpeg_decoder_create(path);
  REQUIRE((decoder) != nullptr);
  REQUIRE(ffmpeg_decoder_has_video(decoder));
  REQUIRE(ffmpeg_decoder_has_audio(decoder));
  float samples[960];
  size_t total = 0, count;
  float energy = 0;
  while ((count = ffmpeg_decoder_read_audio_samples(decoder, samples, 960)) > 0) {
    total += count / AUDIO_CHANNELS;
    for (size_t i = 0; i < count; i++)
      energy += samples[i] * samples[i];
  }
  REQUIRE((total) >= (48000));
  REQUIRE((total) < (50000));
  REQUIRE((energy) > (500.0f));
  ffmpeg_decoder_destroy(decoder);
  platform_delete_temp_file(path);
}

TEST_CASE("recording/both_source_mixes_microphone_and_file_in_the_same_outgoing_block", "[recording]") {
  options_t options = *options_get();
  options.audio_capture_source = AUDIO_CAPTURE_SOURCE_BOTH;
  REQUIRE((options_state_set(&options)) == (ASCIICHAT_OK));
  char path[1024];
  create_audio_fixture(path, sizeof(path));
  media_source_t *source = media_source_create(MEDIA_SOURCE_FILE, path);
  REQUIRE((source) != nullptr);
  audio_context_t ctx = {0};
  REQUIRE((audio_init(&ctx)) == (ASCIICHAT_OK));
  ctx.capture_media_source = source;
  ctx.playback_only = true;
  float mic[960 * AUDIO_CHANNELS];
  for (int i = 0; i < 960 * AUDIO_CHANNELS; i++)
    mic[i] = 0.2f;
  REQUIRE((audio_start_duplex(&ctx)) == (ASCIICHAT_OK));
  REQUIRE((audio_ring_buffer_write(ctx.raw_capture_rb, mic, 960 * AUDIO_CHANNELS)) == (ASCIICHAT_OK));
  wait_for_captured_samples(&ctx, 960 * AUDIO_CHANNELS);
  REQUIRE((audio_ring_buffer_available_read(ctx.capture_buffer)) == (960 * AUDIO_CHANNELS));
  float outgoing[960 * AUDIO_CHANNELS];
  REQUIRE((audio_ring_buffer_read(ctx.capture_buffer, outgoing, 960 * AUDIO_CHANNELS)) == (960 * AUDIO_CHANNELS));
  float mean = 0, variation = 0;
  for (int i = 0; i < 960 * AUDIO_CHANNELS; i++) {
    mean += outgoing[i];
    variation += (outgoing[i] - 0.2f) * (outgoing[i] - 0.2f);
  }
  REQUIRE((mean / (960 * AUDIO_CHANNELS)) == Catch::Approx(0.2f));
  REQUIRE((variation) > (10.0f));
  audio_stop_duplex(&ctx);
  audio_destroy(&ctx);
  media_source_destroy(source);
  platform_delete_temp_file(path);
}

TEST_CASE("recording/file_snapshot_ignores_capture_startup_delay", "[recording]") {
  options_t options = *options_get();
  options.snapshot_mode = true;
  options.snapshot_delay = 3;
  REQUIRE((options_state_set(&options)) == (ASCIICHAT_OK));
  extern uint64_t g_snapshot_actual_duration_ms;
  extern uint64_t g_snapshot_last_capture_elapsed_ns;
  g_snapshot_actual_duration_ms = 5700;
  g_snapshot_last_capture_elapsed_ns = 2900000000ULL;
  char path[1024];
  int fd = -1;
  REQUIRE((platform_create_temp_file(path, sizeof(path), "recording-snapshot", &fd)) == (0));
  if (fd >= 0)
    platform_close(fd);
  audio_recording_destroy(recording);
  recording = NULL;
  render_file_ctx_t *render = NULL;
  REQUIRE((render_file_create(path, 8, 4, 10, 0, &render)) == (ASCIICHAT_OK));
  for (int i = 0; i < 30; i++)
    REQUIRE((render_file_write_frame(render, "hello", epoch + (uint64_t)i * 100000000ULL)) == (ASCIICHAT_OK));
  REQUIRE((render_file_destroy(render)) == (ASCIICHAT_OK));
  ffmpeg_decoder_t *decoder = ffmpeg_decoder_create(path);
  REQUIRE((decoder) != nullptr);
  REQUIRE((ffmpeg_decoder_get_duration(decoder)) == Catch::Approx(3.0));
  ffmpeg_decoder_destroy(decoder);
  platform_delete_temp_file(path);
  g_snapshot_actual_duration_ms = 0;
  g_snapshot_last_capture_elapsed_ns = 0;
}

TEST_CASE("recording/live_recording_uses_elapsed_time_instead_of_received_frame_count", "[recording]") {
  char path[1024];
  int fd = -1;
  REQUIRE((platform_create_temp_file(path, sizeof(path), "recording-live", &fd)) == (0));
  if (fd >= 0)
    platform_close(fd);
  audio_recording_destroy(recording);
  recording = NULL;
  render_file_ctx_t *render = NULL;
  REQUIRE((render_file_create(path, 8, 4, 10, 0, &render)) == (ASCIICHAT_OK));
  render_file_set_live_timing(render);
  REQUIRE((render_file_write_frame(render, "hello", epoch)) == (ASCIICHAT_OK));
  /* Repeated frames inside one output interval are dropped, but a one-second gap is preserved. */
  REQUIRE((render_file_write_frame(render, "hello", epoch + 1000000ULL)) == (ASCIICHAT_OK));
  REQUIRE((render_file_write_frame(render, "hello", epoch + 1000000000ULL)) == (ASCIICHAT_OK));
  REQUIRE((render_file_destroy(render)) == (ASCIICHAT_OK));
  ffmpeg_decoder_t *decoder = ffmpeg_decoder_create(path);
  REQUIRE((decoder) != nullptr);
  REQUIRE((ffmpeg_decoder_get_duration(decoder)) == Catch::Approx(1.1));
  float samples[960];
  size_t total = 0, count;
  while ((count = ffmpeg_decoder_read_audio_samples(decoder, samples, 960)) > 0)
    total += count / AUDIO_CHANNELS;
  REQUIRE((total) >= (52800));
  REQUIRE((total) < (55000));
  ffmpeg_decoder_destroy(decoder);
  platform_delete_temp_file(path);
}

TEST_CASE("recording/file_pause_gap_keeps_audio_and_video_timelines_aligned", "[recording]") {
  char path[1024];
  int fd = -1;
  REQUIRE((platform_create_temp_file(path, sizeof(path), "recording-pause", &fd)) == (0));
  if (fd >= 0)
    platform_close(fd);
  audio_recording_destroy(recording);
  recording = NULL;

  render_file_ctx_t *render = NULL;
  REQUIRE((render_file_create(path, 8, 4, 10, 0, &render)) == (ASCIICHAT_OK));
  render_file_set_live_timing(render);
  const char *frame = "\033[Hhello\nworld\nhello\nworld";
  float tone[4800 * AUDIO_CHANNELS], silence[4800 * AUDIO_CHANNELS] = {0};
  for (int i = 0; i < 4800 * AUDIO_CHANNELS; i++)
    tone[i] = 0.2f * sinf((float)(i / AUDIO_CHANNELS) * 0.057595865f);

  REQUIRE((render_file_write_frame(render, frame, epoch)) == (ASCIICHAT_OK));
  for (int i = 0; i < 10; i++) {
    uint64_t timestamp = epoch + (uint64_t)i * 100000000ULL;
    audio_recording_submit(AUDIO_RECORDING_MEDIA, tone, 4800 * AUDIO_CHANNELS, timestamp);
    REQUIRE((render_file_write_frame(render, frame, timestamp + 100000000ULL)) == (ASCIICHAT_OK));
  }

  /* Paused playback submits silence while video frames stop arriving. */
  for (int i = 10; i < 20; i++)
    audio_recording_submit(AUDIO_RECORDING_MEDIA, silence, 4800 * AUDIO_CHANNELS, epoch + (uint64_t)i * 100000000ULL);
  for (int i = 20; i < 30; i++) {
    uint64_t timestamp = epoch + (uint64_t)i * 100000000ULL;
    audio_recording_submit(AUDIO_RECORDING_MEDIA, tone, 4800 * AUDIO_CHANNELS, timestamp);
    REQUIRE((render_file_write_frame(render, frame, timestamp + 100000000ULL)) == (ASCIICHAT_OK));
  }

  REQUIRE((render_file_destroy(render)) == (ASCIICHAT_OK));
  ffmpeg_decoder_t *decoder = ffmpeg_decoder_create(path);
  REQUIRE((decoder) != nullptr);
  REQUIRE(ffmpeg_decoder_has_video(decoder));
  REQUIRE(ffmpeg_decoder_has_audio(decoder));
  REQUIRE((ffmpeg_decoder_get_duration(decoder)) == Catch::Approx(3.1));

  float samples[12000];
  size_t count;
  float rms[3] = {0};
  const size_t window_starts[3] = {12000, 60000, 108000};
  float window[3] = {0};
  size_t total = 0;
  while ((count = ffmpeg_decoder_read_audio_samples(decoder, samples, 12000)) > 0) {
    for (size_t i = 0; i < count; i++, total++) {
      for (size_t window_index = 0; window_index < 3; window_index++) {
        if (total / AUDIO_CHANNELS >= window_starts[window_index] && total / AUDIO_CHANNELS < window_starts[window_index] + 24000)
          window[window_index] += samples[i] * samples[i];
      }
    }
  }
  REQUIRE((total / AUDIO_CHANNELS) >= (148000));
  for (size_t i = 0; i < 3; i++)
    rms[i] = sqrtf(window[i] / (24000.0f * AUDIO_CHANNELS));
  REQUIRE((rms[0]) > (0.03f));
  REQUIRE((rms[1]) < (0.01f));
  REQUIRE((rms[2]) > (0.03f));
  ffmpeg_decoder_destroy(decoder);
  platform_delete_temp_file(path);
}
