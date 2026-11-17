#include <catch2/catch_test_macros.hpp>
#include <ascii-chat/tui/invitation.h>
#include <string.h>

TEST_CASE("invitation_contract/hosting_joining_and_pending_have_distinct_instructions", "[invitation_contract] [unit]") {
  frame_buffer_t *buffer = frame_buffer_create(24, 80);
  REQUIRE((buffer) != nullptr);
  terminal_size_t size = {.cols = 80, .rows = 24};
  invitation_render(buffer, size, "blue-mountain-tiger", false, 0, false);
  const char *text = frame_buffer_get_content(buffer);
  REQUIRE((strstr(text, "Share this string to connect:")) != nullptr);
  const char *session = strstr(text, "blue-mountain-tiger");
  const char *command = strstr(text, "Run: ascii-chat blue-mountain-tiger");
  REQUIRE((session) != nullptr);
  REQUIRE((command) != nullptr);
  REQUIRE((session) < (command));
  REQUIRE((strstr(text, "\033[38;2;")) == nullptr);
  frame_buffer_reset(buffer);
  invitation_render(buffer, size, "blue-mountain-tiger", true, 10, true);
  text = frame_buffer_get_content(buffer);
  REQUIRE((strstr(text, "Connecting to session:")) != nullptr);
  REQUIRE((strstr(text, "blue-mountain-tiger")) != nullptr);
  REQUIRE((strstr(text, "Run:")) == nullptr);
  REQUIRE((strstr(text, "\033[38;2;")) != nullptr);
  for (int empty = 0; empty < 2; ++empty) {
    frame_buffer_reset(buffer);
    invitation_render(buffer, size, empty ? "" : NULL, false, 0, false);
    text = frame_buffer_get_content(buffer);
    REQUIRE((strstr(text, "Creating session...")) != nullptr);
    REQUIRE((strstr(text, "Run:")) == nullptr);
    REQUIRE((strstr(text, "blue-mountain-tiger")) == nullptr);
  }
  frame_buffer_destroy(buffer);
}

TEST_CASE("invitation_contract/short_and_narrow_terminals_preserve_session_within_bounds", "[invitation_contract] [unit]") {
  frame_buffer_t *buffer = frame_buffer_create(0, 0);
  REQUIRE((buffer) != nullptr);
  const terminal_size_t sizes[] = {{.cols = 80, .rows = 2}, {.cols = 20, .rows = 10}, {.cols = 2, .rows = 2}, {.cols = 52, .rows = 6}};
  for (size_t i = 0; i < sizeof(sizes) / sizeof(*sizes); ++i) {
    frame_buffer_reset(buffer);
    invitation_render(buffer, sizes[i], "blue-tiger", false, 0, false);
    const char *text = frame_buffer_get_content(buffer);
    size_t rows = 0, cells = 0;
    for (const char *cursor = text; *cursor;) {
      if (*cursor == '\033') {
        REQUIRE((cursor[1]) == ('['));
        REQUIRE((cursor[2] == 'K' || cursor[2] == 'J'));
        cursor += 3;
      } else if (*cursor == '\n') {
        REQUIRE((cells) <= ((size_t)sizes[i].cols - 1));
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
    REQUIRE((rows) <= ((size_t)sizes[i].rows - 1));
    REQUIRE((strlen(text)) >= (3));
    REQUIRE(std::strcmp((text + strlen(text) - 3), ("\033[J")) == 0);
    if (i == 0) {
      REQUIRE((strstr(text, "blue-tiger")) != nullptr);
      REQUIRE((strstr(text, "Share this string")) == nullptr);
    }
  }
  frame_buffer_reset(buffer);
  frame_buffer_printf(buffer, "%s", "sentinel");
  invitation_render(buffer, (terminal_size_t){.cols = 1, .rows = 24}, "blue-tiger", false, 0, false);
  invitation_render(buffer, (terminal_size_t){.cols = 80, .rows = 1}, "blue-tiger", false, 0, false);
  invitation_render(NULL, sizes[0], "blue-tiger", false, 0, false);
  REQUIRE(std::strcmp((frame_buffer_get_content(buffer)), ("sentinel")) == 0);
  frame_buffer_destroy(buffer);
}

TEST_CASE("invitation_contract/compact_logo_is_clipped_centered_and_animated", "[invitation_contract] [unit]") {
  frame_buffer_t *buffer = frame_buffer_create(0, 0);
  REQUIRE((buffer) != nullptr);
  invitation_render_logo(buffer, 20, 0, false, true);
  REQUIRE(std::strcmp((frame_buffer_get_content(buffer)), ("     ascii-chat\033[K\r\n")) == 0);
  frame_buffer_reset(buffer);
  invitation_render_logo(buffer, 4, 0, false, true);
  REQUIRE(std::strcmp((frame_buffer_get_content(buffer)), ("asci\033[K\r\n")) == 0);
  frame_buffer_reset(buffer);
  invitation_render_logo(buffer, 20, 0, true, true);
  char initial[2048];
  REQUIRE((frame_buffer_get_length(buffer)) < (sizeof(initial)));
  strcpy(initial, frame_buffer_get_content(buffer));
  frame_buffer_reset(buffer);
  invitation_render_logo(buffer, 20, 50, true, true);
  REQUIRE((strstr(frame_buffer_get_content(buffer), "\033[38;2;")) != nullptr);
  REQUIRE(std::strcmp((initial), (frame_buffer_get_content(buffer))) != 0);
  frame_buffer_destroy(buffer);
}
