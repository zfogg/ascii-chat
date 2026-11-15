#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/log/log.h>
#include <libplatform/filesystem.h>
#include <ascii-chat/session/session_log_buffer.h>
#include <ascii-chat/common.h>
#include <string.h>
#include <ascii-chat/tui/notice.h>
#include <ascii-chat/util/utf8.h>

static char directory[PLATFORM_MAX_PATH_LENGTH], path[PLATFORM_MAX_PATH_LENGTH];

static void setup(void) {
  REQUIRE((platform_mkdtemp(directory, sizeof(directory), "log-persistence")) == (ASCIICHAT_OK));
  safe_snprintf(path, sizeof(path), "%s/records.log", directory);
  log_init(path, LOG_DEV, false, false);
  log_set_level(LOG_DEV);
  log_set_terminal_output(false);
}

static void teardown(void) {
  log_clear_session_log_buffer();
  log_destroy();
  platform_rmdir_recursive(directory);
}

static char *read_records(size_t *size) {
  FILE *file = platform_fopen("log-records-test", path, "rb");
  REQUIRE((file) != nullptr);
  REQUIRE((fseek(file, 0, SEEK_END)) == (0));
  long length = ftell(file);
  REQUIRE((length) >= (0));
  rewind(file);
  char *text = SAFE_MALLOC((size_t)length + 1, char *);
  REQUIRE((fread(text, 1, (size_t)length, file)) == ((size_t)length));
  text[length] = '\0';
  fclose(file);
  if (size)
    *size = (size_t)length;
  return text;
}
TEST_CASE("log_persistence/file_records_survive_disabled_terminal_and_exclude_terminal_only_output", "[log_persistence]") {
  REQUIRE((log_set_format("[%level] ", false)) == (ASCIICHAT_OK));
  log_msg(LOG_INFO, "test.c", 20, "test", "persist-%d", 42);
  log_plain_msg("plain-%s", "record");
  log_file_msg("file-only-record");
  log_colored_msg_impl(LOG_WARN, "test.c", 21, "test", "\033[31mcolored-display\033[0m", "plain-color-record");
  log_terminal_msg(LOG_ERROR, "test.c", 22, "test", "terminal-only-record");
  log_terminal_colored_msg_impl(LOG_ERROR, "test.c", 23, "test", "terminal-color", "terminal-plain");
  char *records = read_records(NULL);
  REQUIRE((strstr(records, "persist-42")) != nullptr);
  REQUIRE((strstr(records, "plain-record")) != nullptr);
  REQUIRE((strstr(records, "file-only-record")) != nullptr);
  REQUIRE((strstr(records, "plain-color-record")) != nullptr);
  REQUIRE((strstr(records, "terminal-only-record")) == nullptr);
  REQUIRE((strstr(records, "terminal-plain")) == nullptr);
  REQUIRE((strstr(records, "\033[")) == nullptr);
  SAFE_FREE(records);
}

TEST_CASE("log_persistence/terminal_history_receives_persistent_and_presentation_messages", "[log_persistence]") {
  session_log_buffer_t *buffer = session_log_buffer_create();
  REQUIRE((buffer) != nullptr);
  log_set_session_log_buffer(buffer);
  REQUIRE((log_get_session_log_buffer()) == (buffer));
  log_msg(LOG_INFO, "test.c", 1, "test", "persistent-history");
  log_terminal_msg(LOG_WARN, "test.c", 2, "test", "presentation-history");
  session_log_entry_t entries[8];
  size_t count = session_log_buffer_get_recent(buffer, entries, 8);
  bool persistent = false, presentation = false;
  for (size_t i = 0; i < count; ++i) {
    persistent |= strstr(entries[i].message, "persistent-history") != NULL;
    presentation |= strstr(entries[i].message, "presentation-history") != NULL;
  }
  REQUIRE(persistent);
  REQUIRE(presentation);
  log_clear_session_log_buffer();
  REQUIRE((log_get_session_log_buffer()) == nullptr);
  session_log_buffer_destroy(buffer);
}

TEST_CASE("log_persistence/tail_rotation_keeps_recent_complete_records_and_new_writes", "[log_persistence]") {
  char payload[4001];
  memset(payload, 'x', sizeof(payload) - 1);
  payload[sizeof(payload) - 1] = '\0';
  for (unsigned i = 0; i < 850; ++i)
    log_file_msg("record-%04u %s", i, payload);
  log_file_msg("after-rotation-marker");
  size_t length;
  char *records = read_records(&length);
  REQUIRE((length) < ((size_t)MAX_LOG_SIZE));
  REQUIRE((length) > ((size_t)MAX_LOG_SIZE / 2));
  REQUIRE((strstr(records, "record-0000 ")) == nullptr);
  REQUIRE((strstr(records, "record-0849 ")) != nullptr);
  REQUIRE((strstr(records, "Log tail-rotated")) != nullptr);
  REQUIRE((strstr(records, "after-rotation-marker\n")) != nullptr);
  REQUIRE((strncmp(records, "record-", 7)) == (0));
  SAFE_FREE(records);
}

