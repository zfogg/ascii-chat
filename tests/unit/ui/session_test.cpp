#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/common.h>
#include <ascii-chat/tui/session.h>
#include <ascii-chat/tui/terminal_screen.h>
#include <ascii-chat/session/session_log_buffer.h>
#include <stdio.h>
#include <string.h>

static void setup(void) {
  options_state_init();
  options_t options = *options_get();
  options.detected_mode = MODE_MIRROR;
  ASCIICHAT_REQUIRE_EQ(options_state_set(&options), ASCIICHAT_OK);
}
static void teardown(void) {
  options_state_destroy();
}
typedef struct {
  unsigned calls;
  double seconds;
  const char *text;
} view_fixture_t;

TEST_CASE("session_ui/shared_dividers_have_matching_utf8_and_ascii_widths", "[session_ui]") {
  frame_buffer_t *buffer = frame_buffer_create(2, 20);
  ASCIICHAT_REQUIRE_NOT_NULL(buffer);
  frame_buffer_render_divider(buffer, 5, true, false);
  ASCIICHAT_REQUIRE_STR_EQ(frame_buffer_get_content(buffer), "━━━━━\n");
  frame_buffer_reset(buffer);
  frame_buffer_render_divider(buffer, 5, false, false);
  ASCIICHAT_REQUIRE_STR_EQ(frame_buffer_get_content(buffer), "-----\n");
  frame_buffer_destroy(buffer);
}

TEST_CASE("session_ui/two_media_sessions_present_every_jittered_60fps_frame", "[session_ui]") {
  ui_session_t *sessions[] = {ui_session_create(), ui_session_create()};
  for (unsigned client = 0; client < 2; client++) {
    ASCIICHAT_REQUIRE_NOT_NULL(sessions[client]);
    ui_session_resize(sessions[client], 80, 30);
  }
  for (unsigned frame = 0; frame < 120; frame++) {
    uint64_t now = 1000000000ULL + (uint64_t)frame * 1000000000ULL / 60;
    now = frame % 2 ? now + 700000 : now - 700000;
    for (unsigned client = 0; client < 2; client++) {
      char text[80];
      snprintf(text, sizeof(text), "client %u changing frame %u", client, frame);
      ASCIICHAT_REQUIRE_EQ(ui_session_media(sessions[client], text, strlen(text)), ASCIICHAT_OK);
      const char *presented = ui_session_tick(sessions[client], now);
      ASCIICHAT_REQUIRE_NOT_NULL(presented, "Client %u dropped frame %u", client, frame);
      ASCIICHAT_REQUIRE_NOT_NULL(strstr(presented, text));
      ASCIICHAT_REQUIRE_NULL(ui_session_tick(sessions[client], now + 1), "Idle ticks must not duplicate media frames");
    }
  }
  for (unsigned client = 0; client < 2; client++)
    ui_session_destroy(sessions[client]);
}

TEST_CASE("session_ui/identical_media_frames_are_presented_once_per_submission", "[session_ui]") {
  ui_session_t *session = ui_session_create();
  ASCIICHAT_REQUIRE_NOT_NULL(session);
  ui_session_resize(session, 80, 30);
  const char *text = "identical ASCII output";
  for (unsigned frame = 0; frame < 120; frame++) {
    uint64_t now = 1000000000ULL + (uint64_t)frame * 1000000000ULL / 60;
    ASCIICHAT_REQUIRE_EQ(ui_session_media(session, text, strlen(text)), ASCIICHAT_OK);
    const char *presented = ui_session_tick(session, now);
    ASCIICHAT_REQUIRE_NOT_NULL(presented, "Dropped identical frame %u", frame);
    ASCIICHAT_REQUIRE_NOT_NULL(strstr(presented, text));
    ASCIICHAT_REQUIRE_NULL(ui_session_tick(session, now + 1), "Idle tick duplicated frame %u", frame);
  }
  ui_session_destroy(session);
}

