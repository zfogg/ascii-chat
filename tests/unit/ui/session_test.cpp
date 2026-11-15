#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
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
  REQUIRE((options_state_set(&options)) == (ASCIICHAT_OK));
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
  REQUIRE((buffer) != nullptr);
  frame_buffer_render_divider(buffer, 5, true, false);
  REQUIRE(std::strcmp((frame_buffer_get_content(buffer)), ("━━━━━\n")) == 0);
  frame_buffer_reset(buffer);
  frame_buffer_render_divider(buffer, 5, false, false);
  REQUIRE(std::strcmp((frame_buffer_get_content(buffer)), ("-----\n")) == 0);
  frame_buffer_destroy(buffer);
}

TEST_CASE("session_ui/two_media_sessions_present_every_jittered_60fps_frame", "[session_ui]") {
  ui_session_t *sessions[] = {ui_session_create(), ui_session_create()};
  for (unsigned client = 0; client < 2; client++) {
    REQUIRE((sessions[client]) != nullptr);
    ui_session_resize(sessions[client], 80, 30);
  }
  for (unsigned frame = 0; frame < 120; frame++) {
    uint64_t now = 1000000000ULL + (uint64_t)frame * 1000000000ULL / 60;
    now = frame % 2 ? now + 700000 : now - 700000;
    for (unsigned client = 0; client < 2; client++) {
      char text[80];
      snprintf(text, sizeof(text), "client %u changing frame %u", client, frame);
      REQUIRE((ui_session_media(sessions[client], text, strlen(text))) == (ASCIICHAT_OK));
      const char *presented = ui_session_tick(sessions[client], now);
      REQUIRE((presented) != nullptr);
      REQUIRE((strstr(presented, text)) != nullptr);
      REQUIRE((ui_session_tick(sessions[client], now + 1)) == nullptr);
    }
  }
  for (unsigned client = 0; client < 2; client++)
    ui_session_destroy(sessions[client]);
}

