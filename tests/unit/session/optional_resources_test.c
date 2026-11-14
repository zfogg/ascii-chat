#include <criterion/criterion.h>
#include "common/session/capture.h"
#include "common/session/display.h"
#include "common/session/host.h"
#include "common/session/participant.h"
#include "common/session/protocol.h"
#include <ascii-chat/options/rcu.h>

static void setup(void) {
  log_init(NULL, LOG_ERROR, false, false);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
}
static void cleanup(void) {
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(optional_resources, .init = setup, .fini = cleanup);

Test(optional_resources, absent_capture_can_be_queried_and_cleaned_up_after_partial_startup) {
  float samples[2] = {0};
  session_capture_destroy(NULL);
  session_capture_sleep_for_fps(NULL);
  session_capture_set_audio_context(NULL, NULL);
  session_capture_set_audio_updater(NULL, NULL, NULL);
  session_capture_lock_source(NULL);
  session_capture_unlock_source(NULL);
  session_capture_complete_open(NULL, 0);
  session_capture_skip_media(NULL, 1);
  session_capture_toggle_media(NULL);
  session_capture_redraw_media(NULL);
  cr_assert_null(session_capture_read_frame(NULL));
  cr_assert_not(session_capture_is_valid(NULL));
  cr_assert_not(session_capture_media_active(NULL));
  cr_assert_not(session_capture_cancel_open(NULL));
  cr_assert_not(session_capture_has_audio(NULL));
  cr_assert_not(session_capture_using_file_audio(NULL));
  cr_assert_null(session_capture_get_audio_context(NULL));
  cr_assert_null(session_capture_get_media_source(NULL));
  cr_assert_eq(session_capture_read_audio(NULL, samples, 2), 0);
  cr_assert_eq(session_capture_get_current_fps(NULL), 0);
  cr_assert_eq(session_capture_get_target_fps(NULL), 0);
  cr_assert_eq(session_capture_source_epoch(NULL), 0);
  cr_assert_eq(session_capture_frame_serial(NULL), 0);
  cr_assert_eq(session_capture_open_media(NULL, "fixture.mp4"), ERROR_INVALID_STATE);
}

Test(optional_resources, absent_display_can_be_released_without_writing_or_using_audio) {
  float samples[2] = {0};
  session_display_destroy(NULL);
  session_display_reset(NULL);
  session_display_clear(NULL);
  session_display_cursor_home(NULL);
  session_display_reset_first_frame(NULL);
  session_display_set_stdin_reader(NULL, NULL);
  session_display_set_render_live_timing(NULL);
  session_display_set_render_fps(NULL, 30);
  session_display_set_snapshot_actual_duration(NULL, 1);
  session_display_begin_drain(NULL, 1, true);
  session_display_write_ascii(NULL, "frame");
  session_display_render_frame(NULL, "frame");
  session_display_write_raw(NULL, "frame", 5);
  cr_assert_not(session_display_has_first_frame(NULL));
  cr_assert_not(session_display_has_tty(NULL));
  cr_assert_not(session_display_has_render_file(NULL));
  cr_assert_not(session_display_has_audio_playback(NULL));
  cr_assert_null(session_display_get_caps(NULL));
  cr_assert_null(session_display_get_stdin_reader(NULL));
  cr_assert_eq(session_display_write_audio(NULL, samples, 2), ERROR_INVALID_PARAM);
}

Test(optional_resources, absent_participant_rejects_mutations_without_claiming_connectivity) {
  session_participant_destroy(NULL);
  session_participant_disconnect(NULL);
  session_participant_stop_video(NULL);
  session_participant_stop_audio(NULL);
  cr_assert_not(session_participant_is_connected(NULL));
  cr_assert_not(session_participant_is_video_active(NULL));
  cr_assert_not(session_participant_is_audio_active(NULL));
  cr_assert_not(session_participant_has_transport(NULL));
  cr_assert_null(session_participant_connection(NULL));
  cr_assert_null(session_participant_get_transport(NULL));
  cr_assert_eq(session_participant_get_client_id(NULL), 0);
  cr_assert_eq(session_participant_connect(NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(session_participant_start_video(NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(session_participant_start_audio(NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(session_participant_receive_settings(NULL, NULL, 0), ERROR_INVALID_PARAM);
}

Test(optional_resources, invalid_protocol_state_is_rejected_before_transport_or_callback_use) {
  terminal_size_t size = {0};
  uint64_t revision = 0;
  cr_assert_eq(session_protocol_dispatch(NULL, PACKET_TYPE_PING, NULL, 0), ERROR_INVALID_PARAM);
  session_protocol_t protocol = {0};
  cr_assert_eq(session_protocol_dispatch(&protocol, PACKET_TYPE_PING, NULL, 1), ERROR_INVALID_PARAM);
  cr_assert_eq(session_protocol_update_settings(NULL, &revision, NULL, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(session_protocol_update_settings(&size, NULL, NULL, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(session_protocol_update_settings(&size, &revision, NULL, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(protocol.packets_received, 0);
}
