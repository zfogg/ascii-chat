#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/log/json.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/common.h>
#include <yyjson.h>
#include <stdio.h>
#include <string.h>

static char fixture_path[512];
static int fixture_fd = -1;
static FILE *fixture_stream;
static yyjson_doc *parsed;

static void setup(void) {
  ASCIICHAT_REQUIRE_EQ(platform_create_temp_file(fixture_path, sizeof(fixture_path), "json-log", &fixture_fd), 0);
  ASCIICHAT_REQUIRE_EQ(platform_temp_file_open("json-log", fixture_path, &fixture_fd), ASCIICHAT_OK);
  fixture_stream = platform_fdopen("json-log", fixture_fd, "w+");
  ASCIICHAT_REQUIRE_NOT_NULL(fixture_stream);
}

static void cleanup(void) {
  if (parsed)
    yyjson_doc_free(parsed);
  parsed = NULL;
  if (fixture_stream)
    fclose(fixture_stream);
  else if (fixture_fd >= 0)
    platform_close(fixture_fd);
  fixture_stream = NULL;
  fixture_fd = -1;
  if (fixture_path[0])
    platform_delete_temp_file(fixture_path);
  fixture_path[0] = '\0';
}
static yyjson_val *read_record(void) {
  char record[8192];
  ASCIICHAT_REQUIRE_NOT_NULL(fgets(record, sizeof(record), fixture_stream));
  size_t length = strlen(record);
  ASCIICHAT_REQUIRE_GT(length, 1);
  ASCIICHAT_REQUIRE_EQ(record[length - 1], '\n', "each record must end with one newline");
  if (parsed)
    yyjson_doc_free(parsed);
  parsed = yyjson_read(record, length, 0);
  ASCIICHAT_REQUIRE_NOT_NULL(parsed, "log output must be valid standalone JSON: %s", record);
  return yyjson_doc_get_root(parsed);
}

TEST_CASE("json_log/structured_records_preserve_escaped_values_and_optional_metadata", "[json_log]") {
  const char message[] = "quote \" slash \\ newline\ncarriage\rtab\tcontrol\x01 unicode \xc3\xa9";
  const uint64_t timestamp = UINT64_C(1700000000123456789);
  log_json_write(fixture_fd, LOG_WARN, timestamp, "lib/log/json.c", 42, "test_json", message);
  log_json_write(fixture_fd, LOG_INFO, timestamp, NULL, 0, NULL, "second record");
  rewind(fixture_stream);
  yyjson_val *root = read_record();
  yyjson_val *header = yyjson_obj_get(root, "header");
  ASCIICHAT_REQUIRE_STR_EQ(yyjson_get_str(yyjson_obj_get(header, "level")), "WARN");
  ASCIICHAT_REQUIRE_STR_EQ(yyjson_get_str(yyjson_obj_get(header, "file")), "lib/log/json.c");
  ASCIICHAT_REQUIRE_EQ(yyjson_get_int(yyjson_obj_get(header, "line")), 42);
  ASCIICHAT_REQUIRE_STR_EQ(yyjson_get_str(yyjson_obj_get(header, "func")), "test_json");
  ASCIICHAT_REQUIRE(yyjson_is_uint(yyjson_obj_get(header, "tid")));
  const char *time = yyjson_get_str(yyjson_obj_get(header, "timestamp"));
  ASCIICHAT_REQUIRE_NOT_NULL(time);
  ASCIICHAT_REQUIRE_EQ(strlen(time), 15);
  ASCIICHAT_REQUIRE_STR_EQ(time + 8, ".123456", "timestamp must retain microseconds");
  ASCIICHAT_REQUIRE_STR_EQ(yyjson_get_str(yyjson_obj_get(yyjson_obj_get(root, "body"), "message")), message);
  root = read_record();
  header = yyjson_obj_get(root, "header");
  ASCIICHAT_REQUIRE_NULL(yyjson_obj_get(header, "file"));
  ASCIICHAT_REQUIRE_NULL(yyjson_obj_get(header, "line"));
  ASCIICHAT_REQUIRE_NULL(yyjson_obj_get(header, "func"));
  ASCIICHAT_REQUIRE_STR_EQ(yyjson_get_str(yyjson_obj_get(yyjson_obj_get(root, "body"), "message")), "second record");
  ASCIICHAT_REQUIRE_EQ(fgetc(fixture_stream), EOF);
}

