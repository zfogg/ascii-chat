#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include "common/session/capture.h"
#include "common/session/display.h"
#include "common/session/host.h"
#include "common/session/audio.h"
#include "common/session/participant.h"
#include "common/session/pipeline.h"
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/util/endian.h>
#include <string.h>

static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  errno_suppress(true);
  ASCIICHAT_REQUIRE_EQ(options_state_init(), ASCIICHAT_OK);
  options_t *v = SAFE_MALLOC(sizeof(*v), options_t *);
  *v = options_t_new();
  v->test_pattern = true;
  v->audio_enabled = false;
  v->width = 40;
  v->height = 12;
  ASCIICHAT_REQUIRE_EQ(options_state_set(v), ASCIICHAT_OK);
  SAFE_FREE(v);
}
static void teardown(void) {
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
#define INVALID(call)                                                                                                  \
  do {                                                                                                                 \
    ASCIICHAT_REQUIRE_EQ((call), ERROR_INVALID_PARAM);                                                                         \
    CLEAR_ERRNO_ALL();                                                                                                 \
  } while (0)
TEST_CASE("session_api_boundaries/capture_null_queries_and_rejected_transmission_do_not_take_ownership", "[session_api_boundaries]") {
  ASCIICHAT_REQUIRE_NULL(session_mirror_capture_create(NULL));
  CLEAR_ERRNO_ALL();
  ASCIICHAT_REQUIRE_NULL(session_capture_read_frame(NULL));
  CLEAR_ERRNO_ALL();
  ASCIICHAT_REQUIRE(session_capture_at_end(NULL));
  ASCIICHAT_REQUIRE_NOT(session_capture_is_valid(NULL));
  ASCIICHAT_REQUIRE_EQ(session_capture_get_current_fps(NULL), 0);
  ASCIICHAT_REQUIRE_EQ(session_capture_get_target_fps(NULL), 0);
  ASCIICHAT_REQUIRE_NOT(session_capture_has_audio(NULL));
  ASCIICHAT_REQUIRE_NOT(session_capture_using_file_audio(NULL));
  ASCIICHAT_REQUIRE_NULL(session_capture_get_audio_context(NULL));
  ASCIICHAT_REQUIRE_NULL(session_capture_get_media_source(NULL));
  ASCIICHAT_REQUIRE_EQ(session_capture_source_epoch(NULL), 0);
  ASCIICHAT_REQUIRE_EQ(session_capture_frame_serial(NULL), 0);
  ASCIICHAT_REQUIRE_NOT(session_capture_media_active(NULL));
  ASCIICHAT_REQUIRE_NOT(session_capture_cancel_open(NULL));
  float sample = 0.25f;
  ASCIICHAT_REQUIRE_EQ(session_capture_read_audio(NULL, &sample, 1), 0);
  ASCIICHAT_REQUIRE_FLOAT_EQ(sample, 0.25, 1e-6);
  session_capture_destroy(NULL);
  session_capture_sleep_for_fps(NULL);
  session_capture_skip_media(NULL, 1);
  session_capture_toggle_media(NULL);
  session_capture_redraw_media(NULL);
  session_capture_set_audio_context(NULL, NULL);
  session_capture_set_audio_updater(NULL, NULL, NULL);
  session_capture_lock_source(NULL);
  session_capture_unlock_source(NULL);
  session_capture_complete_open(NULL, 1);
  image_t *image = image_new(2, 2), *workspace = NULL;
  ASCIICHAT_REQUIRE_NOT_NULL(image);
  memset(image->pixels, 13, 12);
  INVALID(session_capture_process_for_transmission_into(NULL, image, &workspace));
  ASCIICHAT_REQUIRE_NULL(workspace);
  ASCIICHAT_REQUIRE_EQ(((uint8_t *)image->pixels)[0], 13);
  image_destroy(image);
  CLEAR_ERRNO_ALL();
}
TEST_CASE("session_api_boundaries/missing_display_is_rejected_and_empty_ascii_does_not_publish_a_first_frame", "[session_api_boundaries]") {
  ASCIICHAT_REQUIRE_NULL(session_display_get_caps(NULL));
  ASCIICHAT_REQUIRE_NULL(session_display_get_palette_chars(NULL));
  ASCIICHAT_REQUIRE_NULL(session_display_get_luminance_palette(NULL));
  ASCIICHAT_REQUIRE_EQ(session_display_get_palette_len(NULL), 0);
  ASCIICHAT_REQUIRE_NOT(session_display_has_tty(NULL));
  ASCIICHAT_REQUIRE_EQ(session_display_get_tty_fd(NULL), -1);
  ASCIICHAT_REQUIRE_NULL(session_display_get_stdin_reader(NULL));
  ASCIICHAT_REQUIRE_EQ(session_display_get_render_fps(NULL), 0);
  ASCIICHAT_REQUIRE_NOT(session_display_has_render_file(NULL));
  ASCIICHAT_REQUIRE_NOT(session_display_has_first_frame(NULL));
  ASCIICHAT_REQUIRE_NOT(session_display_has_audio_playback(NULL));
  session_display_set_stdin_reader(NULL, NULL);
  session_display_set_render_fps(NULL, 20);
  session_display_reset_first_frame(NULL);
  session_display_set_snapshot_actual_duration(NULL, 1.0);
  session_display_destroy(NULL);
  session_display_render_frame(NULL, "x");
  session_display_write_ascii_frame(NULL, "x", true);
  session_display_write_raw(NULL, "x", 1);
  session_display_clear(NULL);
  session_display_cursor_home(NULL);
  CLEAR_ERRNO_ALL();
  INVALID(session_display_init_render_file(NULL, 30));
  INVALID(session_display_encode_frame(NULL, NULL, 0));
  float sample = 0.1f;
  INVALID(session_display_write_audio(NULL, &sample, 1));
  session_display_config_t config = {
      .snapshot_mode = true, .palette_type = PALETTE_STANDARD, .color_mode = TERM_COLOR_NONE};
  session_display_ctx_t *display = session_display_create(&config);
  ASCIICHAT_REQUIRE_NOT_NULL(display);
  session_display_render_frame(display, NULL);
  ASCIICHAT_REQUIRE_EQ(GET_ERRNO(), ERROR_INVALID_PARAM);
  CLEAR_ERRNO_ALL();
  session_display_write_ascii_frame(display, "", true);
  ASCIICHAT_REQUIRE_EQ(GET_ERRNO(), ERROR_INVALID_PARAM);
  CLEAR_ERRNO_ALL();
  ASCIICHAT_REQUIRE_NOT(session_display_has_first_frame(display));
  INVALID(session_display_write_audio(display, NULL, 1));
  INVALID(session_display_write_audio(display, &sample, 0));
  ASCIICHAT_REQUIRE_EQ(session_display_write_audio(display, &sample, 1), ASCIICHAT_OK);
  session_display_clear(display);
  session_display_cursor_home(display);
  session_display_reset(display);
  session_display_destroy(display);
}
TEST_CASE("session_api_boundaries/host_invalid_mutations_preserve_the_registered_memory_participant", "[session_api_boundaries]") {
  ASCIICHAT_REQUIRE_NULL(session_host_create(NULL));
  CLEAR_ERRNO_ALL();
  INVALID(session_host_start(NULL));
  INVALID(session_host_render_video(NULL));
  INVALID(session_host_render_audio(NULL));
  INVALID(session_host_start_render(NULL));
  INVALID(session_host_set_display(NULL, NULL));
  INVALID(session_host_set_render_target(NULL, NULL));
  INVALID(session_host_remove_client(NULL, 1));
  INVALID(session_host_find_client(NULL, 1, NULL));
  INVALID(session_host_inject_frame(NULL, 1, NULL));
  INVALID(session_host_inject_audio(NULL, 1, NULL, 1));
  INVALID(session_host_broadcast_frame(NULL, "x"));
  INVALID(session_host_send_frame(NULL, 1, "x"));
  INVALID(session_host_set_client_transport(NULL, 1, NULL, ACIP_TRANSPORT_BORROWED));
  INVALID(session_host_receive_packet(NULL, 1, PACKET_TYPE_PING, NULL, 0));
  ASCIICHAT_REQUIRE_NOT(session_host_is_running(NULL));
  ASCIICHAT_REQUIRE_EQ(session_host_get_client_count(NULL), 0);
  ASCIICHAT_REQUIRE_EQ(session_host_get_client_ids(NULL, NULL, 0), 0);
  ASCIICHAT_REQUIRE_EQ(session_host_add_memory_participant(NULL), 0);
  ASCIICHAT_REQUIRE_NOT(session_host_client_has_transport(NULL, 1));
  ASCIICHAT_REQUIRE_NULL(session_host_get_client_transport(NULL, 1));
  ASCIICHAT_REQUIRE_EQ(session_host_stop(NULL), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(session_host_destroy(NULL), ASCIICHAT_OK);
  session_host_stop_render(NULL);
  session_host_config_t config = {.port = 0, .max_clients = 2};
  session_host_t *host = session_host_create(&config);
  ASCIICHAT_REQUIRE_NOT_NULL(host);
  uint32_t id = session_host_add_memory_participant(host);
  ASCIICHAT_REQUIRE_NE(id, 0);
  INVALID(session_host_inject_frame(host, id, NULL));
  INVALID(session_host_inject_audio(host, id, NULL, 1));
  float sample = 0.1;
  INVALID(session_host_inject_audio(host, id, &sample, 0));
  INVALID(session_host_find_client(host, id, NULL));
  INVALID(session_host_receive_packet(host, id, PACKET_TYPE_PING, NULL, 1));
  ASCIICHAT_REQUIRE_EQ(session_host_broadcast_frame(host, "x"), ERROR_INVALID_STATE);
  CLEAR_ERRNO_ALL();
  ASCIICHAT_REQUIRE_EQ(session_host_send_frame(host, id, "x"), ERROR_INVALID_STATE);
  CLEAR_ERRNO_ALL();
  ASCIICHAT_REQUIRE_EQ(session_host_get_client_count(host), 1);
  ASCIICHAT_REQUIRE_NOT(session_host_client_has_transport(host, id));
  session_host_client_info_t info;
  ASCIICHAT_REQUIRE_EQ(session_host_find_client(host, id, &info), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(session_host_find_client(host, UINT32_MAX, &info), ERROR_NOT_FOUND);
  CLEAR_ERRNO_ALL();
  ASCIICHAT_REQUIRE_EQ(session_host_destroy(host), ASCIICHAT_OK);
}
TEST_CASE("session_api_boundaries/audio_invalid_sources_and_buffers_do_not_change_a_valid_host_context", "[session_api_boundaries]") {
  INVALID(session_audio_start_capture(NULL));
  INVALID(session_audio_start_playback(NULL));
  INVALID(session_audio_start_duplex(NULL));
  INVALID(session_audio_write_playback(NULL, NULL, 1));
  INVALID(session_audio_add_source(NULL, 1));
  INVALID(session_audio_write_source(NULL, 1, NULL, 1));
  ASCIICHAT_REQUIRE_NOT(session_audio_is_running(NULL));
  ASCIICHAT_REQUIRE_EQ(session_audio_mix_excluding(NULL, 0, NULL, 1), 0);
  ASCIICHAT_REQUIRE_EQ(session_audio_read_captured(NULL, NULL, 1), 0);
  ASCIICHAT_REQUIRE_NULL(session_audio_get_context(NULL));
  session_audio_destroy(NULL);
  session_audio_stop(NULL);
  session_audio_remove_source(NULL, 1);
  session_audio_set_capture_source(NULL, NULL);
  session_audio_ctx_t *audio = session_audio_create(true);
  ASCIICHAT_REQUIRE_NOT_NULL(audio);
  ASCIICHAT_REQUIRE_EQ(session_audio_add_source(audio, 7), ASCIICHAT_OK);
  float output[4] = {9, 9, 9, 9};
  INVALID(session_audio_write_source(audio, 7, NULL, 4));
  INVALID(session_audio_write_playback(audio, NULL, 4));
  ASCIICHAT_REQUIRE_EQ(session_audio_mix_excluding(audio, 7, output, 0), 0);
  ASCIICHAT_REQUIRE_EQ(session_audio_read_captured(audio, NULL, 4), 0);
  ASCIICHAT_REQUIRE_EQ(session_audio_mix_excluding(audio, 7, output, 4), 4);
  ASCIICHAT_REQUIRE_ARRAY_EQ(output, ((float[]){0, 0, 0, 0}), sizeof(output));
  session_audio_destroy(audio);
}
TEST_CASE("session_api_boundaries/disconnected_participants_reject_media_and_settings_without_consuming_connection_outputs", "[session_api_boundaries]") {
  INVALID(session_participant_connect(NULL));
  INVALID(session_participant_start_video(NULL));
  INVALID(session_participant_start_audio(NULL));
  INVALID(session_participant_get_settings(NULL, NULL));
  INVALID(session_participant_request_settings(NULL, NULL));
  INVALID(session_participant_set_transport(NULL, NULL, ACIP_TRANSPORT_BORROWED));
  network_connection_t *owner = NULL;
  INVALID(session_participant_set_connection(NULL, &owner));
  INVALID(session_participant_take_connection(NULL, &owner));
  INVALID(session_participant_receive_settings(NULL, NULL, 0));
  ASCIICHAT_REQUIRE_NULL(owner);
  ASCIICHAT_REQUIRE_EQ(session_participant_get_client_id(NULL), 0);
  ASCIICHAT_REQUIRE_NOT(session_participant_is_connected(NULL));
  ASCIICHAT_REQUIRE_NOT(session_participant_is_video_active(NULL));
  ASCIICHAT_REQUIRE_NOT(session_participant_is_audio_active(NULL));
  ASCIICHAT_REQUIRE_NOT(session_participant_has_transport(NULL));
  ASCIICHAT_REQUIRE_NULL(session_participant_get_transport(NULL));
  session_participant_disconnect(NULL);
  session_participant_stop_audio(NULL);
  session_participant_stop_video(NULL);
  session_participant_destroy(NULL);
  session_participant_config_t config = {.address = "127.0.0.1", .port = 27001};
  session_participant_t *p = session_participant_create(&config);
  ASCIICHAT_REQUIRE_NOT_NULL(p);
  ASCIICHAT_REQUIRE_EQ(session_participant_start_video(p), ERROR_INVALID_STATE);
  CLEAR_ERRNO_ALL();
  ASCIICHAT_REQUIRE_EQ(session_participant_start_audio(p), ERROR_INVALID_STATE);
  CLEAR_ERRNO_ALL();
  session_settings_t settings = {0};
  ASCIICHAT_REQUIRE_EQ(session_participant_request_settings(p, &settings), ERROR_INVALID_STATE);
  CLEAR_ERRNO_ALL();
  INVALID(session_participant_get_settings(p, NULL));
  INVALID(session_participant_request_settings(p, NULL));
  INVALID(session_participant_take_connection(p, NULL));
  ASCIICHAT_REQUIRE_NOT(session_participant_is_connected(p));
  session_participant_destroy(p);
}
