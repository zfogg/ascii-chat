#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <ascii-chat/media/source.h>
#include <ascii-chat/options/rcu.h>
#include <libplatform/filesystem.h>
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
  REQUIRE((options_state_init()) == (ASCIICHAT_OK));
  options_t *options = SAFE_MALLOC(sizeof(*options), options_t *);
  *options = options_t_new();
  options->test_pattern = true;
  options->width = 40;
  options->height = 12;
  REQUIRE((options_state_set(options)) == (ASCIICHAT_OK));
  SAFE_FREE(options);
  int fd = -1;
  REQUIRE((platform_create_temp_file(path, sizeof(path), "source-audio", &fd)) == (0));
  REQUIRE((platform_close(fd)) == (0));
  FILE *file = platform_fopen("source-audio", path, "wb");
  REQUIRE((file) != nullptr);
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
  REQUIRE((fwrite(header, 1, sizeof(header), file)) == (sizeof(header)));
  for (unsigned i = 0; i < 48000; ++i) {
    uint16_t sample = i < 24000 ? 8192 : 49152;
    unsigned char bytes[] = {(unsigned char)(sample & 255), (unsigned char)(sample >> 8)};
    REQUIRE((fwrite(bytes, 1, sizeof(bytes), file)) == (sizeof(bytes)));
  }
  REQUIRE((fclose(file)) == (0));
  memset(&audio, 0, sizeof(audio));
  source = media_source_create(MEDIA_SOURCE_FILE, path);
  REQUIRE((source) != nullptr);
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
TEST_CASE("source_audio_contract/local_path_url_probe_preserves_preexisting_errors_without_adding_one", "[source_audio_contract] [unit]") {
  CLEAR_ERRNO_ALL();
  SET_ERRNO(ERROR_NETWORK, "Earlier caller error");
  asciichat_errno_scope_t before_open = errno_checkpoint();
  uint64_t errors_before = asciichat_error_stats_get().total_errors;
  media_source_t *second = media_source_create(MEDIA_SOURCE_FILE, path);
  REQUIRE((second) != nullptr);
  REQUIRE(errno_scope_is_clean(before_open));
  REQUIRE((asciichat_error_stats_get().total_errors) == (errors_before));
  asciichat_error_context_t error;
  REQUIRE(HAS_ERRNO(&error));
  REQUIRE((error.code) == (ERROR_NETWORK));
  REQUIRE((strstr(error.context_message, "Earlier caller error")) != nullptr);
  media_source_destroy(second);
  REQUIRE(errno_scope_is_clean(before_open));
  CLEAR_ERRNO_ALL();
}

static void read_samples(float expected) {
  float samples[128];
  REQUIRE((media_source_read_audio(source, samples, 128)) == (128));
  for (size_t i = 0; i < 128; ++i)
    REQUIRE((samples[i]) == Catch::Approx(expected));
}
TEST_CASE("source_audio_contract/paused_audio_is_silent_and_does_not_advance_the_source", "[source_audio_contract] [unit]") {
  REQUIRE((media_source_get_type(source)) == (MEDIA_SOURCE_FILE));
  REQUIRE(media_source_has_audio(source));
  REQUIRE(!(media_source_uses_webcam(source)));
  REQUIRE((media_source_get_duration(source)) == Catch::Approx(1));
  read_samples(.25f);
  double position = media_source_get_position(source);
  media_source_pause(source);
  REQUIRE(media_source_is_paused(source));
  read_samples(0);
  REQUIRE((media_source_get_position(source)) == Catch::Approx(position));
  media_source_resume(source);
  REQUIRE(!(media_source_is_paused(source)));
  read_samples(.25f);
  media_source_toggle_pause(source);
  REQUIRE(media_source_is_paused(source));
  media_source_toggle_pause(source);
  REQUIRE(!(media_source_is_paused(source)));
}
TEST_CASE("source_audio_contract/seeking_clears_stale_playback_audio_and_rewind_recovers_first_samples", "[source_audio_contract] [unit]") {
  audio.playback_buffer = audio_ring_buffer_create_for_capture();
  REQUIRE((audio.playback_buffer) != nullptr);
  float old[16];
  for (size_t i = 0; i < 16; ++i)
    old[i] = .875f;
  REQUIRE((audio_ring_buffer_write(audio.playback_buffer, old, 16)) == (ASCIICHAT_OK));
  media_source_set_audio_context(source, &audio);
  REQUIRE((media_source_seek(source, .75)) == (ASCIICHAT_OK));
  REQUIRE((audio_ring_buffer_read(audio.playback_buffer, old, 16)) == (0));
  read_samples(-.5f);
  REQUIRE((media_source_rewind(source)) == (ASCIICHAT_OK));
  read_samples(.25f);
  REQUIRE((media_source_seek(source, -1)) == (ERROR_INVALID_PARAM));
}
TEST_CASE("source_audio_contract/eof_and_looping_deliver_the_original_audio_again", "[source_audio_contract] [unit]") {
  size_t total = 0;
  float samples[1024];
  for (unsigned attempt = 0; attempt < 100; ++attempt) {
    size_t count = media_source_read_audio(source, samples, 1024);
    total += count / AUDIO_CHANNELS;
    if (!count)
      break;
  }
  REQUIRE((total) == (48000));
  REQUIRE(media_source_at_end(source));
  media_source_set_loop(source, true);
  REQUIRE(!(media_source_at_end(source)));
  read_samples(.25f);
  media_source_set_loop(source, false);
  REQUIRE((media_source_rewind(source)) == (ASCIICHAT_OK));
  read_samples(.25f);
}