TEST_CASE("json_log/async_records_escape_controls_and_remain_one_line", "[json_log]") {
  const char message[] = "\"\\\n\r\t\x01 text";
  log_json_async_safe(fixture_fd, LOG_ERROR, "lib/quoted\"file.c", 9, "function\\name", message);
  log_json_async_safe(fixture_fd, LOG_FATAL, NULL, 0, NULL, "");
  rewind(fixture_stream);
  yyjson_val *root = read_record();
  yyjson_val *header = yyjson_obj_get(root, "header");
  ASCIICHAT_REQUIRE_STR_EQ(yyjson_get_str(yyjson_obj_get(header, "level")), "ERROR");
  ASCIICHAT_REQUIRE_STR_EQ(yyjson_get_str(yyjson_obj_get(header, "file")), "lib/quoted\"file.c");
  ASCIICHAT_REQUIRE_STR_EQ(yyjson_get_str(yyjson_obj_get(header, "func")), "function\\name");
  ASCIICHAT_REQUIRE_STR_EQ(yyjson_get_str(yyjson_obj_get(yyjson_obj_get(root, "body"), "message")), "\"\\\n\r\t text");
  root = read_record();
  header = yyjson_obj_get(root, "header");
  ASCIICHAT_REQUIRE_STR_EQ(yyjson_get_str(yyjson_obj_get(header, "file")), "");
  ASCIICHAT_REQUIRE_STR_EQ(yyjson_get_str(yyjson_obj_get(header, "func")), "");
  ASCIICHAT_REQUIRE_STR_EQ(yyjson_get_str(yyjson_obj_get(header, "level")), "FATAL");
  ASCIICHAT_REQUIRE_EQ(fgetc(fixture_stream), EOF);
}

TEST_CASE("json_log/async_truncation_does_not_split_escape_sequences", "[json_log]") {
  char message[700];
  memset(message, 'x', sizeof(message));
  message[510] = '"';
  message[sizeof(message) - 1] = '\0';
  log_json_async_safe(fixture_fd, LOG_DEBUG, NULL, 1, NULL, message);
  rewind(fixture_stream);
  yyjson_val *root = read_record();
  const char *actual = yyjson_get_str(yyjson_obj_get(yyjson_obj_get(root, "body"), "message"));
  ASCIICHAT_REQUIRE_NOT_NULL(actual);
  ASCIICHAT_REQUIRE_EQ(strlen(actual), 510);
  ASCIICHAT_REQUIRE_EQ(memcmp(actual, message, 510), 0);
}

TEST_CASE("json_log/async_truncation_keeps_complete_utf8_characters", "[json_log]") {
  const char *characters[] = {"\xc3\xa9", "\xe2\x96\x91", "\xf0\x9f\x98\x80"};
  for (size_t i = 0; i < 3; ++i) {
    char message[520];
    memset(message, 'x', 510);
    strcpy(message + 510, characters[i]);
    log_json_async_safe(fixture_fd, LOG_INFO, NULL, 0, NULL, message);
    size_t prefix = 511 - strlen(characters[i]);
    memset(message, 'x', prefix);
    strcpy(message + prefix, characters[i]);
    log_json_async_safe(fixture_fd, LOG_INFO, NULL, 0, NULL, message);
  }
  rewind(fixture_stream);
  for (size_t i = 0; i < 3; ++i) {
    yyjson_val *root = read_record();
    const char *actual = yyjson_get_str(yyjson_obj_get(yyjson_obj_get(root, "body"), "message"));
    ASCIICHAT_REQUIRE_NOT_NULL(actual);
    ASCIICHAT_REQUIRE_EQ(strlen(actual), 510, "a character that cannot fit must be omitted completely");
    for (size_t j = 0; j < 510; ++j)
      ASCIICHAT_REQUIRE_EQ(actual[j], 'x');
    root = read_record();
    actual = yyjson_get_str(yyjson_obj_get(yyjson_obj_get(root, "body"), "message"));
    ASCIICHAT_REQUIRE_NOT_NULL(actual);
    ASCIICHAT_REQUIRE_EQ(strlen(actual), 511);
    ASCIICHAT_REQUIRE_STR_EQ(actual + 511 - strlen(characters[i]), characters[i]);
  }
}

TEST_CASE("json_log/level_names_and_invalid_inputs", "[json_log]") {
  const log_level_t levels[] = {LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL, (log_level_t)99};
  const char *names[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL", "UNKNOWN"};
  log_json_write(-1, LOG_INFO, 0, NULL, 0, NULL, "ignored");
  log_json_write(fixture_fd, LOG_INFO, 0, NULL, 0, NULL, NULL);
  log_json_async_safe(-1, LOG_INFO, NULL, 0, NULL, "ignored");
  log_json_async_safe(fixture_fd, LOG_INFO, NULL, 0, NULL, NULL);
  for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i)
    log_json_write(fixture_fd, levels[i], 0, NULL, 0, NULL, "level check");
  rewind(fixture_stream);
  for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i) {
    yyjson_val *root = read_record();
    ASCIICHAT_REQUIRE_STR_EQ(yyjson_get_str(yyjson_obj_get(yyjson_obj_get(root, "header"), "level")), names[i]);
  }
  ASCIICHAT_REQUIRE_EQ(fgetc(fixture_stream), EOF);
}
