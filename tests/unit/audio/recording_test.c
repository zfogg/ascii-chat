#include <criterion/criterion.h>
#include <ascii-chat/audio/recording.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/atomic.h>
#include <math.h>
#include <ascii-chat/audio/wav_writer.h>
#include <ascii-chat/media/source.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/video/webcam/webcam.h>
#include <ascii-chat/media/render/renderer.h>
#include <ascii-chat/media/ffmpeg_decoder.h>
#include <ascii-chat/options/rcu.h>
#include "../../../src/common/session/audio.h"

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
  cr_assert_eq(ctx, (webcam_context_t *)&webcam_frame);
  return &webcam_frame;
}

void webcam_cleanup_context(webcam_context_t *ctx) {
  cr_assert_eq(ctx, (webcam_context_t *)&webcam_frame);
}

static audio_recording_t *recording;
static const uint64_t epoch = 1000000000ULL;

static void setup(void) {
  options_state_init();
  cr_assert_eq(audio_recording_create(&recording), ASCIICHAT_OK);
  audio_recording_start(recording, epoch);
}

static void teardown(void) {
  audio_recording_destroy(recording);
  recording = NULL;
  asciichat_clear_errno();
  options_state_destroy();
}

TestSuite(recording, .init = setup, .fini = teardown);

Test(recording, mixes_local_media_microphone_and_remote_without_modifying_inputs) {
  const float mic[] = {0.2f, -0.2f};
  const float media[] = {0.3f, -0.3f};
  const float remote[] = {0.4f, -0.4f};
  float output[2];
  audio_recording_submit(AUDIO_RECORDING_MIC, mic, 2, epoch);
  audio_recording_submit(AUDIO_RECORDING_MEDIA, media, 2, epoch);
  audio_recording_submit(AUDIO_RECORDING_REMOTE, remote, 2, epoch);
  audio_recording_read(recording, output, 2);
  cr_assert_float_eq(output[0], 0.9f, 0.00001f);
  cr_assert_float_eq(output[1], -0.9f, 0.00001f);
  cr_assert_float_eq(mic[0], 0.2f, 0.00001f);
}

Test(recording, clips_final_mix_and_rejects_nonfinite_samples) {
  const float a[] = {0.8f, -0.8f, NAN, INFINITY};
  float output[4];
  audio_recording_submit(AUDIO_RECORDING_MIC, a, 4, epoch);
  audio_recording_submit(AUDIO_RECORDING_REMOTE, a, 4, epoch);
  audio_recording_read(recording, output, 4);
  cr_assert_eq(output[0], 1.0f);
  cr_assert_eq(output[1], -1.0f);
  cr_assert_eq(output[2], 0.0f);
  cr_assert_eq(output[3], 0.0f);
}

Test(recording, session_capture_does_not_encode_uninitialized_audio_when_empty) {
  session_audio_ctx_t *ctx = session_audio_create(false);
  cr_assert_not_null(ctx);
  float samples[960];
  cr_assert_eq(session_audio_read_captured(ctx, samples, 960), 0);
  session_audio_destroy(ctx);
}

Test(recording, empty_input_is_temporary_and_partial_intervals_are_padded) {
  float output[960];
  audio_recording_read(recording, output, 480);
  const float audio[] = {0.5f, 0.25f};
  audio_recording_submit(AUDIO_RECORDING_MIC, audio, 2, epoch + 10000000ULL);
  audio_recording_read(recording, output, 960);
  cr_assert_eq(output[0], 0.5f);
  cr_assert_eq(output[1], 0.25f);
  for (int i = 2; i < 960; i++)
    cr_assert_eq(output[i], 0.0f);
}

Test(recording, callback_jitter_does_not_duplicate_or_skip_samples) {
  const float audio[] = {0.5f, 0.25f};
  float output[4];
  audio_recording_submit(AUDIO_RECORDING_MEDIA, audio, 2, epoch);
  audio_recording_submit(AUDIO_RECORDING_MEDIA, audio, 2, epoch + 1000);
  audio_recording_read(recording, output, 4);
  cr_assert_eq(output[0], 0.5f);
  cr_assert_eq(output[1], 0.25f);
  cr_assert_eq(output[2], 0.5f);
  cr_assert_eq(output[3], 0.25f);
}

