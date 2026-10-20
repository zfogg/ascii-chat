#include <ascii-chat/common.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/log/log.h>
#include <windows.h>

#define CHECK(value)                                                                                                   \
  do {                                                                                                                 \
    if (!(value))                                                                                                      \
      FATAL(ERROR_ASSERTION_FAILED, "%s", #value);                                                                     \
  } while (0)
int main(void) {
  log_init(NULL, LOG_ERROR, false, false);
  options_t options = options_t_new();
  options.test_audio = true;
  options.speakers_volume = 0;
  options.audio_capture_source = AUDIO_CAPTURE_SOURCE_MEDIA;
  CHECK(options_state_init() == ASCIICHAT_OK);
  CHECK(options_state_set(&options) == ASCIICHAT_OK);
  audio_context_t *audio = SAFE_CALLOC(1, sizeof(*audio), audio_context_t *);
  CHECK(audio_init(audio) == ASCIICHAT_OK);
  int priority = GetThreadPriority(GetCurrentThread());
  CHECK(audio_start_duplex(audio) == ASCIICHAT_OK);
  CHECK(GetThreadPriority(GetCurrentThread()) == priority);
  CHECK(audio_stop_duplex(audio) == ASCIICHAT_OK);
  audio_destroy(audio);
  SAFE_FREE(audio);
  audio_terminate_portaudio_final();
  options_state_destroy();
  return 0;
}
