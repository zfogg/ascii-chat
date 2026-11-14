#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/media/playlist.h>
#include <string.h>

TEST_CASE("media_playlist/quoted_drops_and_uri_lines_preserve_order", "[media_playlist]") {
  media_playlist_t list = {0}, again = {0};
  ASCIICHAT_REQUIRE_EQ(media_playlist_parse("  \"C:\\Videos\\first movie.webm\"  \"C:\\Videos\\second.mp4\" ", &list),
               ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(list.count, 2);
  ASCIICHAT_REQUIRE_STR_EQ(list.paths[0], "C:\\Videos\\first movie.webm");
  ASCIICHAT_REQUIRE_STR_EQ(list.paths[1], "C:\\Videos\\second.mp4");
  char text[1024];
  ASCIICHAT_REQUIRE_EQ(media_playlist_format(&list, text, sizeof(text)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(text[strlen(text) - 1], '"');
  ASCIICHAT_REQUIRE_NOT_NULL(strchr(text, '\n'));
  ASCIICHAT_REQUIRE_EQ(media_playlist_parse(text, &again), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(again.count, list.count);
  ASCIICHAT_REQUIRE_STR_EQ(again.paths[1], list.paths[1]);
  ASCIICHAT_REQUIRE_EQ(media_playlist_parse("file:///tmp/a%20b.mp4\nhttps://example.com/b.webm\r\n", &list), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(list.count, 2);
  ASCIICHAT_REQUIRE_STR_EQ(list.paths[0], "/tmp/a b.mp4");
  ASCIICHAT_REQUIRE_NOT(list.urls[0]);
  ASCIICHAT_REQUIRE(list.urls[1]);
}

TEST_CASE("media_playlist/invalid_edits_do_not_replace_the_list", "[media_playlist]") {
  media_playlist_t list = {0};
  ASCIICHAT_REQUIRE_EQ(media_playlist_append(&list, "/tmp/good.mp4", false), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(media_playlist_parse("\"unfinished", &list), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_STR_EQ(list.paths[0], "/tmp/good.mp4");
  ASCIICHAT_REQUIRE_NE(media_playlist_parse("\"a\"\"b\"", &list), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(media_playlist_parse("evil\033[2J.mp4", &list), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(list.count, 1);
}

TEST_CASE("media_playlist/boundaries_and_stdin_are_explicit", "[media_playlist]") {
  media_playlist_t list = {0};
  ASCIICHAT_REQUIRE_EQ(media_playlist_append(&list, "-", false), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(media_playlist_append(&list, "/tmp/file.mp4", false), ASCIICHAT_OK);
  list = (media_playlist_t){0};
  for (size_t i = 0; i < MEDIA_PLAYLIST_MAX_ITEMS; ++i)
    ASCIICHAT_REQUIRE_EQ(media_playlist_append(&list, "/tmp/file.mp4", false), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(media_playlist_append(&list, "/tmp/overflow.mp4", false), ASCIICHAT_OK);
  size_t next = 999;
  ASCIICHAT_REQUIRE_NOT(media_playlist_adjacent(&list, -1, &next));
  ASCIICHAT_REQUIRE_EQ(next, 999);
  ASCIICHAT_REQUIRE(media_playlist_adjacent(&list, 1, &next));
  ASCIICHAT_REQUIRE_EQ(next, 1);
  list.index = list.count - 1;
  ASCIICHAT_REQUIRE_NOT(media_playlist_adjacent(&list, 1, &next));
  ASCIICHAT_REQUIRE(media_playlist_adjacent(&list, -1, &next));
  ASCIICHAT_REQUIRE_EQ(next, list.count - 2);
}

TEST_CASE("media_playlist/unquoted_absolute_drop_lists_keep_spaces_within_paths", "[media_playlist]") {
  media_playlist_t list = {0};
  ASCIICHAT_REQUIRE_EQ(media_playlist_parse("C:\\My Videos\\a.mp4 C:\\Videos\\b.mp4 ", &list), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(list.count, 2);
  ASCIICHAT_REQUIRE_STR_EQ(list.paths[0], "C:\\My Videos\\a.mp4");
  ASCIICHAT_REQUIRE_STR_EQ(list.paths[1], "C:\\Videos\\b.mp4");
}

TEST_CASE("media_playlist/display_capacity_includes_quotes_and_terminator", "[media_playlist]") {
  media_playlist_t list = {0};
  ASCIICHAT_REQUIRE_EQ(media_playlist_append(&list, "a", false), ASCIICHAT_OK);
  char exact[4], short_buffer[3];
  ASCIICHAT_REQUIRE_EQ(media_playlist_format(&list, exact, sizeof(exact)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_STR_EQ(exact, "\"a\"");
  ASCIICHAT_REQUIRE_NE(media_playlist_format(&list, short_buffer, sizeof(short_buffer)), ASCIICHAT_OK);
}

TEST_CASE("media_playlist/bare_cli_urls_normalize_before_capture_matches_the_playlist", "[media_playlist]") {
  media_playlist_t list = {0};
  ASCIICHAT_REQUIRE_EQ(media_playlist_append(&list, "example.com/first.mp4", true), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_STR_EQ(list.paths[0], "http://example.com/first.mp4");
}
