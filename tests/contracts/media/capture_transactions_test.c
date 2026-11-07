#include <criterion/criterion.h>
#include "../../../src/common/session/capture.h"
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/util/time.h>

static media_source_type_t requested_type;
static char requested_path[PLATFORM_MAX_PATH_LENGTH];
static bool fail_source, source_sets_error, fail_video, fail_camera, camera_available, has_audio;
static int stop_failure_at, start_failure_at, stop_calls, start_calls;
static unsigned source_destroys, loader_starts, join_failures;
static media_source_t *camera_source;
static void *(*loader_worker)(void *);
static void *loader_arg;
static media_source_t *transaction_source_create(media_source_type_t type, const char *path) {
  requested_type = type;
  safe_snprintf(requested_path, sizeof(requested_path), "%s", path ? path : "");
  if (fail_source) {
    if (source_sets_error)
      SET_ERRNO(ERROR_MEDIA_OPEN, "Injected media-open failure");
    return NULL;
  }
  if (type == MEDIA_SOURCE_WEBCAM) {
    if (!camera_available)
      return NULL;
    camera_source = media_source_create(MEDIA_SOURCE_TEST, NULL);
    return camera_source;
  }
  if (type == MEDIA_SOURCE_STDIN)
    return media_source_create(MEDIA_SOURCE_TEST, NULL);
  return media_source_create(type, path);
}
static media_source_t *transaction_source_create_interruptible(media_source_type_t type, const char *path,
                                                               bool (*cancel)(void *), void *data) {
  (void)cancel;
  (void)data;
  return transaction_source_create(type, path);
}
static void transaction_source_destroy(media_source_t *source) {
  if (source)
    ++source_destroys;
  media_source_destroy(source);
}
static asciichat_error_t transaction_video_start(media_source_t *source) {
  if (fail_video || (source == camera_source && fail_camera))
    return SET_ERRNO(ERROR_MEDIA_INIT, "Injected video startup failure");
  return media_source_start_video(source);
}
static bool transaction_has_audio(media_source_t *source) {
  (void)source;
  return has_audio;
}
static size_t transaction_audio_read(media_source_t *source, float *out, size_t count) {
  (void)source;
  for (size_t i = 0; i < count; ++i)
    out[i] = 0.25f;
  return count;
}
static asciichat_error_t transaction_audio_stop(audio_context_t *audio) {
  (void)audio;
  ++stop_calls;
  return stop_failure_at == stop_calls ? ERROR_AUDIO : ASCIICHAT_OK;
}
static asciichat_error_t transaction_audio_start(audio_context_t *audio) {
  (void)audio;
  ++start_calls;
  return start_failure_at == start_calls ? ERROR_AUDIO : ASCIICHAT_OK;
}
static int transaction_thread_create(asciichat_thread_t *thread, const char *name, void *(*worker)(void *), void *arg) {
  (void)thread;
  (void)name;
  ++loader_starts;
  loader_worker = worker;
  loader_arg = arg;
  return 0;
}
static int transaction_thread_join(asciichat_thread_t *thread, void **result) {
  (void)thread;
  (void)result;
  if (join_failures) {
    --join_failures;
    return -1;
  }
  return 0;
}
#define media_source_create transaction_source_create
#define media_source_create_interruptible transaction_source_create_interruptible
#define media_source_destroy transaction_source_destroy
#define media_source_start_video transaction_video_start
#define media_source_has_audio transaction_has_audio
#define media_source_read_audio transaction_audio_read
#define audio_stop_duplex transaction_audio_stop
#define audio_start_duplex transaction_audio_start
#define acthread_create transaction_thread_create
#define thread_join transaction_thread_join
#include "../../../src/common/session/capture.c"
#undef media_source_create
#undef media_source_create_interruptible
#undef media_source_destroy
#undef media_source_start_video
#undef media_source_has_audio
#undef media_source_read_audio
#undef audio_stop_duplex
#undef audio_start_duplex
#undef acthread_create
#undef thread_join

