#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/util/time.h>
#include "../../../src/discovery/session.h"
#include "session/client_like.h"
#include <ascii-chat/audio/client_pipeline.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/tui/controller.h>
#include "session/audio.h"
#include "session/capture.h"
static unsigned joins, join_failures, writes, sleeps, exits, loops, stops;
static asciichat_error_t write_result, process_result, handoff_result, render_result, run_result;
static bool cancelled, fail_create, report_error, activate, exit_on_process;
static int chunks[8];
static unsigned audio_failure;
static bool fail_read;
static image_t *media_read_frame(session_capture_ctx_t *capture) {
  if (fail_read) {
    SET_ERRNO(ERROR_MEDIA_DECODE, "fixture capture failed");
    return NULL;
  }
  return session_capture_read_frame(capture);
}
static bool end_after_frame;
static asciichat_error_t frame_result;
static session_audio_ctx_t *media_audio_create(bool host) {
  return audio_failure == 1 ? NULL : session_audio_create(host);
}
static asciichat_error_t media_audio_start(session_audio_ctx_t *audio) {
  ASCIICHAT_REQUIRE_NOT_NULL(audio);
  return audio_failure == 2 ? ERROR_AUDIO : ASCIICHAT_OK;
}
static asciichat_error_t media_thread_create(platform_thread_t *thread, const char *name, void *(*fn)(void *),
                                             void *arg) {
  (void)thread;
  (void)name;
  (void)fn;
  (void)arg;
  return ERROR_THREAD;
}
static asciichat_error_t media_join(platform_thread_t *thread, void **result) {
  (void)thread;
  (void)result;
  ++joins;
  return joins <= join_failures ? ERROR_THREAD : ASCIICHAT_OK;
}
static asciichat_error_t media_write(audio_context_t *ctx, const float *samples, int count) {
  (void)ctx;
  ASCIICHAT_REQUIRE_NOT_NULL(samples);
  ASCIICHAT_REQUIRE_LT(writes, 8);
  chunks[writes++] = count;
  return write_result;
}
static void media_sleep_ns(uint64_t ns) {
  (void)ns;
  ++sleeps;
}
static asciichat_error_t media_process(discovery_session_t *s, uint64_t timeout) {
  (void)timeout;
  ++loops;
  if (activate)
    s->state = DISCOVERY_STATE_ACTIVE;
  if (end_after_frame && loops > 1) {
    s->state = DISCOVERY_STATE_ENDED;
    return frame_result;
  }
  if (exit_on_process)
    cancelled = true;
  return process_result;
}
static asciichat_error_t media_handoff(discovery_session_t *s) {
  (void)s;
  return handoff_result;
}
static asciichat_error_t media_render(session_host_t *host) {
  (void)host;
  return render_result;
}
static asciichat_error_t media_run(const session_client_like_config_t *config);
static discovery_session_t *media_create(const discovery_config_t *config) {
  return fail_create ? NULL : discovery_session_create(config);
}
static void media_stop(discovery_session_t *s) {
  ++stops;
  discovery_session_stop(s);
}
static uint64_t media_clock_ns(void) {
  static uint64_t now;
  return now += 100;
}
#define time_get_ns media_clock_ns
#define session_capture_read_frame media_read_frame
#define session_audio_create media_audio_create
#define session_audio_start_capture media_audio_start
#define platform_thread_create media_thread_create
#define thread_join media_join
#define audio_write_samples media_write
#define platform_sleep_ns media_sleep_ns
#define discovery_session_process media_process
#define discovery_session_apply_handoff media_handoff
#define session_host_start_render media_render
#define session_client_like_run media_run
#define discovery_session_create media_create
#define discovery_session_stop media_stop
#include "../../../src/discovery/main.c"
#undef time_get_ns
#undef session_capture_read_frame
#undef session_audio_create
#undef session_audio_start_capture
#undef platform_thread_create
#undef thread_join
#undef audio_write_samples
#undef platform_sleep_ns
#undef discovery_session_process
#undef discovery_session_apply_handoff
#undef session_host_start_render
#undef session_client_like_run
#undef discovery_session_create
#undef discovery_session_stop
bool should_exit(void) {
  return cancelled;
}
void signal_exit(void) {
  cancelled = true;
  ++exits;
}
void set_interrupt_callback(void (*callback)(void)) {
  ASCIICHAT_REQUIRE_NULL(callback);
}
static asciichat_error_t media_run(const session_client_like_config_t *config) {
  ASCIICHAT_REQUIRE_EQ(config->kind, SESSION_CLIENT_LIKE_KIND_DISCOVERY);
  ASCIICHAT_REQUIRE_EQ(config->discovery, g_discovery);
  ASCIICHAT_REQUIRE_EQ(config->max_reconnect_attempts, 0);
  ASCIICHAT_REQUIRE_NOT_NULL(config->prepare_fn);
  ASCIICHAT_REQUIRE_NOT_NULL(config->run_fn);
  ASCIICHAT_REQUIRE_NOT_NULL(config->keyboard_handler);
  if (report_error)
    g_discovery->on_error(ERROR_NETWORK, "peer negotiation failed", g_discovery->callback_user_data);
  return run_result;
}
static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  ASCIICHAT_REQUIRE_EQ(options_state_init(), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(options_set_bool("audio_enabled", false), ASCIICHAT_OK);
}
static void teardown(void) {
  if (g_discovery) {
    discovery_session_destroy(g_discovery);
    g_discovery = NULL;
  }
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TEST_CASE("discovery_media/joins_preserve_worker_flags_until_each_handle_has_been_joined", "[discovery_media]") {
  discovery_video_receiver_t receiver = {.receiver_started = true, .audio_started = true};
  join_failures = 1;
  ASCIICHAT_REQUIRE_EQ(discovery_media_join(&receiver), ERROR_THREAD);
  ASCIICHAT_REQUIRE(receiver.receiver_started && receiver.audio_started);
  ASCIICHAT_REQUIRE_EQ(discovery_media_join(&receiver), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NOT(receiver.receiver_started || receiver.audio_started);
  ASCIICHAT_REQUIRE_EQ(joins, 3);
  ASCIICHAT_REQUIRE_EQ(discovery_media_join(&receiver), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(joins, 3);
  receiver.audio_started = true;
  join_failures = 4;
  ASCIICHAT_REQUIRE_EQ(discovery_media_join(&receiver), ERROR_THREAD);
  ASCIICHAT_REQUIRE(receiver.audio_started);
  ASCIICHAT_REQUIRE_EQ(discovery_media_join(&receiver), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NOT(receiver.audio_started);
  discovery_host_audio_capture_t host = {.started = true};
  atomic_store_bool(&host.running, true);
  joins = 0;
  join_failures = 2;
  discovery_host_audio_join(&host);
  ASCIICHAT_REQUIRE_EQ(joins, 3);
  ASCIICHAT_REQUIRE_EQ(sleeps, 2);
  ASCIICHAT_REQUIRE_NOT(host.started);
  ASCIICHAT_REQUIRE_NOT(atomic_load_bool(&host.running));
}
TEST_CASE("discovery_media/remote_pcm_is_chunked_and_failed_submission_does_not_advance_observations", "[discovery_media]") {
  audio_context_t audio = {0};
  discovery_video_receiver_t receiver = {.audio = &audio};
  size_t count = AUDIO_OPUS_MAX_DECODE_SAMPLES * AUDIO_CHANNELS + 18;
  float *samples = SAFE_MALLOC(count * sizeof(float), float *);
  for (size_t i = 0; i < count; ++i)
    samples[i] = 0.25f;
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = *options_get();
  v->test_audio = true;
  ASCIICHAT_REQUIRE_EQ(options_state_set(v), ASCIICHAT_OK);
  SAFE_FREE(v);
  ASCIICHAT_REQUIRE_EQ(discovery_play_samples(samples, count, &receiver), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(writes, 2);
  ASCIICHAT_REQUIRE_EQ(chunks[0], AUDIO_OPUS_MAX_DECODE_SAMPLES * AUDIO_CHANNELS);
  ASCIICHAT_REQUIRE_EQ(chunks[1], 18);
  ASCIICHAT_REQUIRE_EQ(receiver.observed_audio_samples, count);
  ASCIICHAT_REQUIRE_EQ(receiver.observed_audio_blocks, 1);
  ASCIICHAT_REQUIRE_GT(receiver.observed_audio_ns, 0);
  ASCIICHAT_REQUIRE_EQ(discovery_play_samples(samples, AUDIO_CHANNELS, &receiver), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(receiver.observed_audio_blocks, 2);
  ASCIICHAT_REQUIRE_GT(receiver.observed_audio_gap_ns, 0);
  write_result = ERROR_AUDIO;
  ASCIICHAT_REQUIRE_EQ(discovery_play_samples(samples, AUDIO_CHANNELS, &receiver), ERROR_AUDIO);
  ASCIICHAT_REQUIRE_EQ(receiver.observed_audio_blocks, 2);
  receiver.audio = NULL;
  ASCIICHAT_REQUIRE_EQ(discovery_play_samples(samples, AUDIO_CHANNELS, &receiver), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(writes, 4);
  SAFE_FREE(samples);
}
TEST_CASE("discovery_media/opus_decode_rejects_invalid_frames_and_preserves_the_disabled_audio_contract", "[discovery_media]") {
  discovery_video_receiver_t receiver = {0};
  float samples[OPUS_FRAME_SAMPLES];
  size_t decoded = 99;
  ASCIICHAT_REQUIRE_EQ(discovery_decode_opus(NULL, 0, samples, OPUS_FRAME_SAMPLES, &decoded, &receiver), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(decoded, 0);
  audio_context_t audio = {0};
  receiver.audio = &audio;
  receiver.audio_pipeline = client_audio_pipeline_create(NULL);
  ASCIICHAT_REQUIRE_NOT_NULL(receiver.audio_pipeline);
  uint8_t invalid[3] = {0xff, 0xff, 0xff};
  ASCIICHAT_REQUIRE_EQ(discovery_decode_opus(invalid, sizeof(invalid), samples, OPUS_FRAME_SAMPLES, &decoded, &receiver),
               ERROR_AUDIO);
  ASCIICHAT_REQUIRE_EQ(decoded, 0);
  client_audio_pipeline_destroy(receiver.audio_pipeline);
}
TEST_CASE("discovery_media/error_callback_retains_a_bounded_message_and_requests_exit", "[discovery_media]") {
  discovery_ui_state_t ui = {0};
  ASCIICHAT_REQUIRE_EQ(mutex_init(&ui.mutex, "error-test"), ASCIICHAT_OK);
  on_discovery_error(ERROR_NETWORK, NULL, &ui);
  ASCIICHAT_REQUIRE_STR_EQ(ui.error, "Unknown discovery error");
  ASCIICHAT_REQUIRE_EQ(ui.error_code, ERROR_NETWORK);
  ASCIICHAT_REQUIRE_EQ(exits, 1);
  char large[700];
  memset(large, 'x', sizeof(large) - 1);
  large[sizeof(large) - 1] = 0;
  on_discovery_error(ERROR_AUDIO, large, &ui);
  ASCIICHAT_REQUIRE_EQ(strlen(ui.error), sizeof(ui.error) - 1);
  ASCIICHAT_REQUIRE_EQ(ui.error_code, ERROR_AUDIO);
  mutex_destroy(&ui.mutex);
}
TEST_CASE("discovery_media/state_notifications_accept_all_roles_and_invalid_values", "[discovery_media]") {
  for (int i = DISCOVERY_STATE_INIT; i <= DISCOVERY_STATE_ENDED; ++i)
    on_discovery_state_change((discovery_state_t)i, NULL);
  on_discovery_state_change((discovery_state_t)-1, NULL);
  on_discovery_state_change((discovery_state_t)999, NULL);
  on_session_ready(NULL, NULL);
  on_session_ready("", NULL);
  on_session_ready("river-cloud-tree", NULL);
  splash_clear_discovery_session();
  cancelled = true;
  ASCIICHAT_REQUIRE(discovery_capture_should_exit_adapter(NULL));
  cancelled = false;
  ASCIICHAT_REQUIRE_NOT(discovery_capture_should_exit_adapter(NULL));
  discovery_keyboard_handler(NULL, KEY_NONE, NULL);
}
static void create_session(void) {
  discovery_config_t config = {0};
  g_discovery = discovery_session_create(&config);
  ASCIICHAT_REQUIRE_NOT_NULL(g_discovery);
}
TEST_CASE("discovery_media/role_negotiation_propagates_handoff_and_process_failures_and_honors_cancellation", "[discovery_media]") {
  create_session();
  handoff_result = ERROR_MEMORY;
  ASCIICHAT_REQUIRE_EQ(discovery_run(NULL, NULL, NULL), ERROR_MEMORY);
  ASCIICHAT_REQUIRE_EQ(loops, 0);
  handoff_result = ASCIICHAT_OK;
  cancelled = true;
  ASCIICHAT_REQUIRE_EQ(discovery_run(NULL, NULL, NULL), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(loops, 0);
  cancelled = false;
  process_result = ERROR_NETWORK;
  ASCIICHAT_REQUIRE_EQ(discovery_run(NULL, NULL, NULL), ERROR_NETWORK);
  ASCIICHAT_REQUIRE_EQ(loops, 1);
  process_result = ASCIICHAT_OK;
  activate = true;
  exit_on_process = true;
  ASCIICHAT_REQUIRE_EQ(discovery_run(NULL, NULL, NULL), ASCIICHAT_OK);
}
TEST_CASE("discovery_media/active_roles_require_their_committed_host_or_participant_transport", "[discovery_media]") {
  create_session();
  activate = true;
  g_discovery->is_host = true;
  ASCIICHAT_REQUIRE_EQ(discovery_run(NULL, NULL, NULL), ERROR_INVALID_STATE);
  g_discovery->is_host = false;
  ASCIICHAT_REQUIRE_EQ(discovery_run(NULL, NULL, NULL), ERROR_INVALID_STATE);
  session_host_config_t config = {.max_clients = 1};
  g_discovery->host_ctx = session_host_create(&config);
  ASCIICHAT_REQUIRE_NOT_NULL(g_discovery->host_ctx);
  g_discovery->is_host = true;
  render_result = ERROR_THREAD;
  ASCIICHAT_REQUIRE_EQ(discovery_run(NULL, NULL, NULL), ERROR_THREAD);
  ASCIICHAT_REQUIRE_NOT_NULL(g_discovery->host_ctx);
}
TEST_CASE("discovery_media/entrypoint_cleans_up_the_owned_session_and_preserves_framework_errors", "[discovery_media]") {
  ASCIICHAT_REQUIRE_EQ(discovery_main(), 0);
  ASCIICHAT_REQUIRE_NULL(g_discovery);
  ASCIICHAT_REQUIRE_EQ(stops, 1);
  run_result = ERROR_AUDIO;
  ASCIICHAT_REQUIRE_EQ(discovery_main(), ERROR_AUDIO);
  ASCIICHAT_REQUIRE_NULL(g_discovery);
  ASCIICHAT_REQUIRE_EQ(stops, 2);
}
TEST_CASE("discovery_media/entrypoint_reports_callback_errors_even_when_framework_returns_success", "[discovery_media]") {
  report_error = true;
  ASCIICHAT_REQUIRE_EQ(discovery_main(), ERROR_NETWORK);
  ASCIICHAT_REQUIRE_NULL(g_discovery);
  ASCIICHAT_REQUIRE_EQ(stops, 1);
  ASCIICHAT_REQUIRE_EQ(exits, 1);
}
TEST_CASE("discovery_media/constructor_failure_does_not_enter_the_shared_media_framework", "[discovery_media]") {
  fail_create = true;
  ASCIICHAT_REQUIRE_EQ(discovery_main(), ERROR_MEMORY);
  ASCIICHAT_REQUIRE_EQ(stops, 0);
  ASCIICHAT_REQUIRE_NULL(g_discovery);
}

TEST_CASE("discovery_media/host_audio_startup_failures_leave_session_ownership_with_the_discovery_owner", "[discovery_media]") {
  create_session();
  activate = true;
  g_discovery->is_host = true;
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = *options_get();
  v->audio_enabled = true;
  ASCIICHAT_REQUIRE_EQ(options_state_set(v), ASCIICHAT_OK);
  SAFE_FREE(v);
  session_host_config_t config = {.max_clients = 1};
  for (unsigned stage = 1; stage <= 3; ++stage) {
    g_discovery->host_ctx = session_host_create(&config);
    ASCIICHAT_REQUIRE_NOT_NULL(g_discovery->host_ctx);
    audio_failure = stage;
    ASCIICHAT_REQUIRE_EQ(discovery_run(NULL, NULL, NULL), stage == 3 ? ERROR_THREAD : ERROR_AUDIO);
    ASCIICHAT_REQUIRE_NOT_NULL(g_discovery->host_ctx);
    session_host_destroy(g_discovery->host_ctx);
    g_discovery->host_ctx = NULL;
    CLEAR_ERRNO_ALL();
  }
}
TEST_CASE("discovery_media/host_capture_errors_are_propagated_after_render_cleanup", "[discovery_media]") {
  create_session();
  activate = true;
  g_discovery->is_host = true;
  session_host_config_t config = {.max_clients = 1};
  g_discovery->host_ctx = session_host_create(&config);
  ASCIICHAT_REQUIRE_NOT_NULL(g_discovery->host_ctx);
  render_result = ASCIICHAT_OK;
  session_capture_ctx_t *capture = session_network_capture_create(20);
  ASCIICHAT_REQUIRE_NOT_NULL(capture);
  fail_read = true;
  ASCIICHAT_REQUIRE_EQ(discovery_run(capture, NULL, NULL), ERROR_MEDIA_DECODE);
  session_capture_destroy(capture);
  ASCIICHAT_REQUIRE_NOT_NULL(g_discovery->host_ctx);
}
TEST_CASE("discovery_media/host_delivers_a_real_synthetic_frame_and_preserves_signaling_failure", "[discovery_media]") {
  create_session();
  activate = true;
  end_after_frame = true;
  frame_result = ERROR_NETWORK;
  g_discovery->is_host = true;
  session_host_config_t host_config = {.max_clients = 1};
  g_discovery->host_ctx = session_host_create(&host_config);
  ASCIICHAT_REQUIRE_NOT_NULL(g_discovery->host_ctx);
  session_capture_config_t config = {.type = MEDIA_SOURCE_TEST, .target_fps = 20};
  session_capture_ctx_t *capture = session_mirror_capture_create(&config);
  ASCIICHAT_REQUIRE_NOT_NULL(capture);
  ASCIICHAT_REQUIRE_EQ(discovery_run(capture, NULL, NULL), ERROR_NETWORK);
  ASCIICHAT_REQUIRE_EQ(loops, 2);
  ASCIICHAT_REQUIRE_EQ(session_host_get_client_count(g_discovery->host_ctx), 1);
  ASCIICHAT_REQUIRE_EQ(g_discovery->state, DISCOVERY_STATE_ENDED);
  session_capture_destroy(capture);
}
