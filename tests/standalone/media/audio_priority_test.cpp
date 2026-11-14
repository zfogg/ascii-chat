#include <catch2/catch_test_macros.hpp>
#include <ascii-chat/otel.h>
#include <ascii-chat/common.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/log/log.h>
#include <windows.h>

TEST_CASE("audio thread priority", "[standalone][platform][audio]") {
  log_init(NULL, LOG_ERROR, false, false);
  options_t options = options_t_new();
  options.test_audio = true;
  options.speakers_volume = 0;
  options.audio_capture_source = AUDIO_CAPTURE_SOURCE_MEDIA;
  const char *endpoint = SAFE_GETENV("ASCII_CHAT_OTEL_FIXTURE_ENDPOINT");
  if (endpoint)
    SAFE_STRNCPY(options.otel_endpoint, endpoint, sizeof(options.otel_endpoint));
  REQUIRE(options_state_init() == ASCIICHAT_OK);
  REQUIRE(options_state_set(&options) == ASCIICHAT_OK);
  char *args[] = {"audio-fixture"};
  REQUIRE(ascii_otel_init("audio-fixture", 1, args) == ASCIICHAT_OK);
  float *samples = SAFE_CALLOC(AUDIO_RING_BUFFER_SIZE, sizeof(float), float *);
  audio_ring_buffer_t *playback = audio_ring_buffer_create();
  REQUIRE(audio_ring_buffer_write(playback, samples, 4096) == ASCIICHAT_OK);
  REQUIRE(audio_ring_buffer_read(playback, samples, 8192) == 4096);
  audio_ring_buffer_t *capture = audio_ring_buffer_create_for_capture();
  REQUIRE(audio_ring_buffer_write(capture, samples, AUDIO_RING_BUFFER_SIZE) == ASCIICHAT_OK);
  audio_ring_buffer_destroy(playback);
  audio_ring_buffer_destroy(capture);
  SAFE_FREE(samples);
  audio_context_t *audio = SAFE_CALLOC(1, sizeof(*audio), audio_context_t *);
  REQUIRE(audio_init(audio) == ASCIICHAT_OK);
  int priority = GetThreadPriority(GetCurrentThread());
  REQUIRE(audio_start_duplex(audio) == ASCIICHAT_OK);
  REQUIRE(GetThreadPriority(GetCurrentThread()) == priority);
  REQUIRE(audio_stop_duplex(audio) == ASCIICHAT_OK);
  audio_destroy(audio);
  SAFE_FREE(audio);
  audio_terminate_portaudio_final();
  ascii_otel_exit(0);
  ascii_otel_shutdown();
  options_state_destroy();
  return 0;
}
