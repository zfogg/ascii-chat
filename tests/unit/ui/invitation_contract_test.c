#include <criterion/criterion.h>
#include <ascii-chat/ui/invitation.h>
#include <string.h>

Test(invitation_contract, hosting_joining_and_pending_have_distinct_instructions) {
  frame_buffer_t *buffer = frame_buffer_create(24, 80);
  cr_assert_not_null(buffer);
  terminal_size_t size = {.cols = 80, .rows = 24};
  invitation_render(buffer, size, "blue-mountain-tiger", false, 0, false);
  const char *text = frame_buffer_get_content(buffer);
  cr_assert_not_null(strstr(text, "Share this string to connect:"));
  const char *session = strstr(text, "blue-mountain-tiger");
  const char *command = strstr(text, "Run: ascii-chat blue-mountain-tiger");
  cr_assert_not_null(session);
  cr_assert_not_null(command);
  cr_assert_lt(session, command);
  cr_assert_null(strstr(text, "\033[38;2;"));
  frame_buffer_reset(buffer);
  invitation_render(buffer, size, "blue-mountain-tiger", true, 10, true);
  text = frame_buffer_get_content(buffer);
  cr_assert_not_null(strstr(text, "Connecting to session:"));
  cr_assert_not_null(strstr(text, "blue-mountain-tiger"));
  cr_assert_null(strstr(text, "Run:"));
  cr_assert_not_null(strstr(text, "\033[38;2;"));
  for (int empty = 0; empty < 2; ++empty) {
    frame_buffer_reset(buffer);
    invitation_render(buffer, size, empty ? "" : NULL, false, 0, false);
    text = frame_buffer_get_content(buffer);
    cr_assert_not_null(strstr(text, "Creating session..."));
    cr_assert_null(strstr(text, "Run:"));
    cr_assert_null(strstr(text, "blue-mountain-tiger"));
  }
  frame_buffer_destroy(buffer);
}

Test(invitation_contract, short_and_narrow_terminals_preserve_session_within_bounds) {
  frame_buffer_t *buffer = frame_buffer_create(0, 0);
  cr_assert_not_null(buffer);
  const terminal_size_t sizes[] = {
      {.cols = 80, .rows = 2}, {.cols = 20, .rows = 10}, {.cols = 2, .rows = 2}, {.cols = 52, .rows = 6}};
  for (size_t i = 0; i < sizeof(sizes) / sizeof(*sizes); ++i) {
    frame_buffer_reset(buffer);
    invitation_render(buffer, sizes[i], "blue-tiger", false, 0, false);
    const char *text = frame_buffer_get_content(buffer);
    size_t rows = 0, cells = 0;
    for (const char *cursor = text; *cursor;) {
      if (*cursor == '\033') {
        cr_assert_eq(cursor[1], '[');
        cr_assert(cursor[2] == 'K' || cursor[2] == 'J');
        cursor += 3;
      } else if (*cursor == '\n') {
        cr_assert_leq(cells, (size_t)sizes[i].cols - 1);
        ++rows;
        cells = 0;
        ++cursor;
      } else if (*cursor == '\r') {
        ++cursor;
      } else {
        ++cells;
        ++cursor;
      }
    }
    cr_assert_leq(rows, (size_t)sizes[i].rows - 1);
    cr_assert_geq(strlen(text), 3);
    cr_assert_str_eq(text + strlen(text) - 3, "\033[J");
    if (i == 0) {
      cr_assert_not_null(strstr(text, "blue-tiger"));
      cr_assert_null(strstr(text, "Share this string"));
    }
  }
  frame_buffer_reset(buffer);
  frame_buffer_printf(buffer, "%s", "sentinel");
  invitation_render(buffer, (terminal_size_t){.cols = 1, .rows = 24}, "blue-tiger", false, 0, false);
  invitation_render(buffer, (terminal_size_t){.cols = 80, .rows = 1}, "blue-tiger", false, 0, false);
  invitation_render(NULL, sizes[0], "blue-tiger", false, 0, false);
  cr_assert_str_eq(frame_buffer_get_content(buffer), "sentinel");
  frame_buffer_destroy(buffer);
}

Test(invitation_contract, compact_logo_is_clipped_centered_and_animated) {
  frame_buffer_t *buffer = frame_buffer_create(0, 0);
  cr_assert_not_null(buffer);
  invitation_render_logo(buffer, 20, 0, false, true);
  cr_assert_str_eq(frame_buffer_get_content(buffer), "     ascii-chat\033[K\r\n");
  frame_buffer_reset(buffer);
  invitation_render_logo(buffer, 4, 0, false, true);
  cr_assert_str_eq(frame_buffer_get_content(buffer), "asci\033[K\r\n");
  frame_buffer_reset(buffer);
  invitation_render_logo(buffer, 20, 0, true, true);
  char initial[2048];
  cr_assert_lt(frame_buffer_get_length(buffer), sizeof(initial));
  strcpy(initial, frame_buffer_get_content(buffer));
  frame_buffer_reset(buffer);
  invitation_render_logo(buffer, 20, 50, true, true);
  cr_assert_not_null(strstr(frame_buffer_get_content(buffer), "\033[38;2;"));
  cr_assert_str_neq(initial, frame_buffer_get_content(buffer));
  frame_buffer_destroy(buffer);
}