static char first_path[PLATFORM_MAX_PATH_LENGTH], second_path[PLATFORM_MAX_PATH_LENGTH];
static session_capture_ctx_t *current;
static audio_context_t borrowed_audio;
static void transaction_file(char *path, rgb_pixel_t color) {
  int fd = -1;
  cr_assert_eq(platform_create_temp_file(path, PLATFORM_MAX_PATH_LENGTH, "capture-transaction", &fd), 0);
  cr_assert_eq(platform_close(fd), 0);
  FILE *file = platform_fopen("transaction", path, "wb");
  cr_assert_not_null(file);
  const char header[] = "P6\n2 2\n255\n";
  for (unsigned i = 0; i < 20; ++i) {
    cr_assert_eq(fwrite(header, 1, sizeof(header) - 1, file), sizeof(header) - 1);
    for (unsigned p = 0; p < 4; ++p)
      cr_assert_eq(fwrite(&color, sizeof(color), 1, file), 1);
  }
  cr_assert_eq(fclose(file), 0);
}
static void transaction_setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  errno_suppress(true);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = options_t_new();
  v->audio_enabled = false;
  v->test_pattern = true;
  v->width = 40;
  v->height = 12;
  cr_assert_eq(options_state_set(v), ASCIICHAT_OK);
  SAFE_FREE(v);
  transaction_file(first_path, (rgb_pixel_t){255, 0, 0});
  transaction_file(second_path, (rgb_pixel_t){0, 0, 255});
}
static void transaction_teardown(void) {
  stop_failure_at = start_failure_at = 0;
  join_failures = 0;
  if (current) {
    session_capture_set_audio_context(current, NULL);
    session_capture_destroy(current);
  }
  if (borrowed_audio.initialized)
    audio_destroy(&borrowed_audio);
  platform_unlink(first_path);
  platform_unlink(second_path);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(capture_transactions, .init = transaction_setup, .fini = transaction_teardown, .timeout = 20);
static image_t *transaction_await_frame(void) {
  uint64_t deadline = time_get_ns() + 500 * NS_PER_MS_INT;
  image_t *frame = NULL;
  while (time_get_ns() < deadline) {
    asciichat_errno_scope_t scope = errno_checkpoint();
    frame = session_capture_read_frame(current);
    asciichat_error_context_t failure;
    if (frame || errno_peek_since(scope, &failure))
      break;
    platform_sleep_ms(1);
  }
  return frame;
}
static void open_first(void) {
  session_capture_config_t config = {.type = MEDIA_SOURCE_FILE, .path = first_path, .target_fps = 30};
  current = session_capture_create(&config);
  cr_assert_not_null(current);
  cr_assert_not_null(transaction_await_frame());
  media_source_pause(current->source);
}
static void pending_replacement(void) {
  cr_assert_eq(session_capture_open_media(current, second_path), ASCIICHAT_OK);
  cr_assert_not_null(loader_worker);
  loader_worker(loader_arg);
  cr_assert(current->loader_done);
  cr_assert_not_null(current->loaded_source);
  cr_assert_not_null(current->loaded_frame);
}
Test(capture_transactions, automatic_configuration_selects_each_source_kind_and_preserves_explicit_paths) {
  for (unsigned mode = 0; mode < 4; ++mode) {
    options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
    *v = *options_get();
    v->media_file[0] = 0;
    v->media_from_stdin = false;
    v->test_pattern = false;
    if (mode < 2) {
      SAFE_STRNCPY(v->media_file, mode == 0 ? first_path : "-", sizeof(v->media_file));
      v->media_from_stdin = mode == 1;
    }
    if (mode == 2)
      v->test_pattern = true;
    if (mode == 3) {
      v->webcam_index = 7;
      camera_available = true;
    }
    cr_assert_eq(options_state_set(v), ASCIICHAT_OK);
    SAFE_FREE(v);
    current = session_capture_create(NULL);
    cr_assert_not_null(current);
    const media_source_type_t kinds[] = {MEDIA_SOURCE_FILE, MEDIA_SOURCE_STDIN, MEDIA_SOURCE_TEST, MEDIA_SOURCE_WEBCAM};
    cr_assert_eq(requested_type, kinds[mode]);
    if (mode == 0)
      cr_assert_str_eq(requested_path, first_path);
    if (mode == 3)
      cr_assert_str_eq(requested_path, "7");
    cr_assert_eq(session_capture_get_target_fps(current), 60);
    session_capture_destroy(current);
    current = NULL;
    camera_source = NULL;
  }
}
Test(capture_transactions, source_and_video_start_failures_release_partial_context_and_preserve_error_cause) {
  session_capture_config_t config = {.type = MEDIA_SOURCE_FILE, .path = first_path};
  fail_source = true;
  cr_assert_null(session_capture_create(&config));
  cr_assert_eq(GET_ERRNO(), ERROR_MEDIA_INIT);
  CLEAR_ERRNO_ALL();
  source_sets_error = true;
  cr_assert_null(session_capture_create(&config));
  cr_assert_eq(GET_ERRNO(), ERROR_MEDIA_OPEN);
  CLEAR_ERRNO_ALL();
  fail_source = false;
  fail_video = true;
  cr_assert_null(session_capture_create(&config));
  cr_assert_eq(source_destroys, 1);
  CLEAR_ERRNO_ALL();
  fail_video = false;
  current = session_capture_create(&config);
  cr_assert_not_null(current);
  cr_assert_eq(current->target_fps, 60);
}
Test(capture_transactions, join_failure_defers_commit_without_releasing_either_source) {
  open_first();
  media_source_t *original = current->source;
  uint64_t epoch = session_capture_source_epoch(current);
  pending_replacement();
  media_source_t *replacement = current->loaded_source;
  unsigned before = source_destroys;
  join_failures = 1;
  capture_commit_media(current);
  cr_assert_eq(current->source, original);
  cr_assert_eq(current->loaded_source, replacement);
  cr_assert(current->loader_created);
  cr_assert_eq(source_destroys, before);
  capture_commit_media(current);
  cr_assert_eq(current->source, replacement);
  cr_assert_null(current->loaded_source);
  cr_assert_eq(session_capture_source_epoch(current), epoch + 1);
  cr_assert_not(current->loader_created);
  cr_assert_eq(source_destroys, before + 1);
}
Test(capture_transactions, audio_stop_failure_discards_candidate_and_retains_presented_source) {
  open_first();
  cr_assert_eq(audio_init(&borrowed_audio), ASCIICHAT_OK);
  session_capture_set_audio_context(current, &borrowed_audio);
  media_source_t *original = current->source;
  uint64_t epoch = session_capture_source_epoch(current);
  pending_replacement();
  stop_failure_at = 1;
  unsigned before = source_destroys;
  capture_commit_media(current);
  cr_assert_eq(current->source, original);
  cr_assert_eq(session_capture_source_epoch(current), epoch);
  cr_assert_null(current->loaded_source);
  cr_assert_eq(source_destroys, before + 1);
  cr_assert_not(current->loader_created);
  cr_assert_eq(start_calls, 0);
}
Test(capture_transactions, replacement_audio_start_failure_restores_previous_audio_and_video) {
  open_first();
  cr_assert_eq(audio_init(&borrowed_audio), ASCIICHAT_OK);
  session_capture_set_audio_context(current, &borrowed_audio);
  media_source_t *original = current->source;
  borrowed_audio.capture_media_source = original;
  borrowed_audio.playback_only = true;
  pending_replacement();
  start_failure_at = 1;
  capture_commit_media(current);
  cr_assert_eq(current->source, original);
  cr_assert_eq(borrowed_audio.capture_media_source, original);
  cr_assert(borrowed_audio.playback_only);
  cr_assert_eq(start_calls, 2);
  cr_assert_eq(stop_calls, 2);
  cr_assert_null(current->loaded_source);
}
Test(capture_transactions, failed_stop_of_replacement_audio_retains_the_borrowed_candidate_until_teardown) {
  open_first();
  cr_assert_eq(audio_init(&borrowed_audio), ASCIICHAT_OK);
  session_capture_set_audio_context(current, &borrowed_audio);
  media_source_t *original = current->source;
  pending_replacement();
  media_source_t *replacement = current->loaded_source;
  start_failure_at = 1;
  stop_failure_at = 2;
  capture_commit_media(current);
  cr_assert_eq(current->source, original);
  cr_assert_eq(current->loaded_source, replacement);
  cr_assert_not(current->loader_created);
  cr_assert_eq(start_calls, 1);
  cr_assert_eq(stop_calls, 2);
}
static void toggle_roundtrip(bool camera) {
  open_first();
  cr_assert_eq(audio_init(&borrowed_audio), ASCIICHAT_OK);
  session_capture_set_audio_context(current, &borrowed_audio);
  media_source_t *original = current->source;
  media_source_resume(original);
  borrowed_audio.capture_media_source = original;
  borrowed_audio.playback_only = true;
  borrowed_audio.monitor_local_media = true;
  camera_available = camera;
  session_capture_toggle_media(current);
  image_t *alternate = session_capture_read_frame(current);
  cr_assert_not_null(alternate);
  cr_assert_not(session_capture_media_active(current));
  cr_assert(media_source_is_paused(original));
  cr_assert_eq(current->source, original);
  cr_assert_null(borrowed_audio.capture_media_source);
  cr_assert_not(borrowed_audio.monitor_local_media);
  if (camera)
    cr_assert_not_null(current->webcam_source);
  else {
    cr_assert_null(current->webcam_source);
    cr_assert_not_null(current->empty_frame);
  }
  session_capture_toggle_media(current);
  cr_assert_not_null(transaction_await_frame());
  cr_assert(session_capture_media_active(current));
  cr_assert_not(media_source_is_paused(original));
  cr_assert_eq(borrowed_audio.capture_media_source, original);
  cr_assert(borrowed_audio.playback_only);
  cr_assert(borrowed_audio.monitor_local_media);
}
Test(capture_transactions, hiding_file_media_uses_a_synthetic_camera_and_restores_playback) {
  toggle_roundtrip(true);
}
Test(capture_transactions, missing_camera_keeps_animations_available_and_restores_file_playback) {
  toggle_roundtrip(false);
}
Test(capture_transactions, file_audio_selection_and_bounded_reads_use_the_selected_source) {
  has_audio = true;
  session_capture_config_t config = {
      .type = MEDIA_SOURCE_FILE, .path = first_path, .enable_audio = true, .target_fps = 30};
  current = session_capture_create(&config);
  cr_assert_not_null(current);
  cr_assert(session_capture_has_audio(current));
  cr_assert(session_capture_using_file_audio(current));
  float samples[32] = {0};
  cr_assert_eq(session_capture_read_audio(current, samples, 32), 32);
  for (unsigned i = 0; i < ARRAY_SIZE(samples); ++i)
    cr_assert_float_eq(samples[i], 0.25f, 1e-6);
  cr_assert_eq(session_capture_read_audio(current, NULL, 32), 0);
  cr_assert_eq(session_capture_read_audio(current, samples, 0), 0);
}
