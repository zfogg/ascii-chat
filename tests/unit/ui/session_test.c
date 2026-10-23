#include <criterion/criterion.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/common.h>
#include <ascii-chat/tui/session.h>
#include <ascii-chat/session/session_log_buffer.h>
#include <stdio.h>
#include <string.h>

static void setup(void) {
  options_state_init();
  options_t options = *options_get();
  options.detected_mode = MODE_MIRROR;
  cr_assert_eq(options_state_set(&options), ASCIICHAT_OK);
}
static void teardown(void) {
  options_state_destroy();
}
TestSuite(session_ui, .init = setup, .fini = teardown);

typedef struct {
  unsigned calls;
  double seconds;
  const char *text;
} view_fixture_t;

Test(session_ui, shared_dividers_have_matching_utf8_and_ascii_widths) {
  frame_buffer_t *buffer = frame_buffer_create(2, 20);
  cr_assert_not_null(buffer);
  frame_buffer_render_divider(buffer, 5, true, false);
  cr_assert_str_eq(frame_buffer_get_content(buffer), "━━━━━\n");
  frame_buffer_reset(buffer);
  frame_buffer_render_divider(buffer, 5, false, false);
  cr_assert_str_eq(frame_buffer_get_content(buffer), "-----\n");
  frame_buffer_destroy(buffer);
}

Test(session_ui, two_media_sessions_present_every_jittered_60fps_frame) {
  ui_session_t *sessions[] = {ui_session_create(), ui_session_create()};
  for (unsigned client = 0; client < 2; client++) {
    cr_assert_not_null(sessions[client]);
    ui_session_resize(sessions[client], 80, 30);
  }
  for (unsigned frame = 0; frame < 120; frame++) {
    uint64_t now = 1000000000ULL + (uint64_t)frame * 1000000000ULL / 60;
    now = frame % 2 ? now + 700000 : now - 700000;
    for (unsigned client = 0; client < 2; client++) {
      char text[80];
      snprintf(text, sizeof(text), "client %u changing frame %u", client, frame);
      cr_assert_eq(ui_session_media(sessions[client], text, strlen(text)), ASCIICHAT_OK);
      const char *presented = ui_session_tick(sessions[client], now);
      cr_assert_not_null(presented, "Client %u dropped frame %u", client, frame);
      cr_assert_not_null(strstr(presented, text));
      cr_assert_null(ui_session_tick(sessions[client], now + 1), "Idle ticks must not duplicate media frames");
    }
  }
  for (unsigned client = 0; client < 2; client++)
    ui_session_destroy(sessions[client]);
}

Test(session_ui, identical_media_frames_are_presented_once_per_submission) {
  ui_session_t *session = ui_session_create();
  cr_assert_not_null(session);
  ui_session_resize(session, 80, 30);
  const char *text = "identical ASCII output";
  for (unsigned frame = 0; frame < 120; frame++) {
    uint64_t now = 1000000000ULL + (uint64_t)frame * 1000000000ULL / 60;
    cr_assert_eq(ui_session_media(session, text, strlen(text)), ASCIICHAT_OK);
    const char *presented = ui_session_tick(session, now);
    cr_assert_not_null(presented, "Dropped identical frame %u", frame);
    cr_assert_not_null(strstr(presented, text));
    cr_assert_null(ui_session_tick(session, now + 1), "Idle tick duplicated frame %u", frame);
  }
  ui_session_destroy(session);
}

static void draw(frame_buffer_t *output, terminal_size_t size, const animation_sample_t *animation, void *context) {
  (void)size;
  view_fixture_t *fixture = context;
  ++fixture->calls;
  fixture->seconds = animation->seconds;
  frame_buffer_append(output, fixture->text, strlen(fixture->text));
}

Test(session_ui, shared_priority_order) {
  cr_assert_eq(ui_session_select_screen(0), -1);
  uint32_t visible = 0;
  for (int screen = 0; screen < UI_SCREEN_COUNT; ++screen) {
    visible |= 1u << screen;
    cr_assert_eq(ui_session_select_screen(visible), screen);
  }
}