static void draw(frame_buffer_t *output, terminal_size_t size, const animation_sample_t *animation, void *context) {
  (void)size;
  view_fixture_t *fixture = static_cast<view_fixture_t *>(context);
  ++fixture->calls;
  fixture->seconds = animation->seconds;
  frame_buffer_append(output, fixture->text, strlen(fixture->text));
}

TEST_CASE("session_ui/shared_priority_order", "[session_ui]") {
  ASCIICHAT_REQUIRE_EQ(ui_session_select_screen(0), -1);
  uint32_t visible = 0;
  for (int screen = 0; screen < UI_SCREEN_COUNT; ++screen) {
    visible |= 1u << screen;
    ASCIICHAT_REQUIRE_EQ(ui_session_select_screen(visible), screen);
  }
}

TEST_CASE("session_ui/native_view_resize_and_paused_recovery", "[session_ui]") {
  ui_session_t *session = ui_session_create();
  ASCIICHAT_REQUIRE_NOT_NULL(session);
  view_fixture_t fixture = {.text = "retained media"};
  ui_session_view_t view = {
      .screen = UI_SCREEN_MEDIA, .minimum = {.cols = 20, .rows = 10}, .draw = draw, .context = &fixture};
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(ui_session_render(session, &view, 1), fixture.text));
  ASCIICHAT_REQUIRE_NULL(ui_session_render(session, &view, 2));
  ui_session_resize(session, 10, 5);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(ui_session_render(session, &view, 3), "Terminal"));
  ASCIICHAT_REQUIRE(ui_session_is_covered(session));
  ASCIICHAT_REQUIRE_EQ(fixture.calls, 1);
  ASCIICHAT_REQUIRE_NULL(ui_session_render(session, &view, 4));
  ui_session_resize(session, 80, 24);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(ui_session_render(session, &view, 5), fixture.text));
  ASCIICHAT_REQUIRE_NOT(ui_session_is_covered(session));
  ASCIICHAT_REQUIRE_EQ(fixture.calls, 2);
  view.dirty = true;
  const char *frame = ui_session_render(session, &view, 6);
  ASCIICHAT_REQUIRE_EQ(ui_session_frame_length(session), strlen(frame));
  ui_session_destroy(session);
}

TEST_CASE("session_ui/covered_splash_clock_pauses_even_without_host_ticks", "[session_ui]") {
  ui_session_t *session = ui_session_create();
  view_fixture_t fixture = {.text = "splash"};
  ui_session_view_t view = {
      .screen = UI_SCREEN_SPLASH, .minimum = {.cols = 20, .rows = 10}, .draw = draw, .context = &fixture};
  ui_session_render(session, &view, 1000000000ULL);
  ui_session_render(session, &view, 2000000000ULL);
  ASCIICHAT_REQUIRE_FLOAT_EQ(fixture.seconds, 1, 0.001);
  view.screen = UI_SCREEN_PROMPT;
  ui_session_render(session, &view, 2000000000ULL);
  view.screen = UI_SCREEN_SPLASH;
  ui_session_render(session, &view, 9000000000ULL);
  ASCIICHAT_REQUIRE_FLOAT_EQ(fixture.seconds, 1, 0.001);
  ui_session_render(session, &view, 10000000000ULL);
  ASCIICHAT_REQUIRE_FLOAT_EQ(fixture.seconds, 2, 0.001);
  ui_session_destroy(session);
}

TEST_CASE("session_ui/progress_refresh_is_throttled_but_dirty_snapshots_are_immediate", "[session_ui]") {
  ui_session_t *session = ui_session_create();
  view_fixture_t fixture = {.text = "progress"};
  ui_session_view_t view = {.screen = UI_SCREEN_RENDER_PROGRESS,
                            .minimum = {.cols = 1, .rows = 1},
                            .refresh_ns = 125000000,
                            .draw = draw,
                            .context = &fixture};
  ASCIICHAT_REQUIRE_NOT_NULL(ui_session_render(session, &view, 1));
  ASCIICHAT_REQUIRE_NULL(ui_session_render(session, &view, 100000001));
  ASCIICHAT_REQUIRE_NOT_NULL(ui_session_render(session, &view, 125000001));
  view.dirty = true;
  ASCIICHAT_REQUIRE_NOT_NULL(ui_session_render(session, &view, 125000002));
  ASCIICHAT_REQUIRE_EQ(fixture.calls, 3);
  ui_session_destroy(session);
}

