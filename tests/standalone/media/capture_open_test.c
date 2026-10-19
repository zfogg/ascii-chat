/** Real loader workers/decoders with controlled audio ownership and load failures. */
#include "session/capture.h"
#include <ascii-chat/media/source.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/common.h>
#include <ascii-chat/video/face_follow_image.h>

#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr))                                                                                                       \
      FATAL(ERROR_ASSERTION_FAILED, "line %d: %s", __LINE__, #expr);                                                   \
  } while (0)
static audio_context_t audio;
static bool audio_running, fail_audio_start;
static unsigned starts, stops, destroyed;
static media_source_t *test_open(media_source_type_t type, const char *path, bool (*cancel)(void *), void *data) {
  if (!strcmp(path, "fail")) {
    SET_ERRNO(ERROR_MEDIA_OPEN, "Injected load failure");
    return NULL;
  }
  if (!strcmp(path, "cancel")) {
    while (!cancel(data))
      platform_sleep_ms(1);
    return NULL;
  }
  return media_source_create(type, path);
}
static asciichat_error_t test_audio_stop(audio_context_t *ctx) {
  CHECK(ctx == &audio);
  audio_running = false;
  ++stops;
  return ASCIICHAT_OK;
}
static asciichat_error_t test_audio_start(audio_context_t *ctx) {
  CHECK(ctx == &audio);
  ++starts;
  if (fail_audio_start) {
    fail_audio_start = false;
    return SET_ERRNO(ERROR_AUDIO, "Injected audio startup failure");
  }
  audio_running = true;
  return ASCIICHAT_OK;
}
static void test_destroy(media_source_t *source) {
  CHECK(!audio_running || audio.capture_media_source != source);
  ++destroyed;
  media_source_destroy(source);
}
static void test_face_destroy(face_follow_image_t *context) {
  CHECK(context == NULL);
}
static image_t *test_face_process(face_follow_image_t **context, image_t *source, double now, bool enabled, bool debug,
                                  int width, int height) {
  (void)context;
  (void)now;
  (void)enabled;
  (void)debug;
  (void)width;
  (void)height;
  return source;
}
#define face_follow_image_destroy test_face_destroy
#define face_follow_image_process test_face_process
#define media_source_create_interruptible test_open
#define media_source_destroy test_destroy
#define audio_stop_duplex test_audio_stop
#define audio_start_duplex test_audio_start
#include "../../../src/common/session/capture.c"

static void finish_load(session_capture_ctx_t *ctx) {
  uint64_t deadline = time_get_ns() + 15 * NS_PER_SEC_INT;
  for (;;) {
    mutex_lock(&ctx->load_mutex);
    bool done = ctx->loader_done;
    mutex_unlock(&ctx->load_mutex);
    if (done)
      break;
    CHECK(time_get_ns() < deadline);
    platform_sleep_ms(1);
  }
  session_capture_lock_source(ctx);
  capture_commit_media(ctx);
  session_capture_unlock_source(ctx);
  CHECK(!ctx->loader_created);
}
int main(int argc, char **argv) {
  CHECK(argc == 2);
  log_init(NULL, LOG_ERROR, false, false);
  options_t options = options_t_new();
  options.audio_capture_source = AUDIO_CAPTURE_SOURCE_MEDIA;
  CHECK(options_state_init() == ASCIICHAT_OK);
  CHECK(options_state_set(&options) == ASCIICHAT_OK);
  session_capture_config_t config = {.type = MEDIA_SOURCE_TEST, .target_fps = 30};
  session_capture_ctx_t *ctx = session_capture_create(&config);
  CHECK(ctx);
  audio_running = true;
  audio.monitor_local_media = true;
  session_capture_set_audio_context(ctx, &audio);
  media_source_t *initial = ctx->source;
  CHECK(session_capture_open_media(ctx, argv[1]) == ASCIICHAT_OK);
  finish_load(ctx);
  CHECK(ctx->source != initial && stops == 1 && starts == 1);
  CHECK(audio.capture_media_source == ctx->source && audio_running && audio.playback_only);
  CHECK(!ctx->last_media_frame && !ctx->face_follow && ctx->next_frame_deadline_ns == 0);
  media_source_t *active = ctx->source;
  CHECK(session_capture_open_media(ctx, "fail") == ASCIICHAT_OK);
  finish_load(ctx);
  CHECK(ctx->source == active && stops == 1);
  CHECK(session_capture_open_media(ctx, "cancel") == ASCIICHAT_OK);
  CHECK(session_capture_cancel_open(ctx));
  finish_load(ctx);
  CHECK(ctx->source == active && audio_running && stops == 1);
  for (unsigned i = 0; i < 6; ++i) {
    CHECK(session_capture_open_media(ctx, argv[1]) == ASCIICHAT_OK);
    finish_load(ctx);
    CHECK(audio.capture_media_source == ctx->source && audio_running);
  }
  CHECK(stops == 7 && starts == 7);
  CHECK(destroyed >= 7);
  active = ctx->source;
  fail_audio_start = true;
  CHECK(session_capture_open_media(ctx, argv[1]) == ASCIICHAT_OK);
  finish_load(ctx);
  CHECK(ctx->source == active && audio.capture_media_source == active && audio_running);
  CHECK(starts == 9 && stops == 9);
  session_capture_set_audio_context(ctx, NULL);
  audio_running = false;
  audio.capture_media_source = NULL;
  session_capture_destroy(ctx);
  ui_controller_shutdown();
  options_state_destroy();
  return 0;
}
