#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include "../../../src/client/connection_internal.h"
#include "../../../src/client/capture.h"
#include "../../../src/client/display.h"
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/thread_pool.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>

atomic_t g_should_exit;
static unsigned ticks, tick_limit, spawn_calls, encoded_calls, raw_calls;
static unsigned frame_mode, fail_encodings;
static bool fail_spawn, fail_open, open_sets_error;
static asciichat_error_t wire_result;
static session_capture_config_t chosen_config;
static char chosen_path[1024];
static void *(*scheduled_worker)(void *);
static void *scheduled_arg;
bool should_exit(void) {
  return atomic_load_bool(&g_should_exit) || (tick_limit && ticks >= tick_limit);
}
void signal_exit(void) {
  atomic_store_bool(&g_should_exit, true);
}
asciichat_error_t client_connection_checkpoint(const char *stage) {
  (void)stage;
  return ASCIICHAT_OK;
}
audio_context_t *audio_get_context(void) {
  return NULL;
}
static session_capture_ctx_t *video_open(const session_capture_config_t *config) {
  chosen_config = *config;
  safe_snprintf(chosen_path, sizeof(chosen_path), "%s", config->path ? config->path : "");
  if (fail_open) {
    if (open_sets_error)
      SET_ERRNO(ERROR_WEBCAM_IN_USE, "Injected busy camera");
    return NULL;
  }
  session_capture_config_t synthetic = *config;
  synthetic.type = MEDIA_SOURCE_TEST;
  synthetic.path = NULL;
  synthetic.enable_audio = false;
  return session_capture_create(&synthetic);
}
static asciichat_error_t video_spawn(thread_pool_t *pool, void *(*fn)(void *), void *arg, int order, const char *name) {
  (void)pool;
  ASCIICHAT_REQUIRE_EQ(order, 2);
  ASCIICHAT_REQUIRE_STR_EQ(name, "webcam_capture");
  ++spawn_calls;
  scheduled_worker = fn;
  scheduled_arg = arg;
  return fail_spawn ? ERROR_THREAD : ASCIICHAT_OK;
}
static void video_pace(session_capture_ctx_t *capture) {
  (void)capture;
  ++ticks;
}
static image_t *video_read(session_capture_ctx_t *capture) {
  if (frame_mode == 1) {
    SET_ERRNO(ERROR_MEDIA_DECODE, "Injected capture decode failure");
    return NULL;
  }
  if (frame_mode == 2)
    return NULL;
  return session_capture_read_frame(capture);
}
static asciichat_error_t video_encoded(client_connection_t *connection, const void *pixels, uint32_t width,
                                       uint32_t height, video_codec_t codec, bool *encoding_failed) {
  (void)connection;
  (void)pixels;
  ASCIICHAT_REQUIRE(codec == VIDEO_CODEC_H264 || codec == VIDEO_CODEC_H265);
  ASCIICHAT_REQUIRE_GT(width, 0);
  ASCIICHAT_REQUIRE_GT(height, 0);
  ++encoded_calls;
  if (encoded_calls <= fail_encodings) {
    *encoding_failed = true;
    return SET_ERRNO(ERROR_MEDIA_INIT, "Injected codec initialization failure");
  }
  *encoding_failed = false;
  return wire_result;
}
static asciichat_error_t video_raw(client_connection_t *connection, const void *pixels, uint32_t width, uint32_t height,
                                   uint32_t format) {
  (void)connection;
  ASCIICHAT_REQUIRE_NOT_NULL(pixels);
  ASCIICHAT_REQUIRE_GT(width, 0);
  ASCIICHAT_REQUIRE_GT(height, 0);
  ASCIICHAT_REQUIRE_EQ(format, 1);
  ++raw_calls;
  return wire_result;
}
#define session_capture_create video_open
#define thread_pool_spawn video_spawn
#define session_capture_sleep_for_fps video_pace
#define session_capture_read_frame video_read
#define threaded_send_image_frame_encoded video_encoded
#define threaded_send_image_frame video_raw
#include "../../../src/client/capture.c"
#undef session_capture_create
#undef thread_pool_spawn
#undef session_capture_sleep_for_fps
#undef session_capture_read_frame
#undef threaded_send_image_frame_encoded
#undef threaded_send_image_frame
#define session_capture_create video_open
#include "../../../src/client/display.c"
#undef session_capture_create