TEST_CASE("log_persistence/disabling_file_output_stops_persistence", "[log_persistence]") {
  log_file_msg("before-disable");
  size_t before, after;
  char *records = read_records(&before);
  SAFE_FREE(records);
  log_disable_file_output();
  log_file_msg("after-disable");
  log_msg(LOG_ERROR, "test.c", 1, "test", "also-after-disable");
  records = read_records(&after);
  REQUIRE((after) == (before));
  REQUIRE((strstr(records, "before-disable")) != nullptr);
  REQUIRE((strstr(records, "after-disable")) == nullptr);
  SAFE_FREE(records);
}

TEST_CASE("log_persistence/recoloring_rejects_incomplete_headers_and_keeps_message_content", "[log_persistence]") {
  char output[4096];
  const char *invalid[] = {"",
                           "plain",
                           "[",
                           "[] [INFO] x.c:1 in f(): text",
                           "[12] INFO",
                           "[12] [",
                           "[12] [] x.c:1 in f(): text",
                           "[12] [INFO] x.c",
                           "[12] [INFO] x.c:no in f(): text",
                           "[12] [INFO] x.c:1 missing f(): text",
                           "[12] [INFO] x.c:1 in (): text",
                           "[12] [INFO] x.c:1 in f"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    REQUIRE((log_recolor_plain_entry(invalid[i], output, sizeof(output))) == (0));
  REQUIRE((log_recolor_plain_entry(NULL, output, sizeof(output))) == (0));
  REQUIRE((log_recolor_plain_entry("[12] [INFO] x.c:1 in f(): value", NULL, sizeof(output))) == (0));
  REQUIRE((log_recolor_plain_entry("[12] [INFO] x.c:1 in f(): value", output, 20)) == (0));
  const char *levels[] = {"DEV", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "UNKNOWN"};
  for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i) {
    char input[256];
    safe_snprintf(input, sizeof(input), "[12:34:56.123456] [%s] [tid:123] x.c:42 in function(): unique-message", levels[i]);
    size_t length = log_recolor_plain_entry(input, output, sizeof(output));
    REQUIRE((length) > (0));
    REQUIRE((length) == (strlen(output)));
    REQUIRE((strstr(output, "unique-message")) != nullptr);
    REQUIRE((strstr(output, "function")) != nullptr);
  }
}

TEST_CASE("log_persistence/notice_records_follow_levels_while_announcements_bypass_them", "[log_persistence]") {
  session_log_buffer_t *buffer = session_log_buffer_create();
  REQUIRE((buffer) != nullptr);
  log_set_session_log_buffer(buffer);
  REQUIRE((log_set_format("[%level] ", false)) == (ASCIICHAT_OK));
  log_set_level(LOG_ERROR);
  ui_notice_log(UI_NOTICE_INFO, "notice-test.c", 12, "test", "FILTERED TITLE", "hidden body");
  ui_notice_log(UI_NOTICE_DANGER, "notice-test.c", 13, "test", "ERROR TITLE", "error body %d", 7);
  ui_notice_announce("notice-test.c", 14, "test", "ANNOUNCED TITLE", "important body %s", "ready");
  char *records = read_records(NULL);
  REQUIRE((strstr(records, "FILTERED TITLE")) == nullptr);
  REQUIRE((strstr(records, "ERROR TITLE")) != nullptr);
  REQUIRE((strstr(records, "error body 7")) != nullptr);
  REQUIRE((strstr(records, "ANNOUNCED TITLE")) != nullptr);
  REQUIRE((strstr(records, "important body ready")) != nullptr);
  REQUIRE((strstr(records, "\033[")) == nullptr);
  SAFE_FREE(records);
  session_log_entry_t entries[8];
  size_t count = session_log_buffer_get_recent(buffer, entries, 8);
  REQUIRE((count) == (4));
  REQUIRE(std::strcmp((entries[0].message), ("ERROR TITLE")) == 0);
  REQUIRE(std::strcmp((entries[1].message), ("error body 7")) == 0);
  REQUIRE(std::strcmp((entries[2].message), ("ANNOUNCED TITLE")) == 0);
  REQUIRE(std::strcmp((entries[3].message), ("important body ready")) == 0);
  log_clear_session_log_buffer();
  session_log_buffer_destroy(buffer);
}
TEST_CASE("log_persistence/long_notice_chunks_preserve_every_utf8_character_and_final_marker", "[log_persistence]") {
  char body[6004];
  for (size_t i = 0; i < 6000; i += 3)
    memcpy(body + i, "\xe2\x82\xac", 3);
  memcpy(body + 6000, "END", 4);
  ui_notice_log(UI_NOTICE_WARNING, "notice-test.c", 21, "test", "NOTICE", "%s", body);
  char *records = read_records(NULL);
  REQUIRE(utf8_is_valid(records));
  REQUIRE((strstr(records, "NOTICE")) != nullptr);
  REQUIRE((strstr(records, "END")) != nullptr);
  size_t characters = 0;
  const char *cursor = records;
  while ((cursor = strstr(cursor, "\xe2\x82\xac")) != NULL) {
    ++characters;
    cursor += 3;
  }
  REQUIRE((characters) == (2000));
  REQUIRE((strstr(records, "\033[")) == nullptr);
  SAFE_FREE(records);
}