Test(recording, subsequent_recording_has_no_old_timing_or_eof_state) {
  float output[1];
  const float audio[] = {0.5f};
  audio_recording_submit(AUDIO_RECORDING_MEDIA, audio, 1, epoch);
  audio_recording_destroy(recording);
  recording = NULL;
  cr_assert_eq(audio_recording_create(&recording), ASCIICHAT_OK);
  audio_recording_start(recording, epoch + 1000000000ULL);
  audio_recording_submit(AUDIO_RECORDING_MIC, audio, 1, epoch + 1000000000ULL);
  audio_recording_read(recording, output, 1);
  cr_assert_eq(output[0], 0.5f);
}

Test(recording, playback_and_recording_are_independent_consumers) {
  audio_context_t ctx = {0};
  ctx.initialized = true;
  ctx.playback_buffer = audio_ring_buffer_create_for_capture();
  cr_assert_not_null(ctx.playback_buffer);
  const float audio[] = {0.5f, 0.25f};
  audio_recording_destroy(recording);
  cr_assert_eq(audio_recording_create(&recording), ASCIICHAT_OK);
  audio_recording_start(recording, time_get_ns());
  cr_assert_eq(audio_write_samples(&ctx, audio, 2), ASCIICHAT_OK);
  cr_assert_eq(audio_ring_buffer_available_read(ctx.playback_buffer), 2);
  float output[2];
  audio_recording_read(recording, output, 2);
  cr_assert_eq(audio_ring_buffer_available_read(ctx.playback_buffer), 2);
  cr_assert_eq(audio_ring_buffer_read(ctx.playback_buffer, output, 2), 2);
  audio_ring_buffer_destroy(ctx.playback_buffer);
}

static void create_audio_fixture(char *path, size_t size) {
  int fd = -1;
  cr_assert_eq(platform_create_temp_file(path, size, "recording-audio", &fd), 0);
  if (fd >= 0)
    platform_close(fd);
  wav_writer_t *writer = wav_writer_open(path, 48000, 1);
  cr_assert_not_null(writer);
  float tone[48000];
  for (int i = 0; i < 48000; i++)
    tone[i] = 0.25f * sinf((float)i * 0.057595865f);
  cr_assert_eq(wav_writer_write(writer, tone, 48000), 0);
  wav_writer_close(writer);
}

Test(recording, audio_only_file_selects_webcam_without_opening_it_during_probe) {
  webcam_starts = 0;
  char path[1024];
  create_audio_fixture(path, sizeof(path));
  media_source_t *source = media_source_create(MEDIA_SOURCE_FILE, path);
  cr_assert_not_null(source);
  cr_assert(media_source_has_audio(source));
  cr_assert(media_source_has_video(source));
  cr_assert(media_source_uses_webcam(source));
  cr_assert_not(media_source_at_end(source));
  cr_assert_eq(webcam_starts, 0);
#ifndef __APPLE__
  /* ELF resolves the shared library's webcam calls to the mock in this executable. */
  cr_assert_eq(media_source_start_video(source), ASCIICHAT_OK);
  cr_assert_eq(webcam_starts, 1);
  cr_assert_eq(media_source_read_video(source), &webcam_frame);
  cr_assert_eq(media_source_start_video(source), ASCIICHAT_OK);
  cr_assert_eq(webcam_starts, 1);
#endif
  float samples[960];
  cr_assert_gt(media_source_read_audio(source, samples, 960), 0);
  cr_assert_gt(media_source_get_position(source), 0);
  media_source_destroy(source);
  platform_delete_temp_file(path);
}

Test(recording, microphone_worker_copies_audio_without_draining_transmission, .timeout = 10) {
  audio_context_t ctx = {0};
  cr_assert_eq(audio_init(&ctx), ASCIICHAT_OK);
  ctx.playback_only = true;
  float mic[960];
  for (int i = 0; i < 960; i++)
    mic[i] = 0.25f * sinf(2.0f * 3.14159265f * 440.0f * i / 48000.0f);
  audio_recording_destroy(recording);
  cr_assert_eq(audio_recording_create(&recording), ASCIICHAT_OK);
  cr_assert_eq(audio_start_duplex(&ctx), ASCIICHAT_OK);
  audio_recording_start(recording, time_get_ns());
  cr_assert_eq(audio_ring_buffer_write(ctx.raw_capture_rb, mic, 960), ASCIICHAT_OK);
  platform_sleep_ns(100000000ULL);
  cr_assert_gt(audio_ring_buffer_available_read(ctx.capture_buffer), 0);
  float output[8192];
  audio_recording_read(recording, output, 8192);
  float energy = 0;
  for (int i = 0; i < 8192; i++)
    energy += output[i] * output[i];
  cr_assert_gt(energy, 1.0f, "Mic energy %f, sensitivity %f, queued %zu", energy, GET_OPTION(microphone_sensitivity),
               audio_ring_buffer_available_read(ctx.capture_buffer));
  cr_assert_gt(audio_ring_buffer_available_read(ctx.capture_buffer), 0);
  audio_stop_duplex(&ctx);
  audio_destroy(&ctx);
}