Test(session_ui, native_view_resize_and_paused_recovery) {
  ui_session_t *session = ui_session_create();
  cr_assert_not_null(session);
  view_fixture_t fixture = {.text = "retained media"};
  ui_session_view_t view = {
      .screen = UI_SCREEN_MEDIA, .minimum = {.cols = 20, .rows = 10}, .draw = draw, .context = &fixture};
  cr_assert_not_null(strstr(ui_session_render(session, &view, 1), fixture.text));
  cr_assert_null(ui_session_render(session, &view, 2));
  ui_session_resize(session, 10, 5);
  cr_assert_not_null(strstr(ui_session_render(session, &view, 3), "Terminal"));
  cr_assert(ui_session_is_covered(session));
  cr_assert_eq(fixture.calls, 1);
  cr_assert_null(ui_session_render(session, &view, 4));
  ui_session_resize(session, 80, 24);
  cr_assert_not_null(strstr(ui_session_render(session, &view, 5), fixture.text));
  cr_assert_not(ui_session_is_covered(session));
  cr_assert_eq(fixture.calls, 2);
  view.dirty = true;
  const char *frame = ui_session_render(session, &view, 6);
  cr_assert_eq(ui_session_frame_length(session), strlen(frame));
  ui_session_destroy(session);
}

Test(session_ui, covered_splash_clock_pauses_even_without_host_ticks) {
  ui_session_t *session = ui_session_create();
  view_fixture_t fixture = {.text = "splash"};
  ui_session_view_t view = {
      .screen = UI_SCREEN_SPLASH, .minimum = {.cols = 20, .rows = 10}, .draw = draw, .context = &fixture};
  ui_session_render(session, &view, 1000000000ULL);
  ui_session_render(session, &view, 2000000000ULL);
  cr_assert_float_eq(fixture.seconds, 1, 0.001);
  view.screen = UI_SCREEN_PROMPT;
  ui_session_render(session, &view, 2000000000ULL);
  view.screen = UI_SCREEN_SPLASH;
  ui_session_render(session, &view, 9000000000ULL);
  cr_assert_float_eq(fixture.seconds, 1, 0.001);
  ui_session_render(session, &view, 10000000000ULL);
  cr_assert_float_eq(fixture.seconds, 2, 0.001);
  ui_session_destroy(session);
}

Test(session_ui, progress_refresh_is_throttled_but_dirty_snapshots_are_immediate) {
  ui_session_t *session = ui_session_create();
  view_fixture_t fixture = {.text = "progress"};
  ui_session_view_t view = {.screen = UI_SCREEN_RENDER_PROGRESS,
                            .minimum = {.cols = 1, .rows = 1},
                            .refresh_ns = 125000000,
                            .draw = draw,
                            .context = &fixture};
  cr_assert_not_null(ui_session_render(session, &view, 1));
  cr_assert_null(ui_session_render(session, &view, 100000001));
  cr_assert_not_null(ui_session_render(session, &view, 125000001));
  view.dirty = true;
  cr_assert_not_null(ui_session_render(session, &view, 125000002));
  cr_assert_eq(fixture.calls, 3);
  ui_session_destroy(session);
}

Test(session_ui, log_history_returns_latest_entries_after_wrap) {
  session_log_buffer_t *logs = session_log_buffer_create();
  cr_assert_not_null(logs);
  for (unsigned i = 0; i < SESSION_LOG_BUFFER_SIZE + 10; ++i)
    session_log_buffer_append(logs, "older");
  session_log_buffer_append(logs, "last but one");
  session_log_buffer_append(logs, "latest");
  session_log_entry_t entries[2];
  cr_assert_eq(session_log_buffer_get_recent(logs, entries, 2), 2);
  cr_assert_str_eq(entries[0].message, "last but one");
  cr_assert_str_eq(entries[1].message, "latest");
  cr_assert_eq(entries[0].sequence + 1, entries[1].sequence);
  session_log_buffer_clear(logs);
  cr_assert_eq(session_log_buffer_get_recent(logs, entries, 2), 0);
  session_log_buffer_destroy(logs);
}

Test(session_ui, media_toggle_action_and_shared_help) {
  double volume = 1.0;
  cr_assert_eq(ui_session_key((keyboard_key_t)'[', &volume), UI_ACTION_TOGGLE_MEDIA);
  terminal_size_t minimum;
  char *help = ui_session_help_render((terminal_size_t){.rows = 60, .cols = 160}, true, false, &minimum);
  cr_assert_not_null(help);
  cr_assert_not_null(strstr(help, "Toggle Media Playback"));
#if defined(ASCIICHAT_DIAGNOSTIC_UI)
  cr_assert_not_null(strstr(help, "Error stacks diagnostic TUI"));
#else
  cr_assert_null(strstr(help, "Error stacks diagnostic TUI"));
#endif
  SAFE_FREE(help);
}
