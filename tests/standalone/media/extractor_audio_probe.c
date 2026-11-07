/* Verify that a resolved media source supplies real audio samples. */
#include <ascii-chat/common.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/media/source.h>
#include <ascii-chat/media/yt_dlp.h>
#include <ascii-chat/common/limits.h>
#include <ascii-chat/options/rcu.h>
#include <string.h>

static bool cancel_after_polling(void *data) {
  int *count = data;
  return ++*count > 20;
}

int main(int argc, char **argv) {
  if (argc != 2 && argc != 3)
    return 1;
  log_init(NULL, LOG_ERROR, false, false);
  if (named_init() != ASCIICHAT_OK || options_state_init() != ASCIICHAT_OK)
    return 2;
  options_t options = options_t_new();
  if (options_state_set(&options) != ASCIICHAT_OK)
    return 3;
  if (argc == 3) {
    if (strcmp(argv[2], "--expect-cancel") == 0) {
      char video[MEDIA_STREAM_URL_SIZE], audio[MEDIA_STREAM_URL_SIZE];
      int polls = 0;
      asciichat_error_t result = yt_dlp_extract_stream_urls_interruptible(argv[1], NULL, video, sizeof(video), audio,
                                                                          sizeof(audio), cancel_after_polling, &polls);
      options_state_destroy();
      return result != ASCIICHAT_OK && polls > 20 ? 0 : 7;
    }
    char video[MEDIA_STREAM_URL_SIZE], audio[MEDIA_STREAM_URL_SIZE];
    memset(video, 'X', sizeof(video));
    memset(audio, 'X', sizeof(audio));
    asciichat_error_t result = yt_dlp_extract_stream_urls(argv[1], NULL, video, sizeof(video), audio, sizeof(audio));
    options_state_destroy();
    if (strcmp(argv[2], "--expect-error") == 0)
      return result != ASCIICHAT_OK && !video[0] && !video[sizeof(video) - 1] && !audio[0] && !audio[sizeof(audio) - 1]
                 ? 0
                 : 6;
    return result == ASCIICHAT_OK && strcmp(video, argv[2]) == 0 && !audio[0] ? 0 : 6;
  }
  media_source_t *source = media_source_create(MEDIA_SOURCE_FILE, argv[1]);
  if (!source || !media_source_has_video(source) || !media_source_has_audio(source))
    return 4;
  float samples[1024] = {0};
  size_t count = media_source_read_audio(source, samples, ARRAY_SIZE(samples));
  bool nonzero = false;
  for (size_t i = 0; i < count; ++i)
    nonzero |= samples[i] != 0.0f;
  media_source_destroy(source);
  options_state_destroy();
  return count > 0 && nonzero ? 0 : 5;
}
