/** Real loader workers/decoders with controlled audio ownership and load failures. */
#include "session/capture.h"
#include <ascii-chat/media/source.h>
#include <ascii-chat/media/ffmpeg_decoder.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/audio/recording.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/common.h>
#include <ascii-chat/video/face_follow_image.h>
#include <math.h>

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
#ifndef NDEBUG
static mutex_t *prefetch_mutex, *recording_mutex;
static void find_prefetch(uintptr_t key, uint64_t generation, const char *name, const char *type, const char *file,
                          int line, void *data) {
  (void)generation;
  (void)file;
  (void)line;
  (void)data;
  if (!strcmp(type, "mutex") && strstr(name, "ffmpeg_prefetch."))
    prefetch_mutex = (mutex_t *)key;
  if (!strcmp(type, "mutex") && strstr(name, "audio_recording."))
    recording_mutex = (mutex_t *)key;
}

#endif

static void *toggle_pause_many(void *data) {
  for (unsigned i = 0; i < 10001; ++i)
    media_source_toggle_pause(data);
  return NULL;
}

static atomic_bool submitting;
static void *record_many(void *unused) {
  (void)unused;
  const float samples[4] = {0.25f, 0.25f, 0.25f, 0.25f};
  while (atomic_load(&submitting))
    audio_recording_submit(AUDIO_RECORDING_MEDIA, samples, 4, time_get_ns());
  return NULL;
}

static void check_recording_fast_path(void) {
  audio_recording_t *recording = NULL;
  CHECK(audio_recording_create(&recording) == ASCIICHAT_OK);
#ifndef NDEBUG
  size_t required = 0;
  bool complete = false;
  CHECK(named_registry_try_snapshot(SIZE_MAX, &required, &complete, find_prefetch, NULL) == ASCIICHAT_OK && complete);
  CHECK(recording_mutex);
  uint64_t locks = recording_mutex->lock_count;
#endif
  const float samples[4] = {0.25f, 0.25f, 0.25f, 0.25f};
  for (unsigned i = 0; i < 10000; ++i)
    audio_recording_submit(AUDIO_RECORDING_MEDIA, samples, 4, NS_PER_SEC_INT);
#ifndef NDEBUG
  CHECK(recording_mutex->lock_count == locks);
#endif
  audio_recording_start(recording, NS_PER_SEC_INT);
  audio_recording_submit(AUDIO_RECORDING_MEDIA, samples, 4, NS_PER_SEC_INT);
  float output[4];
  audio_recording_read(recording, output, 4);
  CHECK(!memcmp(output, samples, sizeof(output)));
  audio_recording_destroy(recording);
#ifndef NDEBUG
  locks = recording_mutex->lock_count;
#endif
  for (unsigned i = 0; i < 10000; ++i)
    audio_recording_submit(AUDIO_RECORDING_MEDIA, samples, 4, NS_PER_SEC_INT);
#ifndef NDEBUG
  CHECK(recording_mutex->lock_count == locks);
#endif
  atomic_store(&submitting, true);
  asciichat_thread_t producer;
  CHECK(acthread_create(&producer, "recording_submitter", record_many, NULL) == ASCIICHAT_OK);
  for (unsigned i = 0; i < 5; ++i) {
    CHECK(audio_recording_create(&recording) == ASCIICHAT_OK);
    audio_recording_start(recording, time_get_ns());
    platform_sleep_ms(2);
    audio_recording_destroy(recording);
  }
  atomic_store(&submitting, false);
  CHECK(thread_join(&producer, NULL) == ASCIICHAT_OK);
}

static void check_atomic_pause(void) {
  media_source_t *source = media_source_create(MEDIA_SOURCE_TEST, NULL);
  CHECK(source && !media_source_is_paused(source));
  media_source_pause(source);
  CHECK(media_source_is_paused(source));
  CHECK(media_source_read_video(source) == NULL);
  float silence[32];
  for (size_t i = 0; i < 32; ++i)
    silence[i] = 1;
  CHECK(media_source_read_audio(source, silence, 32) == 32);
  for (size_t i = 0; i < 32; ++i)
    CHECK(silence[i] == 0);
  media_source_resume(source);
  asciichat_thread_t toggles[2];
  for (size_t i = 0; i < 2; ++i)
    CHECK(acthread_create(&toggles[i], "pause_toggles", toggle_pause_many, source) == ASCIICHAT_OK);
  for (size_t i = 0; i < 2; ++i)
    CHECK(thread_join(&toggles[i], NULL) == ASCIICHAT_OK);
  CHECK(!media_source_is_paused(source));
  CHECK(media_source_read_video(source) != NULL);
  media_source_destroy(source);
}