static bool cancel_open(void *context) {
  ++*(unsigned *)context;
  return true;
}
TEST_CASE("source_audio_contract/cancelled_and_invalid_source_creation_fail_without_disturbing_playback", "[source_audio_contract] [unit]") {
  unsigned calls = 0;
  media_source_t *cancelled = media_source_create_interruptible(MEDIA_SOURCE_FILE, path, cancel_open, &calls);
  REQUIRE((cancelled) == nullptr);
  REQUIRE((calls) > (0));
  REQUIRE((media_source_create((media_source_type_t)99, path)) == nullptr);
  REQUIRE((media_source_create(MEDIA_SOURCE_FILE, NULL)) == nullptr);
  read_samples(.25f);
  REQUIRE((media_source_start_video(NULL)) == (ERROR_INVALID_PARAM));
  REQUIRE(!(media_source_uses_webcam(NULL)));
  REQUIRE((media_source_seek(NULL, 1)) == (ERROR_INVALID_PARAM));
  REQUIRE((media_source_rewind(NULL)) == (ERROR_INVALID_PARAM));
  media_source_pause(NULL);
  media_source_resume(NULL);
  media_source_toggle_pause(NULL);
  REQUIRE(!(media_source_is_paused(NULL)));
  REQUIRE(media_source_at_end(NULL));
  REQUIRE((media_source_read_audio(NULL, NULL, 0)) == (0));
}

TEST_CASE("source_audio_contract/audio_visualizations_do_not_require_a_webcam", "[source_audio_contract] [unit]") {
  REQUIRE((options_set_bool("test_pattern", false)) == (ASCIICHAT_OK));
  const char *visualizations[] = {"waveform", "fft"};
  for (size_t i = 0; i < 2; ++i) {
    REQUIRE((options_set_bool(visualizations[i], true)) == (ASCIICHAT_OK));
    REQUIRE(!(media_source_uses_webcam(source)));
    REQUIRE((media_source_start_video(source)) == (ASCIICHAT_OK));
    REQUIRE((media_source_read_video(source)) != nullptr);
    read_samples(.25f);
    REQUIRE((options_set_bool(visualizations[i], false)) == (ASCIICHAT_OK));
  }
}

TEST_CASE("source_audio_contract/live_audio_visualizations_create_frames_without_opening_a_camera", "[source_audio_contract] [unit]") {
  REQUIRE((options_set_bool("test_pattern", false)) == (ASCIICHAT_OK));
  const char *visualizations[] = {"waveform", "fft"};
  for (size_t i = 0; i < 2; ++i) {
    REQUIRE((options_set_bool(visualizations[i], true)) == (ASCIICHAT_OK));
    media_source_t *live = media_source_create(MEDIA_SOURCE_WEBCAM, "65535");
    REQUIRE((live) != nullptr);
    REQUIRE(!(media_source_uses_webcam(live)));
    REQUIRE((media_source_start_video(live)) == (ASCIICHAT_OK));
    REQUIRE((media_source_read_video(live)) != nullptr);
    media_source_destroy(live);
    REQUIRE((options_set_bool(visualizations[i], false)) == (ASCIICHAT_OK));
  }
}
