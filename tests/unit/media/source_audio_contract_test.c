#include <criterion/criterion.h>
#include <ascii-chat/media/source.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/log/log.h>
#include <string.h>

static char path[PLATFORM_MAX_PATH_LENGTH];
static media_source_t *source;
static audio_context_t audio;
static void le32(unsigned char *output, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    output[i] = (unsigned char)((value >> (8 * i)) & 255);
}
static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
  log_set_terminal_output(false);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  options_t *options = SAFE_MALLOC(sizeof(*options), options_t *);
  *options = options_t_new();
  options->test_pattern = true;
  options->width = 40;
  options->height = 12;
  cr_assert_eq(options_state_set(options), ASCIICHAT_OK);
  SAFE_FREE(options);
  int fd = -1;
  cr_assert_eq(platform_create_temp_file(path, sizeof(path), "source-audio", &fd), 0);
  cr_assert_eq(platform_close(fd), 0);
  FILE *file = platform_fopen("source-audio", path, "wb");
  cr_assert_not_null(file);
  unsigned char header[44] = {0};
  memcpy(header, "RIFF", 4);
  le32(header + 4, 96036);
  memcpy(header + 8, "WAVEfmt ", 8);
  le32(header + 16, 16);
  header[20] = 1;
  header[22] = 1;
  le32(header + 24, 48000);
  le32(header + 28, 96000);
  header[32] = 2;
  header[34] = 16;
  memcpy(header + 36, "data", 4);
  le32(header + 40, 96000);
  cr_assert_eq(fwrite(header, 1, sizeof(header), file), sizeof(header));
  for (unsigned i = 0; i < 48000; ++i) {
    uint16_t sample = i < 24000 ? 8192 : 49152;
    unsigned char bytes[] = {(unsigned char)(sample & 255), (unsigned char)(sample >> 8)};
    cr_assert_eq(fwrite(bytes, 1, sizeof(bytes), file), sizeof(bytes));
  }
  cr_assert_eq(fclose(file), 0);
  memset(&audio, 0, sizeof(audio));
  source = media_source_create(MEDIA_SOURCE_FILE, path);
  cr_assert_not_null(source);
}
static void teardown(void) {
  media_source_destroy(source);
  source = NULL;
  if (audio.playback_buffer)
    audio_ring_buffer_destroy(audio.playback_buffer);
  platform_delete_temp_file(path);
  options_state_destroy();
  log_destroy();
}
TestSuite(source_audio_contract, .init = setup, .fini = teardown, .timeout = 20);
static void read_samples(float expected) {
  float samples[128];
  cr_assert_eq(media_source_read_audio(source, samples, 128), 128);
  for (size_t i = 0; i < 128; ++i)
    cr_assert_float_eq(samples[i], expected, 1e-6, "sample %zu", i);
}
Test(source_audio_contract, paused_audio_is_silent_and_does_not_advance_the_source) {
  cr_assert_eq(media_source_get_type(source), MEDIA_SOURCE_FILE);
  cr_assert(media_source_has_audio(source));
  cr_assert_not(media_source_uses_webcam(source));
  cr_assert_float_eq(media_source_get_duration(source), 1, .01);
  read_samples(.25f);
  double position = media_source_get_position(source);
  media_source_pause(source);
  cr_assert(media_source_is_paused(source));
  read_samples(0);
  cr_assert_float_eq(media_source_get_position(source), position, 1e-6);
  media_source_resume(source);
  cr_assert_not(media_source_is_paused(source));
  read_samples(.25f);
  media_source_toggle_pause(source);
  cr_assert(media_source_is_paused(source));
  media_source_toggle_pause(source);
  cr_assert_not(media_source_is_paused(source));
}
Test(source_audio_contract, seeking_clears_stale_playback_audio_and_rewind_recovers_first_samples) {
  audio.playback_buffer = audio_ring_buffer_create_for_capture();
  cr_assert_not_null(audio.playback_buffer);
  float old[16];
  for (size_t i = 0; i < 16; ++i)
    old[i] = .875f;
  cr_assert_eq(audio_ring_buffer_write(audio.playback_buffer, old, 16), ASCIICHAT_OK);
  media_source_set_audio_context(source, &audio);
  cr_assert_eq(media_source_seek(source, .75), ASCIICHAT_OK);
  cr_assert_eq(audio_ring_buffer_read(audio.playback_buffer, old, 16), 0);
  read_samples(-.5f);
  cr_assert_eq(media_source_rewind(source), ASCIICHAT_OK);
  read_samples(.25f);
  cr_assert_eq(media_source_seek(source, -1), ERROR_INVALID_PARAM);
}
Test(source_audio_contract, eof_and_looping_deliver_the_original_audio_again) {
  size_t total = 0;
  float samples[1024];
  for (unsigned attempt = 0; attempt < 100; ++attempt) {
    size_t count = media_source_read_audio(source, samples, 1024);
    total += count;
    if (!count)
      break;
  }
  cr_assert_eq(total, 48000);
  cr_assert(media_source_at_end(source));
  media_source_set_loop(source, true);
  cr_assert_not(media_source_at_end(source));
  read_samples(.25f);
  media_source_set_loop(source, false);
  cr_assert_eq(media_source_rewind(source), ASCIICHAT_OK);
  read_samples(.25f);
}

static bool cancel_open(void *context) {
  ++*(unsigned *)context;
  return true;
}
Test(source_audio_contract, cancelled_and_invalid_source_creation_fail_without_disturbing_playback) {
  unsigned calls = 0;
  media_source_t *cancelled = media_source_create_interruptible(MEDIA_SOURCE_FILE, path, cancel_open, &calls);
  cr_assert_null(cancelled);
  cr_assert_gt(calls, 0);
  cr_assert_null(media_source_create((media_source_type_t)99, path));
  cr_assert_null(media_source_create(MEDIA_SOURCE_FILE, NULL));
  read_samples(.25f);
  cr_assert_eq(media_source_start_video(NULL), ERROR_INVALID_PARAM);
  cr_assert_not(media_source_uses_webcam(NULL));
  cr_assert_eq(media_source_seek(NULL, 1), ERROR_INVALID_PARAM);
  cr_assert_eq(media_source_rewind(NULL), ERROR_INVALID_PARAM);
  media_source_pause(NULL);
  media_source_resume(NULL);
  media_source_toggle_pause(NULL);
  cr_assert_not(media_source_is_paused(NULL));
  cr_assert(media_source_at_end(NULL));
  cr_assert_eq(media_source_read_audio(NULL, NULL, 0), 0);
}

Test(source_audio_contract, audio_visualizations_do_not_require_a_webcam) {
  cr_assert_eq(options_set_bool("test_pattern", false), ASCIICHAT_OK);
  const char *visualizations[] = {"waveform", "fft"};
  for (size_t i = 0; i < 2; ++i) {
    cr_assert_eq(options_set_bool(visualizations[i], true), ASCIICHAT_OK);
    cr_assert_not(media_source_uses_webcam(source));
    cr_assert_eq(media_source_start_video(source), ASCIICHAT_OK);
    cr_assert_not_null(media_source_read_video(source));
    read_samples(.25f);
    cr_assert_eq(options_set_bool(visualizations[i], false), ASCIICHAT_OK);
  }
}