static client_connection_t video_connection;
static asciichat_error_t video_send(acip_transport_t *transport, const void *data, size_t length) {
  (void)transport;
  (void)data;
  (void)length;
  return ASCIICHAT_OK;
}
static bool video_connected(acip_transport_t *transport) {
  (void)transport;
  return true;
}
static const acip_transport_methods_t video_methods = {.send = video_send, .is_connected = video_connected};
static acip_transport_t video_transport = {.methods = &video_methods};
static session_display_ctx_t *owned_display;
static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  ASCIICHAT_REQUIRE_EQ(options_state_init(), ASCIICHAT_OK);
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = options_t_new();
  v->test_pattern = true;
  v->audio_enabled = false;
  v->width = 40;
  v->height = 12;
  v->snapshot_mode = true;
  ASCIICHAT_REQUIRE_EQ(options_state_set(v), ASCIICHAT_OK);
  SAFE_FREE(v);
  ASCIICHAT_REQUIRE_EQ(server_connection_init(&video_connection), ASCIICHAT_OK);
  server_connection_set_transport(&video_connection, &video_transport);
  tick_limit = 1;
}
static void teardown(void) {
  display_cleanup();
  if (owned_display)
    session_display_destroy(owned_display);
  capture_cleanup();
  h265_encoder_destroy(video_connection.video_encoder);
  mutex_destroy(&video_connection.send_mutex);
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
static options_t *video_options(void) {
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = *options_get();
  return v;
}
static void video_publish(options_t *v) {
  ASCIICHAT_REQUIRE_EQ(options_state_set(v), ASCIICHAT_OK);
  SAFE_FREE(v);
}
static void start_capture(void) {
  ASCIICHAT_REQUIRE_EQ(capture_init(), 0);
  video_connection.capture_context = capture_get_context();
  ASCIICHAT_REQUIRE_NOT_NULL(video_connection.capture_context);
}
static void run_worker(void) {
  ASCIICHAT_REQUIRE_EQ(capture_start_thread(&video_connection), 0);
  ASCIICHAT_REQUIRE_NOT_NULL(scheduled_worker);
  scheduled_worker(scheduled_arg);
  ASCIICHAT_REQUIRE(capture_thread_exited(&video_connection));
  capture_stop_thread(&video_connection);
}
TEST_CASE("client_video_contract/capture_selection_prioritizes_url_then_stdin_file_test_pattern_and_camera", "[client_video_contract]") {
  for (unsigned scenario = 0; scenario < 5; ++scenario) {
    options_t *v = video_options();
    v->media_url[0] = v->media_file[0] = 0;
    v->media_from_stdin = false;
    v->media_loop = true;
    v->test_pattern = true;
    v->webcam_index = 4;
    v->fps = scenario ? 60 : 0;
    if (scenario == 0) {
      SAFE_STRNCPY(v->media_url, "http://127.0.0.1/local", sizeof(v->media_url));
      SAFE_STRNCPY(v->media_file, "ignored", sizeof(v->media_file));
    }
    if (scenario == 1) {
      SAFE_STRNCPY(v->media_file, "-", sizeof(v->media_file));
      v->media_from_stdin = true;
    }
    if (scenario == 2)
      SAFE_STRNCPY(v->media_file, "fixture.ppm", sizeof(v->media_file));
    if (scenario == 4)
      v->test_pattern = false;
    video_publish(v);
    ASCIICHAT_REQUIRE_EQ(capture_init(), 0);
    const media_source_type_t expected[] = {MEDIA_SOURCE_FILE, MEDIA_SOURCE_STDIN, MEDIA_SOURCE_FILE, MEDIA_SOURCE_TEST,
                                            MEDIA_SOURCE_WEBCAM};
    ASCIICHAT_REQUIRE_EQ(chosen_config.type, expected[scenario]);
    ASCIICHAT_REQUIRE_EQ(chosen_config.target_fps, scenario ? 60 : 144);
    ASCIICHAT_REQUIRE(chosen_config.resize_for_network);
    ASCIICHAT_REQUIRE(chosen_config.enable_audio);
    ASCIICHAT_REQUIRE(chosen_config.audio_fallback_to_mic);
    if (scenario == 0)
      ASCIICHAT_REQUIRE_STR_EQ(chosen_path, "http://127.0.0.1/local");
    if (scenario == 1)
      ASCIICHAT_REQUIRE_NOT(chosen_config.loop);
    if (scenario == 2)
      ASCIICHAT_REQUIRE(chosen_config.loop);
    if (scenario == 4)
      ASCIICHAT_REQUIRE_STR_EQ(chosen_path, "4");
    ASCIICHAT_REQUIRE_NOT_NULL(capture_get_media_source());
    capture_cleanup();
    ASCIICHAT_REQUIRE_NULL(capture_get_context());
  }
}
TEST_CASE("client_video_contract/capture_factory_preserves_specific_failure_and_supplies_a_fallback_error", "[client_video_contract]") {
  fail_open = true;
  open_sets_error = true;
  ASCIICHAT_REQUIRE_EQ(capture_init(), ERROR_WEBCAM_IN_USE);
  ASCIICHAT_REQUIRE_NULL(capture_get_context());
  CLEAR_ERRNO_ALL();
  open_sets_error = false;
  ASCIICHAT_REQUIRE_EQ(capture_init(), -1);
  asciichat_error_context_t error = {0};
  ASCIICHAT_REQUIRE(HAS_ERRNO(&error));
  ASCIICHAT_REQUIRE_EQ(error.code, ERROR_MEDIA_INIT);
}
TEST_CASE("client_video_contract/capture_worker_start_failure_is_retryable_and_existing_worker_is_not_duplicated", "[client_video_contract]") {
  ASCIICHAT_REQUIRE_EQ(capture_start_thread(&video_connection), 0);
  ASCIICHAT_REQUIRE_EQ(spawn_calls, 0);
  start_capture();
  fail_spawn = true;
  ASCIICHAT_REQUIRE_EQ(capture_start_thread(&video_connection), -1);
  ASCIICHAT_REQUIRE_NOT(video_connection.capture_thread_created);
  fail_spawn = false;
  CLEAR_ERRNO_ALL();
  ASCIICHAT_REQUIRE_EQ(capture_start_thread(&video_connection), 0);
  ASCIICHAT_REQUIRE_EQ(spawn_calls, 2);
  ASCIICHAT_REQUIRE_EQ(capture_start_thread(&video_connection), 0);
  ASCIICHAT_REQUIRE_EQ(spawn_calls, 2);
  atomic_store_bool(&video_connection.capture_thread_exited, true);
  capture_stop_thread(&video_connection);
  capture_stop_thread(&video_connection);
  capture_stop_thread(NULL);
  ASCIICHAT_REQUIRE_NOT(video_connection.capture_thread_created);
}
TEST_CASE("client_video_contract/automatic_codec_failure_falls_back_to_h264_then_raw_without_losing_connection", "[client_video_contract]") {
  options_t *v = video_options();
  SAFE_STRNCPY(v->video_codec, "auto", sizeof(v->video_codec));
  video_publish(v);
  start_capture();
  fail_encodings = 2;
  wire_result = ASCIICHAT_OK;
  run_worker();
  ASCIICHAT_REQUIRE_EQ(encoded_calls, 2);
  ASCIICHAT_REQUIRE_EQ(raw_calls, 1);
  ASCIICHAT_REQUIRE_NOT(server_connection_is_lost(&video_connection));
  ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&g_should_exit));
}

