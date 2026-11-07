/* Verify that a resolved media source supplies real audio samples. */
#include <ascii-chat/common.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/media/source.h>
#include <ascii-chat/media/yt_dlp.h>
#include <ascii-chat/common/limits.h>
#include <ascii-chat/options/rcu.h>
#include "session/capture.h"
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/options/colorscheme.h>

static _Atomic bool stop_keyboard_poll;
static void *poll_keyboard(void *unused) {
  (void)unused;
  const char *extractor_pid = getenv("FAKE_YTDLP_EXTRACTOR_PID");
  const char *query_attempted = getenv("FAKE_YTDLP_QUERY_ATTEMPTED");
  bool checked_background = false;
  while (!atomic_load(&stop_keyboard_poll)) {
    (void)keyboard_input_available();
    if (!checked_background && extractor_pid && query_attempted && access(extractor_pid, F_OK) == 0) {
      usleep(350000);
      (void)keyboard_request_background();
      uint8_t r, g, b;
      (void)keyboard_query_background(&r, &g, &b);
      (void)colorscheme_current_background();
      FILE *marker = fopen(query_attempted, "w");
      if (marker)
        fclose(marker);
      checked_background = true;
    }
    usleep(1000);
  }
  return NULL;
}
#endif

bool extractor_cpp_linkage_probe(void);

static bool cancel_after_polling(void *data) {
  int *count = data;
  return ++*count > 20;
}

static bool never_cancel(void *data) {
  (void)data;
  return false;
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
#ifndef _WIN32
  if (getenv("FAKE_YTDLP_QUERY_ATTEMPTED") && keyboard_init() != ASCIICHAT_OK)
    return 12;
#endif
  if (argc == 3) {
    if (strcmp(argv[2], "--expect-cpp-linkage") == 0) {
      bool linked = extractor_cpp_linkage_probe();
      options_state_destroy();
      return linked ? 0 : 8;
    }
    if (strcmp(argv[2], "--expect-capture-cancel") == 0) {
      int polls = 0;
      session_capture_config_t config = {.type = MEDIA_SOURCE_FILE,
                                         .path = argv[1],
                                         .should_exit_callback = cancel_after_polling,
                                         .callback_data = &polls};
      session_capture_ctx_t *capture = session_capture_create(&config);
      if (capture)
        session_capture_destroy(capture);
      options_state_destroy();
      return !capture && polls > 20 ? 0 : 9;
    }
    if (strcmp(argv[2], "--expect-cancel") == 0) {
      char video[MEDIA_STREAM_URL_SIZE], audio[MEDIA_STREAM_URL_SIZE];
      int polls = 0;
#ifndef _WIN32
      pthread_t keyboard_thread = {0};
      bool keyboard_started =
          getenv("FAKE_YTDLP_EXPECT_TERMINAL") && pthread_create(&keyboard_thread, NULL, poll_keyboard, NULL) == 0;
      if (getenv("FAKE_YTDLP_EXPECT_TERMINAL") && !keyboard_started)
        return 11;
#endif
      asciichat_error_t result = yt_dlp_extract_stream_urls_interruptible(argv[1], NULL, video, sizeof(video), audio,
                                                                          sizeof(audio), cancel_after_polling, &polls);
#ifndef _WIN32
      if (keyboard_started) {
        atomic_store(&stop_keyboard_poll, true);
        pthread_join(keyboard_thread, NULL);
      }
#endif
      options_state_destroy();
#ifndef _WIN32
      if (getenv("FAKE_YTDLP_EXPECT_TERMINAL") && tcgetpgrp(STDIN_FILENO) != getpgrp())
        return 10;
#endif
      return result != ASCIICHAT_OK && polls > 20 ? 0 : 7;
    }
    char video[MEDIA_STREAM_URL_SIZE], audio[MEDIA_STREAM_URL_SIZE];
    memset(video, 'X', sizeof(video));
    memset(audio, 'X', sizeof(audio));
#ifndef _WIN32
    pthread_t keyboard_thread = {0};
    bool keyboard_started =
        getenv("FAKE_YTDLP_EXPECT_TERMINAL") && pthread_create(&keyboard_thread, NULL, poll_keyboard, NULL) == 0;
    if (getenv("FAKE_YTDLP_EXPECT_TERMINAL") && !keyboard_started)
      return 11;
#endif
    asciichat_error_t result =
        getenv("FAKE_YTDLP_EXPECT_TERMINAL")
            ? yt_dlp_extract_stream_urls_interruptible(argv[1], NULL, video, sizeof(video), audio, sizeof(audio),
                                                       never_cancel, NULL)
            : yt_dlp_extract_stream_urls(argv[1], NULL, video, sizeof(video), audio, sizeof(audio));
#ifndef _WIN32
    if (keyboard_started) {
      atomic_store(&stop_keyboard_poll, true);
      pthread_join(keyboard_thread, NULL);
    }
    if (getenv("FAKE_YTDLP_QUERY_ATTEMPTED"))
      keyboard_destroy();
#endif
    options_state_destroy();
#ifndef _WIN32
    if (getenv("FAKE_YTDLP_EXPECT_TERMINAL") && tcgetpgrp(STDIN_FILENO) != getpgrp())
      return 10;
#endif
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