static atomic_bool audio_reading, stop_audio_reads;
static void *read_audio_during_seeks(void *data) {
  ffmpeg_decoder_t *decoder = data;
  float samples[37];
  while (!atomic_load(&stop_audio_reads)) {
    size_t count = ffmpeg_decoder_read_audio_samples(decoder, samples, 37);
    for (size_t i = 0; i < count; ++i)
      CHECK(isfinite(samples[i]));
    if (count)
      atomic_store(&audio_reading, true);
    else if (ffmpeg_decoder_at_end(decoder))
      CHECK(ffmpeg_decoder_rewind(decoder) == ASCIICHAT_OK);
  }
  return NULL;
}

static void check_audio_seek_serialization(const char *path) {
  ffmpeg_decoder_t *decoder = ffmpeg_decoder_create(path);
  CHECK(decoder);
  atomic_store(&audio_reading, false);
  atomic_store(&stop_audio_reads, false);
  asciichat_thread_t reader;
  CHECK(acthread_create(&reader, "seek_audio_reader", read_audio_during_seeks, decoder) == ASCIICHAT_OK);
  uint64_t deadline = time_get_ns() + NS_PER_SEC_INT;
  while (!atomic_load(&audio_reading) && time_get_ns() < deadline)
    platform_sleep_ms(1);
  CHECK(atomic_load(&audio_reading));
  for (unsigned i = 0; i < 20; ++i) {
    CHECK(ffmpeg_decoder_seek_to_timestamp(decoder, (i % 2) * 0.25) == ASCIICHAT_OK);
    platform_sleep_ms(1);
  }
  atomic_store(&stop_audio_reads, true);
  CHECK(thread_join(&reader, NULL) == ASCIICHAT_OK);
  CHECK(ffmpeg_decoder_seek_to_timestamp(decoder, 0) == ASCIICHAT_OK);
  float resumed[37];
  CHECK(ffmpeg_decoder_read_audio_samples(decoder, resumed, 37) > 0);
  ffmpeg_decoder_destroy(decoder);
}

static unsigned drain_video(ffmpeg_decoder_t *decoder) {
  unsigned frames = 0;
  uint64_t deadline = time_get_ns() + 5 * NS_PER_SEC_INT;
  for (;;) {
    image_t *frame = NULL;
    asciichat_error_context_t failure;
    ffmpeg_frame_status_t status = ffmpeg_decoder_poll_video(decoder, &frame, &failure);
    CHECK(status != FFMPEG_FRAME_FAILED);
    if (status == FFMPEG_FRAME_EOF)
      return frames;
    if (status == FFMPEG_FRAME_READY) {
      CHECK(frame && frame->pixels);
      ++frames;
    } else {
      platform_sleep_ms(1);
    }
    CHECK(time_get_ns() < deadline);
  }
}

static void check_prefetch_notifications(const char *path) {
  ffmpeg_decoder_t *decoder = ffmpeg_decoder_create(path);
  CHECK(decoder);
#ifndef NDEBUG
  size_t required = 0;
  bool complete = false;
  prefetch_mutex = NULL;
  CHECK(named_registry_try_snapshot(SIZE_MAX, &required, &complete, find_prefetch, NULL) == ASCIICHAT_OK && complete);
  CHECK(prefetch_mutex);
  uint64_t locks = prefetch_mutex->lock_count;
#endif
  // A decoder without a running producer has no frame; polling is read-only.
  for (unsigned i = 0; i < 10000; ++i) {
    image_t *frame = NULL;
    asciichat_error_context_t failure;
    CHECK(ffmpeg_decoder_poll_video(decoder, &frame, &failure) == FFMPEG_FRAME_WAITING);
    CHECK(!frame);
  }
#ifndef NDEBUG
  CHECK(prefetch_mutex->lock_count == locks);
#endif
  CHECK(ffmpeg_decoder_start_prefetch(decoder) == ASCIICHAT_OK);
  // Leave a frame pending: the producer should sleep without touching its mutex.
  platform_sleep_ms(100);
#ifndef NDEBUG
  locks = prefetch_mutex->lock_count;
  platform_sleep_ms(100);
  CHECK(prefetch_mutex->lock_count == locks);
#endif
  CHECK(drain_video(decoder) == 30);
#ifndef NDEBUG
  CHECK(prefetch_mutex->lock_count - locks <= 3 * 30 + 12);
#endif
  // EOF sleeps too: rewinding must explicitly wake the waiting producer.
  for (unsigned i = 0; i < 3; ++i) {
    platform_sleep_ms(20);
    CHECK(ffmpeg_decoder_rewind(decoder) == ASCIICHAT_OK);
    CHECK(drain_video(decoder) == 30);
  }
  CHECK(ffmpeg_decoder_seek_to_timestamp(decoder, 0) == ASCIICHAT_OK);
  image_t *held = NULL;
  uint64_t deadline = time_get_ns() + NS_PER_SEC_INT;
  while (!held && time_get_ns() < deadline) {
    held = ffmpeg_decoder_read_video_frame(decoder);
    platform_sleep_ms(1);
  }
  CHECK(held && held->pixels);
  held->pixels[0].r ^= 0xff;
  rgb_pixel_t retained = held->pixels[0];
  CHECK(ffmpeg_decoder_seek_to_timestamp(decoder, 0) == ASCIICHAT_OK);
  platform_sleep_ms(30);
  CHECK(!memcmp(&held->pixels[0], &retained, sizeof(retained)));
  CHECK(drain_video(decoder) == 30);
  platform_sleep_ms(20);
  uint64_t started = time_get_ns();
  ffmpeg_decoder_stop_prefetch(decoder);
  CHECK(!ffmpeg_decoder_is_prefetch_running(decoder));
  CHECK(time_get_ns() - started < NS_PER_SEC_INT);
  CHECK(ffmpeg_decoder_start_prefetch(decoder) == ASCIICHAT_OK);
  CHECK(ffmpeg_decoder_seek_to_timestamp(decoder, 0) == ASCIICHAT_OK);
  CHECK(drain_video(decoder) == 30);
  for (unsigned i = 0; i < 10; ++i) {
    CHECK(ffmpeg_decoder_seek_to_timestamp(decoder, 0) == ASCIICHAT_OK);
    platform_sleep_ms(2);
  }
  CHECK(drain_video(decoder) == 30);
  CHECK(ffmpeg_decoder_seek_to_timestamp(decoder, 0) == ASCIICHAT_OK);
  platform_sleep_ms(100);
  started = time_get_ns();
  ffmpeg_decoder_destroy(decoder);
  CHECK(time_get_ns() - started < NS_PER_SEC_INT);
}