TEST_CASE("client_video_contract/encoder_availability_probe_does_not_create_client_errors", "[client_video_contract]") {
  asciichat_errno_scope_t scope = errno_scope_begin();
  (void)video_encoder_is_available(VIDEO_CODEC_H265);
  (void)video_encoder_is_available(VIDEO_CODEC_H264);
  ASCIICHAT_REQUIRE(errno_scope_is_clean(scope), "encoder capability probing must not leave client errors");
  errno_scope_end(scope, ASCIICHAT_ERRNO_HANDLED);
}
TEST_CASE("client_video_contract/forced_codec_failure_requests_exit_without_falling_back", "[client_video_contract]") {
  options_t *v = video_options();
  SAFE_STRNCPY(v->video_codec, "h264", sizeof(v->video_codec));
  video_publish(v);
  start_capture();
  fail_encodings = 1;
  run_worker();
  ASCIICHAT_REQUIRE_EQ(encoded_calls, 1);
  ASCIICHAT_REQUIRE_EQ(raw_calls, 0);
  ASCIICHAT_REQUIRE(atomic_load_bool(&g_should_exit));
}
TEST_CASE("client_video_contract/capture_decode_failure_requests_exit_before_encoding_any_frame", "[client_video_contract]") {
  start_capture();
  frame_mode = 1;
  run_worker();
  ASCIICHAT_REQUIRE_EQ(encoded_calls, 0);
  ASCIICHAT_REQUIRE(atomic_load_bool(&g_should_exit));
}
TEST_CASE("client_video_contract/missing_frame_can_wait_for_later_capture_without_losing_connection", "[client_video_contract]") {
  start_capture();
  frame_mode = 2;
  run_worker();
  ASCIICHAT_REQUIRE_EQ(encoded_calls, 0);
  ASCIICHAT_REQUIRE_EQ(raw_calls, 0);
  ASCIICHAT_REQUIRE_NOT(server_connection_is_lost(&video_connection));
}
TEST_CASE("client_video_contract/local_video_rejection_is_dropped_while_transport_failure_marks_connection_lost", "[client_video_contract]") {
  start_capture();
  wire_result = ERROR_INVALID_PARAM;
  run_worker();
  ASCIICHAT_REQUIRE_NOT(server_connection_is_lost(&video_connection));
  ticks = 0;
  video_connection.capture_thread_created = false;
  wire_result = ERROR_NETWORK;
  CLEAR_ERRNO_ALL();
  run_worker();
  ASCIICHAT_REQUIRE(server_connection_is_lost(&video_connection));
}
TEST_CASE("client_video_contract/raw_codec_and_requested_encoded_codec_send_a_frame_without_fallback", "[client_video_contract]") {
  options_t *v = video_options();
  SAFE_STRNCPY(v->video_codec, "raw", sizeof(v->video_codec));
  video_publish(v);
  start_capture();
  run_worker();
  ASCIICHAT_REQUIRE_EQ(raw_calls, 1);
  ASCIICHAT_REQUIRE_EQ(encoded_calls, 0);
  ticks = 0;
  v = video_options();
  SAFE_STRNCPY(v->video_codec, "h264", sizeof(v->video_codec));
  video_publish(v);
  run_worker();
  ASCIICHAT_REQUIRE_EQ(encoded_calls, 1);
  ASCIICHAT_REQUIRE_EQ(raw_calls, 1);
}
TEST_CASE("client_video_contract/display_adapter_honors_first_frame_and_keeps_borrowed_display_alive_after_cleanup", "[client_video_contract]") {
  session_display_config_t config = {.snapshot_mode = true, .color_mode = TERM_COLOR_NONE};
  owned_display = session_display_create(&config);
  ASCIICHAT_REQUIRE_NOT_NULL(owned_display);
  display_set_context(owned_display);
  video_connection.display_context = owned_display;
  display_reset_for_new_connection(&video_connection);
  ASCIICHAT_REQUIRE(atomic_load_bool(&video_connection.display_first_frame));
  display_render_frame(&video_connection, NULL);
  ASCIICHAT_REQUIRE(atomic_load_bool(&video_connection.display_first_frame));
  FILE *file = platform_tmpfile();
  ASCIICHAT_REQUIRE_NOT_NULL(file);
  int saved = platform_dup(1);
  ASCIICHAT_REQUIRE_GE(saved, 0);
  ASCIICHAT_REQUIRE_EQ(platform_dup2(fileno(file), 1), ASCIICHAT_OK);
  display_render_frame(&video_connection, "frame");
  fflush(file);
  ASCIICHAT_REQUIRE_GT(ftell(file), 0);
  ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&video_connection.display_first_frame));
  display_disable_logging_for_first_frame(&video_connection);
  display_poll_keyboard(&video_connection);
  display_full_reset();
  display_cleanup();
  ASCIICHAT_REQUIRE_NOT_NULL(session_display_get_caps(owned_display));
  ASCIICHAT_REQUIRE_EQ(platform_dup2(saved, 1), ASCIICHAT_OK);
  platform_close(saved);
  fclose(file);
  video_connection.display_context = NULL;
  display_render_frame(&video_connection, "ignored");
  display_set_context(NULL);
}