TEST_CASE("session_ui/log_history_returns_latest_entries_after_wrap", "[session_ui]") {
  session_log_buffer_t *logs = session_log_buffer_create();
  ASCIICHAT_REQUIRE_NOT_NULL(logs);
  for (unsigned i = 0; i < SESSION_LOG_BUFFER_SIZE + 10; ++i)
    session_log_buffer_append(logs, "older");
  session_log_buffer_append(logs, "last but one");
  session_log_buffer_append(logs, "latest");
  session_log_entry_t entries[2];
  ASCIICHAT_REQUIRE_EQ(session_log_buffer_get_recent(logs, entries, 2), 2);
  ASCIICHAT_REQUIRE_STR_EQ(entries[0].message, "last but one");
  ASCIICHAT_REQUIRE_STR_EQ(entries[1].message, "latest");
  ASCIICHAT_REQUIRE_EQ(entries[0].sequence + 1, entries[1].sequence);
  session_log_buffer_clear(logs);
  ASCIICHAT_REQUIRE_EQ(session_log_buffer_get_recent(logs, entries, 2), 0);
  session_log_buffer_destroy(logs);
}

TEST_CASE("session_ui/media_toggle_action_and_shared_help", "[session_ui]") {
  double volume = 1.0;
  ASCIICHAT_REQUIRE_EQ(ui_session_key((keyboard_key_t)'[', &volume), UI_ACTION_TOGGLE_MEDIA);
  ASCIICHAT_REQUIRE_EQ(ui_session_key((keyboard_key_t)'q', &volume), UI_ACTION_QUIT);
  ASCIICHAT_REQUIRE_EQ(ui_session_key((keyboard_key_t)'Q', &volume), UI_ACTION_FORCE_QUIT);
  ASCIICHAT_REQUIRE_EQ(ui_session_key(KEY_ESCAPE, &volume), UI_ACTION_CLOSE);
  ASCIICHAT_REQUIRE_EQ(ui_session_key(KEY_SHIFT_ESCAPE, &volume), UI_ACTION_FORCE_QUIT);
  terminal_size_t minimum;
  char *help = ui_session_help_render((terminal_size_t){.rows = 60, .cols = 160}, true, false, &minimum);
  ASCIICHAT_REQUIRE_NOT_NULL(help);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(help, "Toggle Media Playback"));
#if defined(ASCIICHAT_DIAGNOSTIC_UI)
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(help, "Error stacks diagnostic TUI"));
#else
  ASCIICHAT_REQUIRE_NULL(strstr(help, "Error stacks diagnostic TUI"));
#endif
  SAFE_FREE(help);
}

TEST_CASE("session_ui/embedded_log_help_uses_the_session_viewport", "[session_ui]") {
  terminal_screen_logs_reset();
  ui_session_t *session = ui_session_create();
  ASCIICHAT_REQUIRE_NOT_NULL(session);
  ui_session_resize(session, 100, 40);
  ASCIICHAT_REQUIRE_EQ(ui_session_input(session, ']'), UI_ACTION_LOGS);
  ASCIICHAT_REQUIRE_EQ(ui_session_input(session, KEY_QUESTION), UI_ACTION_LOGS);
  const char *frame = ui_session_tick(session, NS_PER_SEC_INT);
  ASCIICHAT_REQUIRE_NOT_NULL(frame);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(frame, "Log Viewer Keyboard Controls"));
  ASCIICHAT_REQUIRE_EQ(ui_session_input(session, KEY_ESCAPE), UI_ACTION_LOGS);
  frame = ui_session_tick(session, 2 * NS_PER_SEC_INT);
  ASCIICHAT_REQUIRE_NOT_NULL(frame);
  ASCIICHAT_REQUIRE_NULL(strstr(frame, "Log Viewer Keyboard Controls"));
  ui_session_destroy(session);
  terminal_screen_logs_reset();
}