TEST_CASE("session_ui/identical_media_frames_are_presented_once_per_submission", "[session_ui]") {
  ui_session_t *session = ui_session_create();
  REQUIRE((session) != nullptr);
  ui_session_resize(session, 80, 30);
  const char *text = "identical ASCII output";
  for (unsigned frame = 0; frame < 120; frame++) {
    uint64_t now = 1000000000ULL + (uint64_t)frame * 1000000000ULL / 60;
    REQUIRE((ui_session_media(session, text, strlen(text))) == (ASCIICHAT_OK));
    const char *presented = ui_session_tick(session, now);
    REQUIRE((presented) != nullptr);
    REQUIRE((strstr(presented, text)) != nullptr);
    REQUIRE((ui_session_tick(session, now + 1)) == nullptr);
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
  REQUIRE((ui_session_select_screen(0)) == (-1));
  uint32_t visible = 0;
  for (int screen = 0; screen < UI_SCREEN_COUNT; ++screen) {
    visible |= 1u << screen;
    REQUIRE((ui_session_select_screen(visible)) == (screen));
  }
}

TEST_CASE("session_ui/native_view_resize_and_paused_recovery", "[session_ui]") {
  ui_session_t *session = ui_session_create();
  REQUIRE((session) != nullptr);
  view_fixture_t fixture = {.text = "retained media"};
  ui_session_view_t view = {.screen = UI_SCREEN_MEDIA, .minimum = {.cols = 20, .rows = 10}, .draw = draw, .context = &fixture};
  REQUIRE((strstr(ui_session_render(session, &view, 1), fixture.text)) != nullptr);
  REQUIRE((ui_session_render(session, &view, 2)) == nullptr);
  ui_session_resize(session, 10, 5);
  REQUIRE((strstr(ui_session_render(session, &view, 3), "Terminal")) != nullptr);
  REQUIRE(ui_session_is_covered(session));
  REQUIRE((fixture.calls) == (1));
  REQUIRE((ui_session_render(session, &view, 4)) == nullptr);
  ui_session_resize(session, 80, 24);
  REQUIRE((strstr(ui_session_render(session, &view, 5), fixture.text)) != nullptr);
  REQUIRE(!(ui_session_is_covered(session)));
  REQUIRE((fixture.calls) == (2));
  view.dirty = true;
  const char *frame = ui_session_render(session, &view, 6);
  REQUIRE((ui_session_frame_length(session)) == (strlen(frame)));
  ui_session_destroy(session);
}

TEST_CASE("session_ui/covered_splash_clock_pauses_even_without_host_ticks", "[session_ui]") {
  ui_session_t *session = ui_session_create();
  view_fixture_t fixture = {.text = "splash"};
  ui_session_view_t view = {.screen = UI_SCREEN_SPLASH, .minimum = {.cols = 20, .rows = 10}, .draw = draw, .context = &fixture};
  ui_session_render(session, &view, 1000000000ULL);
  ui_session_render(session, &view, 2000000000ULL);
  REQUIRE((fixture.seconds) == Catch::Approx(1));
  view.screen = UI_SCREEN_PROMPT;
  ui_session_render(session, &view, 2000000000ULL);
  view.screen = UI_SCREEN_SPLASH;
  ui_session_render(session, &view, 9000000000ULL);
  REQUIRE((fixture.seconds) == Catch::Approx(1));
  ui_session_render(session, &view, 10000000000ULL);
  REQUIRE((fixture.seconds) == Catch::Approx(2));
  ui_session_destroy(session);
}

TEST_CASE("session_ui/progress_refresh_is_throttled_but_dirty_snapshots_are_immediate", "[session_ui]") {
  ui_session_t *session = ui_session_create();
  view_fixture_t fixture = {.text = "progress"};
  ui_session_view_t view = {.screen = UI_SCREEN_RENDER_PROGRESS, .minimum = {.cols = 1, .rows = 1}, .refresh_ns = 125000000, .draw = draw, .context = &fixture};
  REQUIRE((ui_session_render(session, &view, 1)) != nullptr);
  REQUIRE((ui_session_render(session, &view, 100000001)) == nullptr);
  REQUIRE((ui_session_render(session, &view, 125000001)) != nullptr);
  view.dirty = true;
  REQUIRE((ui_session_render(session, &view, 125000002)) != nullptr);
  REQUIRE((fixture.calls) == (3));
  ui_session_destroy(session);
}

TEST_CASE("session_ui/log_history_returns_latest_entries_after_wrap", "[session_ui]") {
  session_log_buffer_t *logs = session_log_buffer_create();
  REQUIRE((logs) != nullptr);
  for (unsigned i = 0; i < SESSION_LOG_BUFFER_SIZE + 10; ++i)
    session_log_buffer_append(logs, "older");
  session_log_buffer_append(logs, "last but one");
  session_log_buffer_append(logs, "latest");
  session_log_entry_t entries[2];
  REQUIRE((session_log_buffer_get_recent(logs, entries, 2)) == (2));
  REQUIRE(std::strcmp((entries[0].message), ("last but one")) == 0);
  REQUIRE(std::strcmp((entries[1].message), ("latest")) == 0);
  REQUIRE((entries[0].sequence + 1) == (entries[1].sequence));
  session_log_buffer_clear(logs);
  REQUIRE((session_log_buffer_get_recent(logs, entries, 2)) == (0));
  session_log_buffer_destroy(logs);
}

TEST_CASE("session_ui/media_toggle_action_and_shared_help", "[session_ui]") {
  double volume = 1.0;
  REQUIRE((ui_session_key((keyboard_key_t)'[', &volume)) == (UI_ACTION_TOGGLE_MEDIA));
  REQUIRE((ui_session_key((keyboard_key_t)'q', &volume)) == (UI_ACTION_QUIT));
  REQUIRE((ui_session_key((keyboard_key_t)'Q', &volume)) == (UI_ACTION_FORCE_QUIT));
  REQUIRE((ui_session_key(KEY_ESCAPE, &volume)) == (UI_ACTION_CLOSE));
  REQUIRE((ui_session_key(KEY_SHIFT_ESCAPE, &volume)) == (UI_ACTION_FORCE_QUIT));
  terminal_size_t minimum;
  char *help = ui_session_help_render((terminal_size_t){.rows = 60, .cols = 160}, true, false, &minimum);
  REQUIRE((help) != nullptr);
  REQUIRE((strstr(help, "Toggle Media Playback")) != nullptr);
#if defined(ASCIICHAT_DIAGNOSTIC_UI)
  REQUIRE((strstr(help, "Error stacks diagnostic TUI")) != nullptr);
#else
  REQUIRE((strstr(help, "Error stacks diagnostic TUI")) == nullptr);
#endif
  SAFE_FREE(help);
}

TEST_CASE("session_ui/embedded_log_help_uses_the_session_viewport", "[session_ui]") {
  terminal_screen_logs_reset();
  ui_session_t *session = ui_session_create();
  REQUIRE((session) != nullptr);
  ui_session_resize(session, 100, 40);
  REQUIRE((ui_session_input(session, static_cast<keyboard_key_t>(']'))) == (UI_ACTION_LOGS));
  REQUIRE((ui_session_input(session, KEY_QUESTION)) == (UI_ACTION_LOGS));
  const char *frame = ui_session_tick(session, NS_PER_SEC_INT);
  REQUIRE((frame) != nullptr);
  REQUIRE((strstr(frame, "Log Viewer Keyboard Controls")) != nullptr);
  REQUIRE((ui_session_input(session, KEY_ESCAPE)) == (UI_ACTION_LOGS));
  frame = ui_session_tick(session, 2 * NS_PER_SEC_INT);
  REQUIRE((frame) != nullptr);
  REQUIRE((strstr(frame, "Log Viewer Keyboard Controls")) == nullptr);
  ui_session_destroy(session);
  terminal_screen_logs_reset();
}