TEST_CASE("client_video_contract/display_initialization_selects_local_controls_for_file_url_and_stdin_and_releases_them", "[client_video_contract]") {
  for (unsigned scenario = 0; scenario < 4; ++scenario) {
    options_t *v = video_options();
    v->media_url[0] = v->media_file[0] = 0;
    v->palette_custom_set = true;
    v->palette_type = PALETTE_CUSTOM;
    SAFE_STRNCPY(v->palette_custom, "XX", sizeof(v->palette_custom));
    if (scenario == 1)
      SAFE_STRNCPY(v->media_file, "local.ppm", sizeof(v->media_file));
    if (scenario == 2)
      SAFE_STRNCPY(v->media_file, "-", sizeof(v->media_file));
    if (scenario == 3) {
      SAFE_STRNCPY(v->media_url, "http://127.0.0.1/local", sizeof(v->media_url));
      SAFE_STRNCPY(v->media_file, "ignored", sizeof(v->media_file));
    }
    video_publish(v);
    ASCIICHAT_REQUIRE_EQ(display_init(), 0);
    owned_display = g_display_ctx;
    ASCIICHAT_REQUIRE_NOT_NULL(owned_display);
    if (scenario) {
      ASCIICHAT_REQUIRE_NOT_NULL(g_display_capture_ctx);
      ASCIICHAT_REQUIRE_EQ(chosen_config.type, scenario == 2 ? MEDIA_SOURCE_STDIN : MEDIA_SOURCE_FILE);
      if (scenario == 3)
        ASCIICHAT_REQUIRE_STR_EQ(chosen_path, "http://127.0.0.1/local");
    } else
      ASCIICHAT_REQUIRE_NULL(g_display_capture_ctx);
    display_has_tty();
    display_cleanup();
    ASCIICHAT_REQUIRE_NULL(g_display_capture_ctx);
    ASCIICHAT_REQUIRE_NULL(g_display_ctx);
    ASCIICHAT_REQUIRE_NOT_NULL(session_display_get_caps(owned_display));
    session_display_destroy(owned_display);
    owned_display = NULL;
  }
}
TEST_CASE("client_video_contract/local_capture_failure_degrades_keyboard_controls_while_display_remains_available", "[client_video_contract]") {
  options_t *v = video_options();
  SAFE_STRNCPY(v->media_file, "unavailable.ppm", sizeof(v->media_file));
  video_publish(v);
  fail_open = true;
  open_sets_error = true;
  ASCIICHAT_REQUIRE_EQ(display_init(), 0);
  owned_display = g_display_ctx;
  ASCIICHAT_REQUIRE_NOT_NULL(owned_display);
  ASCIICHAT_REQUIRE_NULL(g_display_capture_ctx);
  display_set_context(owned_display);
  atomic_store_bool(&g_should_exit, true);
  display_full_reset();
  display_cleanup();
  ASCIICHAT_REQUIRE_NOT_NULL(session_display_get_caps(owned_display));
}