Test(recording, file_audio_worker_feeds_transmission_playback_and_recording, .timeout = 10) {
  char path[1024];
  create_audio_fixture(path, sizeof(path));
  media_source_t *source = media_source_create(MEDIA_SOURCE_FILE, path);
  cr_assert_not_null(source);
  audio_context_t ctx = {0};
  cr_assert_eq(audio_init(&ctx), ASCIICHAT_OK);
  ctx.capture_media_source = source;
  ctx.monitor_local_media = true;
  ctx.playback_only = true;
  audio_recording_destroy(recording);
  cr_assert_eq(audio_recording_create(&recording), ASCIICHAT_OK);
  cr_assert_eq(audio_start_duplex(&ctx), ASCIICHAT_OK);
  audio_recording_start(recording, time_get_ns());
  platform_sleep_ns(250000000ULL);
  cr_assert_gt(audio_ring_buffer_available_read(ctx.capture_buffer), 0);
  float output[8192];
  audio_recording_read(recording, output, 8192);
  float energy = 0;
  for (int i = 0; i < 8192; i++)
    energy += output[i] * output[i];
  cr_assert_gt(energy, 1.0f, "Recorded file audio must contain the tone, not only a silent track");
  cr_assert_eq(audio_stop_duplex(&ctx), ASCIICHAT_OK);
  audio_destroy(&ctx);
  media_source_destroy(source);
  platform_delete_temp_file(path);
}

Test(recording, session_audio_stops_worker_before_releasing_borrowed_media, .timeout = 10) {
  char path[1024];
  create_audio_fixture(path, sizeof(path));
  media_source_t *source = media_source_create(MEDIA_SOURCE_FILE, path);
  cr_assert_not_null(source);
  session_audio_ctx_t *ctx = session_audio_create(false);
  cr_assert_not_null(ctx);
  session_audio_set_capture_source(ctx, source);
  cr_assert_eq(session_audio_start_duplex(ctx), ASCIICHAT_OK);
  platform_sleep_ns(100000000ULL);
  float samples[480];
  cr_assert_gt(session_audio_read_captured(ctx, samples, 480), 0);
  session_audio_destroy(ctx);
  media_source_destroy(source);
  platform_delete_temp_file(path);
}

Test(recording, rendered_video_contains_mixed_audio_and_flushes_the_tail, .timeout = 15) {
  char path[1024];
  int fd = -1;
  cr_assert_eq(platform_create_temp_file(path, sizeof(path), "recording-video", &fd), 0);
  if (fd >= 0)
    platform_close(fd);
  audio_recording_destroy(recording);
  recording = NULL;
  render_file_ctx_t *render = NULL;
  cr_assert_eq(render_file_create(path, 8, 4, 10, 0, &render), ASCIICHAT_OK);
  const char *frame = "\033[Hhello\nworld\nhello\nworld";
  cr_assert_eq(render_file_write_frame(render, frame, epoch), ASCIICHAT_OK);
  float mic[4800], remote[4800];
  for (int i = 0; i < 4800; i++) {
    mic[i] = 0.2f * sinf((float)i * 0.057595865f);
    remote[i] = 0.15f * sinf((float)i * 0.11519173f);
  }
  for (int i = 0; i < 9; i++) {
    audio_recording_submit(AUDIO_RECORDING_MIC, mic, 4800, epoch + (uint64_t)i * 100000000ULL);
    audio_recording_submit(AUDIO_RECORDING_REMOTE, remote, 4800, epoch + (uint64_t)i * 100000000ULL);
    cr_assert_eq(render_file_write_frame(render, frame, epoch + (uint64_t)(i + 1) * 100000000ULL), ASCIICHAT_OK);
  }
  cr_assert_eq(render_file_destroy(render), ASCIICHAT_OK);
  ffmpeg_decoder_t *decoder = ffmpeg_decoder_create(path);
  cr_assert_not_null(decoder);
  cr_assert(ffmpeg_decoder_has_video(decoder));
  cr_assert(ffmpeg_decoder_has_audio(decoder));
  float samples[960];
  size_t total = 0, count;
  float energy = 0;
  while ((count = ffmpeg_decoder_read_audio_samples(decoder, samples, 960)) > 0) {
    total += count;
    for (size_t i = 0; i < count; i++)
      energy += samples[i] * samples[i];
  }
  cr_assert_geq(total, 48000);
  cr_assert_lt(total, 50000);
  cr_assert_gt(energy, 500.0f);
  ffmpeg_decoder_destroy(decoder);
  platform_delete_temp_file(path);
}

