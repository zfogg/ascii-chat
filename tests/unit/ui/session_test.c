#include <criterion/criterion.h>
#include <ascii-chat/ui/session.h>
#include <string.h>

typedef struct {
  unsigned calls;
  double seconds;
  const char *text;
} view_fixture_t;

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
