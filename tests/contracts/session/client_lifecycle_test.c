#include <criterion/criterion.h>
#include "../../../src/common/session/client_like.h"
#include "../../../src/common/session/capture.h"
#include "../../../src/common/session/display.h"
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/video/webcam/webcam.h>
#include <ascii-chat/terminal/fd/reader.h>
#include <ascii-chat/audio/audio.h>
static bool stdout_tty, stdin_tty, fail_enumeration, fail_display, fail_capture, fail_reader, fail_audio_init,
    fail_audio_start, fail_audio_stop, file_audio;
static unsigned devices_available, enumerations, freed_lists, audio_starts, audio_stops, attempts, slept;
static bool exit_requested;
static bool keepawake_failure;
static unsigned keepawake_calls;
static asciichat_error_t attempt_result;
static asciichat_error_t lifecycle_keepawake(void) {
  ++keepawake_calls;
  return keepawake_failure ? SET_ERRNO(ERROR_PLATFORM_INIT, "No system bus") : ASCIICHAT_OK;
}
static bool lifecycle_tty(void) {
  return stdout_tty;
}
static bool lifecycle_stdin(void) {
  return stdin_tty;
}
static asciichat_error_t lifecycle_enumerate(webcam_device_info_t **devices, unsigned *count) {
  ++enumerations;
  if (fail_enumeration)
    return ERROR_WEBCAM;
  *count = devices_available;
  *devices = devices_available ? SAFE_CALLOC(devices_available, sizeof(**devices), webcam_device_info_t *) : NULL;
  return ASCIICHAT_OK;
}
static void lifecycle_free_devices(webcam_device_info_t *devices) {
  ++freed_lists;
  SAFE_FREE(devices);
}
static session_display_ctx_t *lifecycle_display(const session_display_config_t *config) {
  return fail_display ? NULL : session_display_create(config);
}
static session_capture_ctx_t *lifecycle_network_capture(uint32_t fps) {
  return fail_capture ? NULL : session_network_capture_create(fps);
}
static asciichat_error_t lifecycle_reader(int fd, int rows, terminal_fd_reader_t **out) {
  return fail_reader ? ERROR_MEDIA_INIT : terminal_fd_reader_create(fd, rows, out);
}
static bool lifecycle_has_audio(media_source_t *source) {
  (void)source;
  return file_audio;
}
static asciichat_error_t lifecycle_audio_init(audio_context_t *context) {
  return fail_audio_init ? ERROR_AUDIO : audio_init(context);
}
static asciichat_error_t lifecycle_audio_start(audio_context_t *context) {
  ++audio_starts;
  if (fail_audio_start)
    return ERROR_AUDIO;
  context->running = true;
  return ASCIICHAT_OK;
}
static asciichat_error_t lifecycle_audio_stop(audio_context_t *context) {
  ++audio_stops;
  if (fail_audio_stop)
    return ERROR_AUDIO;
  context->running = false;
  return ASCIICHAT_OK;
}
#define terminal_is_stdout_tty lifecycle_tty
#define terminal_is_stdin_tty lifecycle_stdin
#define webcam_list_devices lifecycle_enumerate
#define webcam_free_device_list lifecycle_free_devices
#define session_display_create lifecycle_display
#define session_network_capture_create lifecycle_network_capture
#define terminal_fd_reader_create lifecycle_reader
#define media_source_has_audio lifecycle_has_audio
#define audio_init lifecycle_audio_init
#define audio_start_duplex lifecycle_audio_start
#define audio_stop_duplex lifecycle_audio_stop
#define platform_enable_keepawake lifecycle_keepawake
#include "../../../src/common/session/client_like.c"
#undef terminal_is_stdout_tty
#undef terminal_is_stdin_tty
#undef webcam_list_devices
#undef webcam_free_device_list
#undef session_display_create
#undef session_network_capture_create
#undef terminal_fd_reader_create
#undef media_source_has_audio
#undef audio_init
#undef audio_start_duplex
#undef audio_stop_duplex
#undef platform_enable_keepawake
static client_like_state_t state;
static bool lifecycle_exit(void) {
  return exit_requested;
}
static bool lifecycle_custom_exit(void *data) {
  cr_assert_eq(data, &exit_requested);
  return exit_requested;
}
static void lifecycle_sleep(unsigned ms) {
  slept += ms;
}
static asciichat_error_t lifecycle_attempt(session_capture_ctx_t *capture, session_display_ctx_t *display, void *data) {
  cr_assert_eq(capture, state.capture);
  cr_assert_eq(display, state.display);
  cr_assert_eq(data, &attempts);
  ++attempts;
  return attempts >= 3 ? ASCIICHAT_OK : attempt_result;
}
static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  static app_callbacks_t callbacks = {.should_exit = lifecycle_exit, .platform_sleep_ms = lifecycle_sleep};
  app_callbacks_register(&callbacks);
}
static void teardown(void) {
  // Retry attempts may start the splash monitor even without a display context.
  // Join it before releasing options, logging, or callback-owned fixture data.
  splash_intro_done();
  splash_wait_for_animation();
  ui_controller_shutdown();
  session_display_set_global_context(NULL);
  if (state.display)
    session_display_destroy(state.display);
  if (state.capture)
    session_capture_destroy(state.capture);
  if (state.audio_ctx) {
    if (state.probe_source)
      media_source_set_audio_context(state.probe_source, NULL);
    state.audio_ctx->running = false;
    audio_destroy(state.audio_ctx);
    SAFE_FREE(state.audio_ctx);
  }
  if (state.probe_source)
    media_source_destroy(state.probe_source);
  if (g_stdin_reader) {
    terminal_fd_reader_destroy(g_stdin_reader);
    g_stdin_reader = NULL;
  }
  app_callbacks_register(NULL);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(client_lifecycle, .init = setup, .fini = teardown, .timeout = 20);
Test(client_lifecycle, optional_keepawake_failure_preserves_prior_errors_without_notification) {
  errno_notifications_enable(true);
  SET_ERRNO(ERROR_NETWORK, "Earlier failure");
  size_t notifications = errno_notifications_snapshot(NULL, ASCIICHAT_ERRNO_HISTORY_SIZE, NULL);
  asciichat_errno_scope_t scope = errno_scope_begin();
  keepawake_failure = true;
  cr_assert_eq(client_like_setup_keepawake(), ASCIICHAT_OK);
  cr_assert_eq(keepawake_calls, 1);
  cr_assert(errno_scope_is_clean(scope));
  cr_assert(HAS_ERRNO_CODE(ERROR_NETWORK));
  cr_assert_eq(errno_notifications_snapshot(NULL, ASCIICHAT_ERRNO_HISTORY_SIZE, NULL), notifications);
}
static options_t *lifecycle_options(void) {
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = *options_get();
  return v;
}
static void lifecycle_publish(options_t *v) {
  cr_assert_eq(options_state_set(v), ASCIICHAT_OK);
  SAFE_FREE(v);
}
Test(client_lifecycle, mirror_webcam_selection_validates_devices_but_explicit_sources_bypass_enumeration) {
  session_client_like_config_t c = {.kind = SESSION_CLIENT_LIKE_KIND_MIRROR};
  stdout_tty = true;
  options_t *v = lifecycle_options();
  v->test_pattern = v->waveform = v->fft = false;
  v->media_file[0] = v->media_url[0] = 0;
  v->webcam_index = 0;
  lifecycle_publish(v);
  fail_enumeration = true;
  cr_assert_eq(client_like_validate_mirror_webcam_selection(&c), ERROR_WEBCAM);
  cr_assert_eq(freed_lists, 0);
  fail_enumeration = false;
  cr_assert_eq(client_like_validate_mirror_webcam_selection(&c), ERROR_WEBCAM);
  cr_assert_eq(freed_lists, 1);
  devices_available = 1;
  cr_assert_eq(client_like_validate_mirror_webcam_selection(&c), ASCIICHAT_OK);
  cr_assert_eq(freed_lists, 2);
  v = lifecycle_options();
  v->webcam_index = 2;
  lifecycle_publish(v);
  cr_assert_eq(client_like_validate_mirror_webcam_selection(&c), ERROR_WEBCAM);
  cr_assert_eq(freed_lists, 3);
  v = lifecycle_options();
  SAFE_STRNCPY(v->media_file, "explicit.ppm", sizeof(v->media_file));
  lifecycle_publish(v);
  unsigned before = enumerations;
  cr_assert_eq(client_like_validate_mirror_webcam_selection(&c), ASCIICHAT_OK);
  cr_assert_eq(enumerations, before);
  v = lifecycle_options();
  v->media_file[0] = 0;
  SAFE_STRNCPY(v->media_url, "http://127.0.0.1/test", sizeof(v->media_url));
  lifecycle_publish(v);
  cr_assert_eq(client_like_validate_mirror_webcam_selection(&c), ASCIICHAT_OK);
  cr_assert_eq(enumerations, before);
}
Test(client_lifecycle, absent_required_run_and_conflicting_keepawake_fail_before_creating_display) {
  cr_assert_eq(session_client_like_run(NULL), ERROR_INVALID_PARAM);
  session_client_like_config_t c = {0};
  cr_assert_eq(session_client_like_run(&c), ERROR_INVALID_PARAM);
  options_t *v = lifecycle_options();
  v->enable_keepawake = v->disable_keepawake = true;
  lifecycle_publish(v);
  cr_assert_eq(client_like_setup_keepawake(), ERROR_INVALID_PARAM);
  fail_display = true;
  cr_assert_eq(client_like_create_session_display(&state), ERROR_DISPLAY);
  cr_assert_null(state.display);
  cr_assert_null(session_display_get_global_context());
}
Test(client_lifecycle, exit_adapters_combine_global_and_per_session_cancellation) {
  g_current_config = (session_client_like_config_t){0};
  cr_assert_not(client_like_capture_should_exit_adapter(NULL));
  cr_assert_not(client_like_display_should_exit_adapter(NULL));
  g_current_config.custom_should_exit = lifecycle_custom_exit;
  g_current_config.exit_user_data = &exit_requested;
  cr_assert_not(client_like_capture_should_exit_adapter(NULL));
  exit_requested = true;
  cr_assert(client_like_capture_should_exit_adapter(NULL));
  cr_assert(client_like_display_should_exit_adapter(NULL));
  g_current_config = (session_client_like_config_t){0};
}
Test(client_lifecycle, stdin_render_constraints_reject_width_and_reader_failure_before_allocating_capture) {
  session_client_like_config_t c = {.kind = SESSION_CLIENT_LIKE_KIND_CLIENT};
  state.capture_config.target_fps = 60;
  options_t *v = lifecycle_options();
  v->test_pattern = false;
  v->media_file[0] = v->media_url[0] = 0;
  SAFE_STRNCPY(v->render_file, "-", sizeof(v->render_file));
  v->width = 40;
  v->height = 12;
  lifecycle_publish(v);
  cr_assert_eq(client_like_create_network_capture(&c, &state), ERROR_USAGE);
  cr_assert_null(state.capture);
  cr_assert_null(g_stdin_reader);
  v = lifecycle_options();
  v->width = OPT_WIDTH_DEFAULT;
  lifecycle_publish(v);
  fail_reader = true;
  cr_assert_eq(client_like_create_network_capture(&c, &state), ERROR_MEDIA_INIT);
  cr_assert_null(g_stdin_reader);
  cr_assert_null(state.capture);
}
Test(client_lifecycle, capture_factory_failure_propagates_for_network_capture_without_creating_local_devices) {
  session_client_like_config_t c = {.kind = SESSION_CLIENT_LIKE_KIND_CLIENT};
  state.capture_config.target_fps = 60;
  fail_capture = true;
  options_t *v = lifecycle_options();
  v->render_file[0] = 0;
  lifecycle_publish(v);
  cr_assert_eq(client_like_create_network_capture(&c, &state), ERROR_MEDIA_INIT);
  cr_assert_null(state.capture);
  fail_capture = false;
  cr_assert_eq(client_like_create_network_capture(&c, &state), ASCIICHAT_OK);
  cr_assert_not_null(state.capture);
  cr_assert_null(session_capture_get_media_source(state.capture));
}
Test(client_lifecycle, mirror_audio_start_failure_retains_context_and_retries_only_after_options_change) {
  options_t *v = lifecycle_options();
  v->audio_enabled = true;
  v->test_audio = true;
  v->audio_no_playback = true;
  lifecycle_publish(v);
  media_source_t *source = media_source_create(MEDIA_SOURCE_TEST, NULL);
  cr_assert_not_null(source);
  state.probe_source = source;
  fail_audio_start = true;
  audio_context_t *audio = client_like_update_mirror_audio(source, &state);
  cr_assert_not_null(audio);
  cr_assert_not(state.audio_available);
  cr_assert_not(audio->running);
  cr_assert_eq(audio_starts, 1);
  cr_assert_eq(client_like_update_mirror_audio(source, &state), audio);
  cr_assert_eq(audio_starts, 1);
  v = lifecycle_options();
  v->fps = 45;
  lifecycle_publish(v);
  fail_audio_start = false;
  cr_assert_eq(client_like_update_mirror_audio(source, &state), audio);
  cr_assert_eq(audio_starts, 2);
  cr_assert(state.audio_available);
  cr_assert(audio->running);
  cr_assert_eq(client_like_update_mirror_audio(source, &state), audio);
  cr_assert_eq(audio_starts, 2);
  v = lifecycle_options();
  v->audio_enabled = false;
  lifecycle_publish(v);
  fail_audio_stop = true;
  client_like_update_mirror_audio(source, &state);
  cr_assert(audio->running);
  fail_audio_stop = false;
  client_like_update_mirror_audio(source, &state);
  cr_assert_not(audio->running);
  cr_assert_eq(audio_stops, 2);
}
Test(client_lifecycle, mirror_audio_init_failure_has_no_context_and_file_audio_selection_preserves_source_ownership) {
  options_t *v = lifecycle_options();
  v->audio_enabled = true;
  v->test_audio = true;
  v->audio_no_playback = true;
  v->audio_capture_source = AUDIO_CAPTURE_SOURCE_MEDIA;
  lifecycle_publish(v);
  media_source_t *source = media_source_create(MEDIA_SOURCE_TEST, NULL);
  cr_assert_not_null(source);
  state.probe_source = source;
  fail_audio_init = true;
  cr_assert_null(client_like_update_mirror_audio(source, &state));
  cr_assert_null(state.audio_ctx);
  cr_assert_eq(audio_starts, 0);
  fail_audio_init = false;
  file_audio = true;
  v = lifecycle_options();
  v->fps = 45;
  lifecycle_publish(v);
  audio_context_t *audio = client_like_update_mirror_audio(source, &state);
  cr_assert_not_null(audio);
  cr_assert_eq(audio->capture_media_source, source);
  cr_assert(audio->monitor_local_media);
  cr_assert(audio->playback_only);
}
Test(client_lifecycle, default_retry_policy_obeys_limits_and_success_and_uses_bounded_rejection_backoff) {
  session_client_like_config_t c = {.kind = SESSION_CLIENT_LIKE_KIND_CLIENT,
                                    .run_fn = lifecycle_attempt,
                                    .run_user_data = &attempts,
                                    .max_reconnect_attempts = 2,
                                    .reconnect_delay_ms = 125};
  attempt_result = ERROR_NETWORK;
  cr_assert_eq(client_like_run_session_attempts(&c, &state), ERROR_NETWORK);
  cr_assert_eq(attempts, 2);
  cr_assert_eq(slept, 125);
  attempts = slept = 0;
  c.max_reconnect_attempts = -1;
  attempt_result = ERROR_SESSION_FULL;
  cr_assert_eq(client_like_run_session_attempts(&c, &state), ASCIICHAT_OK);
  cr_assert_eq(attempts, 3);
  cr_assert_eq(slept, 6000);
}
