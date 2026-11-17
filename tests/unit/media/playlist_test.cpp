#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/media/playlist.h>
#include <string.h>

TEST_CASE("media_playlist/quoted_drops_and_uri_lines_preserve_order", "[media_playlist]") {
  media_playlist_t list = {0}, again = {0};
  REQUIRE((media_playlist_parse("  \"C:\\Videos\\first movie.webm\"  \"C:\\Videos\\second.mp4\" ", &list)) == (ASCIICHAT_OK));
  REQUIRE((list.count) == (2));
  REQUIRE(std::strcmp((list.paths[0]), ("C:\\Videos\\first movie.webm")) == 0);
  REQUIRE(std::strcmp((list.paths[1]), ("C:\\Videos\\second.mp4")) == 0);
  char text[1024];
  REQUIRE((media_playlist_format(&list, text, sizeof(text))) == (ASCIICHAT_OK));
  REQUIRE((text[strlen(text) - 1]) == ('"'));
  REQUIRE((strchr(text, '\n')) != nullptr);
  REQUIRE((media_playlist_parse(text, &again)) == (ASCIICHAT_OK));
  REQUIRE((again.count) == (list.count));
  REQUIRE(std::strcmp((again.paths[1]), (list.paths[1])) == 0);
  REQUIRE((media_playlist_parse("file:///tmp/a%20b.mp4\nhttps://example.com/b.webm\r\n", &list)) == (ASCIICHAT_OK));
  REQUIRE((list.count) == (2));
  REQUIRE(std::strcmp((list.paths[0]), ("/tmp/a b.mp4")) == 0);
  REQUIRE(!(list.urls[0]));
  REQUIRE(list.urls[1]);
}

TEST_CASE("media_playlist/invalid_edits_do_not_replace_the_list", "[media_playlist]") {
  media_playlist_t list = {0};
  REQUIRE((media_playlist_append(&list, "/tmp/good.mp4", false)) == (ASCIICHAT_OK));
  REQUIRE((media_playlist_parse("\"unfinished", &list)) != (ASCIICHAT_OK));
  REQUIRE(std::strcmp((list.paths[0]), ("/tmp/good.mp4")) == 0);
  REQUIRE((media_playlist_parse("\"a\"\"b\"", &list)) != (ASCIICHAT_OK));
  REQUIRE((media_playlist_parse("evil\033[2J.mp4", &list)) != (ASCIICHAT_OK));
  REQUIRE((list.count) == (1));
}

TEST_CASE("media_playlist/boundaries_and_stdin_are_explicit", "[media_playlist]") {
  media_playlist_t list = {0};
  REQUIRE((media_playlist_append(&list, "-", false)) == (ASCIICHAT_OK));
  REQUIRE((media_playlist_append(&list, "/tmp/file.mp4", false)) != (ASCIICHAT_OK));
  list = (media_playlist_t){0};
  for (size_t i = 0; i < MEDIA_PLAYLIST_MAX_ITEMS; ++i)
    REQUIRE((media_playlist_append(&list, "/tmp/file.mp4", false)) == (ASCIICHAT_OK));
  REQUIRE((media_playlist_append(&list, "/tmp/overflow.mp4", false)) != (ASCIICHAT_OK));
  size_t next = 999;
  REQUIRE(!(media_playlist_adjacent(&list, -1, &next)));
  REQUIRE((next) == (999));
  REQUIRE(media_playlist_adjacent(&list, 1, &next));
  REQUIRE((next) == (1));
  list.index = list.count - 1;
  REQUIRE(!(media_playlist_adjacent(&list, 1, &next)));
  REQUIRE(media_playlist_adjacent(&list, -1, &next));
  REQUIRE((next) == (list.count - 2));
}

TEST_CASE("media_playlist/unquoted_absolute_drop_lists_keep_spaces_within_paths", "[media_playlist]") {
  media_playlist_t list = {0};
  REQUIRE((media_playlist_parse("C:\\My Videos\\a.mp4 C:\\Videos\\b.mp4 ", &list)) == (ASCIICHAT_OK));
  REQUIRE((list.count) == (2));
  REQUIRE(std::strcmp((list.paths[0]), ("C:\\My Videos\\a.mp4")) == 0);
  REQUIRE(std::strcmp((list.paths[1]), ("C:\\Videos\\b.mp4")) == 0);
}

TEST_CASE("media_playlist/display_capacity_includes_quotes_and_terminator", "[media_playlist]") {
  media_playlist_t list = {0};
  REQUIRE((media_playlist_append(&list, "a", false)) == (ASCIICHAT_OK));
  char exact[4], short_buffer[3];
  REQUIRE((media_playlist_format(&list, exact, sizeof(exact))) == (ASCIICHAT_OK));
  REQUIRE(std::strcmp((exact), ("\"a\"")) == 0);
  REQUIRE((media_playlist_format(&list, short_buffer, sizeof(short_buffer))) != (ASCIICHAT_OK));
}

TEST_CASE("media_playlist/bare_cli_urls_normalize_before_capture_matches_the_playlist", "[media_playlist]") {
  media_playlist_t list = {0};
  REQUIRE((media_playlist_append(&list, "example.com/first.mp4", true)) == (ASCIICHAT_OK));
  REQUIRE(std::strcmp((list.paths[0]), ("http://example.com/first.mp4")) == 0);
}