Test(recording, both_source_mixes_microphone_and_file_in_the_same_outgoing_block, .timeout = 10) {
  options_t options = *options_get();
  options.audio_source = AUDIO_SOURCE_BOTH;
  cr_assert_eq(options_state_set(&options), ASCIICHAT_OK);
  char path[1024];
  create_audio_fixture(path, sizeof(path));
  media_source_t *source = media_source_create(MEDIA_SOURCE_FILE, path);
  cr_assert_not_null(source);
  audio_context_t ctx = {0};
  cr_assert_eq(audio_init(&ctx), ASCIICHAT_OK);
  ctx.capture_media_source = source;
  ctx.playback_only = true;
  float mic[960];
  for (int i = 0; i < 960; i++)
    mic[i] = 0.2f;
  cr_assert_eq(audio_start_duplex(&ctx), ASCIICHAT_OK);
  cr_assert_eq(audio_ring_buffer_write(ctx.raw_capture_rb, mic, 960), ASCIICHAT_OK);
  platform_sleep_ns(100000000ULL);
  cr_assert_eq(audio_ring_buffer_available_read(ctx.capture_buffer), 960);
  float outgoing[960];
  cr_assert_eq(audio_ring_buffer_read(ctx.capture_buffer, outgoing, 960), 960);
  float mean = 0, variation = 0;
  for (int i = 0; i < 960; i++) {
    mean += outgoing[i];
    variation += (outgoing[i] - 0.2f) * (outgoing[i] - 0.2f);
  }
  cr_assert_float_eq(mean / 960, 0.2f, 0.01f);
  cr_assert_gt(variation, 10.0f, "Outgoing microphone blocks must also contain the file tone");
  audio_stop_duplex(&ctx);
  audio_destroy(&ctx);
  media_source_destroy(source);
  platform_delete_temp_file(path);
}

Test(recording, live_recording_uses_elapsed_time_instead_of_received_frame_count, .timeout = 15) {
  char path[1024];
  int fd = -1;
  cr_assert_eq(platform_create_temp_file(path, sizeof(path), "recording-live", &fd), 0);
  if (fd >= 0)
    platform_close(fd);
  audio_recording_destroy(recording);
  recording = NULL;
  render_file_ctx_t *render = NULL;
  cr_assert_eq(render_file_create(path, 8, 4, 10, 0, &render), ASCIICHAT_OK);
  render_file_set_live_timing(render);
  cr_assert_eq(render_file_write_frame(render, "hello", epoch), ASCIICHAT_OK);
  /* Repeated frames inside one output interval are dropped, but a one-second gap is preserved. */
  cr_assert_eq(render_file_write_frame(render, "hello", epoch + 1000000ULL), ASCIICHAT_OK);
  cr_assert_eq(render_file_write_frame(render, "hello", epoch + 1000000000ULL), ASCIICHAT_OK);
  cr_assert_eq(render_file_destroy(render), ASCIICHAT_OK);
  ffmpeg_decoder_t *decoder = ffmpeg_decoder_create(path);
  cr_assert_not_null(decoder);
  cr_assert_float_eq(ffmpeg_decoder_get_duration(decoder), 1.1, 0.03);
  float samples[960];
  size_t total = 0, count;
  while ((count = ffmpeg_decoder_read_audio_samples(decoder, samples, 960)) > 0)
    total += count;
  cr_assert_geq(total, 52800);
  cr_assert_lt(total, 55000);
  ffmpeg_decoder_destroy(decoder);
  platform_delete_temp_file(path);
}