int main(int argc, char **argv) {
  CHECK(argc == 2);
  log_init(NULL, LOG_ERROR, false, false);
  CHECK(named_init() == ASCIICHAT_OK);
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
  CHECK(atomic_load_bool(&ctx->awaiting_presentation));
  uint64_t epoch = session_capture_source_epoch(ctx);
  CHECK(epoch > 0);
  session_capture_complete_open(ctx, epoch - 1);
  CHECK(atomic_load_bool(&ctx->awaiting_presentation));
  session_capture_complete_open(ctx, epoch);
  CHECK(!atomic_load_bool(&ctx->awaiting_presentation));
  media_source_t *active = ctx->source;
  atomic_store_bool(&ctx->awaiting_presentation, true);
  CHECK(session_capture_open_media(ctx, "fail") == ASCIICHAT_OK);
  CHECK(!atomic_load_bool(&ctx->awaiting_presentation));
  session_capture_complete_open(ctx, epoch);
  CHECK(ctx->loader_created);
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
  // A playlist uses the same loader and audio handoff as a single replacement.
  // Revisiting an item creates a fresh decoder at the start.
  media_playlist_t list = {0};
  CHECK(media_playlist_append(&list, argv[1], false) == ASCIICHAT_OK);
  CHECK(media_playlist_append(&list, argv[1], false) == ASCIICHAT_OK);
  char quoted[1024];
  CHECK(media_playlist_format(&list, quoted, sizeof(quoted)) == ASCIICHAT_OK);
  CHECK(session_capture_open_playlist(ctx, quoted) == ASCIICHAT_OK);
  finish_load(ctx);
  CHECK(ctx->playlist.count == 2 && ctx->playlist.index == 0);
  active = ctx->source;
  CHECK(session_capture_open_media(ctx, "fail") == ASCIICHAT_OK);
  finish_load(ctx);
  CHECK(!ctx->advance_failed && ctx->playlist.count == 2 && ctx->playlist.index == 0);
  CHECK(session_capture_open_media(ctx, "cancel") == ASCIICHAT_OK);
  CHECK(session_capture_cancel_open(ctx));
  finish_load(ctx);
  CHECK(!ctx->advance_failed && ctx->playlist.index == 0);
  CHECK(capture_load_playlist(ctx, &ctx->playlist, 1, true) == ASCIICHAT_OK);
  finish_load(ctx);
  CHECK(ctx->playlist.index == 1 && ctx->source != active);
  CHECK(media_source_get_position(ctx->source) < 0.1);
  CHECK(capture_load_playlist(ctx, &ctx->playlist, 0, false) == ASCIICHAT_OK);
  finish_load(ctx);
  CHECK(ctx->playlist.index == 0 && media_source_get_position(ctx->source) < 0.1);
  media_playlist_t bad_next = ctx->playlist;
  snprintf(bad_next.paths[1], sizeof(bad_next.paths[1]), "%s", "fail");
  active = ctx->source;
  CHECK(capture_load_playlist(ctx, &bad_next, 1, true) == ASCIICHAT_OK);
  finish_load(ctx);
  CHECK(ctx->advance_failed && ctx->source == active && ctx->playlist.index == 0);
  session_capture_set_audio_context(ctx, NULL);
  audio_running = false;
  audio.capture_media_source = NULL;
  session_capture_destroy(ctx);
  ui_controller_shutdown();
  check_recording_fast_path();
  check_atomic_pause();
  check_prefetch_notifications(argv[1]);
  check_audio_seek_serialization(argv[1]);
  options_state_destroy();
  return 0;
}
