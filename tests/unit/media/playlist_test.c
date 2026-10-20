#include <criterion/criterion.h>
#include <ascii-chat/media/playlist.h>
#include <string.h>

Test(media_playlist, quoted_drops_and_uri_lines_preserve_order) {
  media_playlist_t list = {0}, again = {0};
  cr_assert_eq(media_playlist_parse("  \"C:\\Videos\\first movie.webm\"  \"C:\\Videos\\second.mp4\" ", &list),
               ASCIICHAT_OK);
  cr_assert_eq(list.count, 2);
  cr_assert_str_eq(list.paths[0], "C:\\Videos\\first movie.webm");
  cr_assert_str_eq(list.paths[1], "C:\\Videos\\second.mp4");
  char text[1024];
  cr_assert_eq(media_playlist_format(&list, text, sizeof(text)), ASCIICHAT_OK);
  cr_assert_eq(text[strlen(text) - 1], '"');
  cr_assert_not_null(strchr(text, '\n'));
  cr_assert_eq(media_playlist_parse(text, &again), ASCIICHAT_OK);
  cr_assert_eq(again.count, list.count);
  cr_assert_str_eq(again.paths[1], list.paths[1]);
  cr_assert_eq(media_playlist_parse("file:///tmp/a%20b.mp4\nhttps://example.com/b.webm\r\n", &list), ASCIICHAT_OK);
  cr_assert_eq(list.count, 2);
  cr_assert_str_eq(list.paths[0], "/tmp/a b.mp4");
  cr_assert_not(list.urls[0]);
  cr_assert(list.urls[1]);
}

Test(media_playlist, invalid_edits_do_not_replace_the_list) {
  media_playlist_t list = {0};
  cr_assert_eq(media_playlist_append(&list, "/tmp/good.mp4", false), ASCIICHAT_OK);
  cr_assert_neq(media_playlist_parse("\"unfinished", &list), ASCIICHAT_OK);
  cr_assert_str_eq(list.paths[0], "/tmp/good.mp4");
  cr_assert_neq(media_playlist_parse("\"a\"\"b\"", &list), ASCIICHAT_OK);
  cr_assert_neq(media_playlist_parse("evil\033[2J.mp4", &list), ASCIICHAT_OK);
  cr_assert_eq(list.count, 1);
}

Test(media_playlist, boundaries_and_stdin_are_explicit) {
  media_playlist_t list = {0};
  cr_assert_eq(media_playlist_append(&list, "-", false), ASCIICHAT_OK);
  cr_assert_neq(media_playlist_append(&list, "/tmp/file.mp4", false), ASCIICHAT_OK);
  list = (media_playlist_t){0};
  for (size_t i = 0; i < MEDIA_PLAYLIST_MAX_ITEMS; ++i)
    cr_assert_eq(media_playlist_append(&list, "/tmp/file.mp4", false), ASCIICHAT_OK);
  cr_assert_neq(media_playlist_append(&list, "/tmp/overflow.mp4", false), ASCIICHAT_OK);
  size_t next = 999;
  cr_assert_not(media_playlist_adjacent(&list, -1, &next));
  cr_assert_eq(next, 999);
  cr_assert(media_playlist_adjacent(&list, 1, &next));
  cr_assert_eq(next, 1);
  list.index = list.count - 1;
  cr_assert_not(media_playlist_adjacent(&list, 1, &next));
  cr_assert(media_playlist_adjacent(&list, -1, &next));
  cr_assert_eq(next, list.count - 2);
}

Test(media_playlist, unquoted_absolute_drop_lists_keep_spaces_within_paths) {
  media_playlist_t list = {0};
  cr_assert_eq(media_playlist_parse("C:\\My Videos\\a.mp4 C:\\Videos\\b.mp4 ", &list), ASCIICHAT_OK);
  cr_assert_eq(list.count, 2);
  cr_assert_str_eq(list.paths[0], "C:\\My Videos\\a.mp4");
  cr_assert_str_eq(list.paths[1], "C:\\Videos\\b.mp4");
}

Test(media_playlist, display_capacity_includes_quotes_and_terminator) {
  media_playlist_t list = {0};
  cr_assert_eq(media_playlist_append(&list, "a", false), ASCIICHAT_OK);
  char exact[4], short_buffer[3];
  cr_assert_eq(media_playlist_format(&list, exact, sizeof(exact)), ASCIICHAT_OK);
  cr_assert_str_eq(exact, "\"a\"");
  cr_assert_neq(media_playlist_format(&list, short_buffer, sizeof(short_buffer)), ASCIICHAT_OK);
}

Test(media_playlist, bare_cli_urls_normalize_before_capture_matches_the_playlist) {
  media_playlist_t list = {0};
  cr_assert_eq(media_playlist_append(&list, "example.com/first.mp4", true), ASCIICHAT_OK);
  cr_assert_str_eq(list.paths[0], "http://example.com/first.mp4");
}
