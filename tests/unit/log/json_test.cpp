#include <catch2/catch_test_macros.hpp>
#include <ascii-chat/log/json.h>
#include <libplatform/filesystem.h>
#include <ascii-chat/common.h>
#include <yyjson.h>
#include <stdio.h>
#include <string.h>

static char fixture_path[512];
static int fixture_fd = -1;
static FILE *fixture_stream;
static yyjson_doc *parsed;

static void setup(void) {
  REQUIRE((platform_create_temp_file(fixture_path, sizeof(fixture_path), "json-log", &fixture_fd)) == (0));
  REQUIRE((platform_temp_file_open("json-log", fixture_path, &fixture_fd)) == (ASCIICHAT_OK));
  fixture_stream = platform_fdopen("json-log", fixture_fd, "w+");
  REQUIRE((fixture_stream) != nullptr);
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
  REQUIRE((fgets(record, sizeof(record), fixture_stream)) != nullptr);
  size_t length = strlen(record);
  REQUIRE((length) > (1));
  REQUIRE((record[length - 1]) == ('\n'));
  if (parsed)
    yyjson_doc_free(parsed);
  parsed = yyjson_read(record, length, 0);
  REQUIRE((parsed) != nullptr);
  return yyjson_doc_get_root(parsed);
}

TEST_CASE("json_log/structured_records_preserve_escaped_values_and_optional_metadata", "[json_log] [unit]") {
  const char message[] = "quote \" slash \\ newline\ncarriage\rtab\tcontrol\x01 unicode \xc3\xa9";
  const uint64_t timestamp = UINT64_C(1700000000123456789);
  log_json_write(fixture_fd, LOG_WARN, timestamp, "lib/log/json.c", 42, "test_json", message);
  log_json_write(fixture_fd, LOG_INFO, timestamp, NULL, 0, NULL, "second record");
  rewind(fixture_stream);
  yyjson_val *root = read_record();
  yyjson_val *header = yyjson_obj_get(root, "header");
  REQUIRE(std::strcmp((yyjson_get_str(yyjson_obj_get(header, "level"))), ("WARN")) == 0);
  REQUIRE(std::strcmp((yyjson_get_str(yyjson_obj_get(header, "file"))), ("lib/log/json.c")) == 0);
  REQUIRE((yyjson_get_int(yyjson_obj_get(header, "line"))) == (42));
  REQUIRE(std::strcmp((yyjson_get_str(yyjson_obj_get(header, "func"))), ("test_json")) == 0);
  REQUIRE(yyjson_is_uint(yyjson_obj_get(header, "tid")));
  const char *time = yyjson_get_str(yyjson_obj_get(header, "timestamp"));
  REQUIRE((time) != nullptr);
  REQUIRE((strlen(time)) == (15));
  REQUIRE(std::strcmp((time + 8), (".123456")) == 0);
  REQUIRE(std::strcmp((yyjson_get_str(yyjson_obj_get(yyjson_obj_get(root, "body"), "message"))), (message)) == 0);
  root = read_record();
  header = yyjson_obj_get(root, "header");
  REQUIRE((yyjson_obj_get(header, "file")) == nullptr);
  REQUIRE((yyjson_obj_get(header, "line")) == nullptr);
  REQUIRE((yyjson_obj_get(header, "func")) == nullptr);
  REQUIRE(std::strcmp((yyjson_get_str(yyjson_obj_get(yyjson_obj_get(root, "body"), "message"))), ("second record")) == 0);
  REQUIRE((fgetc(fixture_stream)) == (EOF));
}

TEST_CASE("json_log/async_records_escape_controls_and_remain_one_line", "[json_log] [unit]") {
  const char message[] = "\"\\\n\r\t\x01 text";
  log_json_async_safe(fixture_fd, LOG_ERROR, "lib/quoted\"file.c", 9, "function\\name", message);
  log_json_async_safe(fixture_fd, LOG_FATAL, NULL, 0, NULL, "");
  rewind(fixture_stream);
  yyjson_val *root = read_record();
  yyjson_val *header = yyjson_obj_get(root, "header");
  REQUIRE(std::strcmp((yyjson_get_str(yyjson_obj_get(header, "level"))), ("ERROR")) == 0);
  REQUIRE(std::strcmp((yyjson_get_str(yyjson_obj_get(header, "file"))), ("lib/quoted\"file.c")) == 0);
  REQUIRE(std::strcmp((yyjson_get_str(yyjson_obj_get(header, "func"))), ("function\\name")) == 0);
  REQUIRE(std::strcmp((yyjson_get_str(yyjson_obj_get(yyjson_obj_get(root, "body"), "message"))), ("\"\\\n\r\t text")) == 0);
  root = read_record();
  header = yyjson_obj_get(root, "header");
  REQUIRE(std::strcmp((yyjson_get_str(yyjson_obj_get(header, "file"))), ("")) == 0);
  REQUIRE(std::strcmp((yyjson_get_str(yyjson_obj_get(header, "func"))), ("")) == 0);
  REQUIRE(std::strcmp((yyjson_get_str(yyjson_obj_get(header, "level"))), ("FATAL")) == 0);
  REQUIRE((fgetc(fixture_stream)) == (EOF));
}

TEST_CASE("json_log/async_truncation_does_not_split_escape_sequences", "[json_log] [unit]") {
  char message[700];
  memset(message, 'x', sizeof(message));
  message[510] = '"';
  message[sizeof(message) - 1] = '\0';
  log_json_async_safe(fixture_fd, LOG_DEBUG, NULL, 1, NULL, message);
  rewind(fixture_stream);
  yyjson_val *root = read_record();
  const char *actual = yyjson_get_str(yyjson_obj_get(yyjson_obj_get(root, "body"), "message"));
  REQUIRE((actual) != nullptr);
  REQUIRE((strlen(actual)) == (510));
  REQUIRE((memcmp(actual, message, 510)) == (0));
}

TEST_CASE("json_log/async_truncation_keeps_complete_utf8_characters", "[json_log] [unit]") {
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
    REQUIRE((actual) != nullptr);
    REQUIRE((strlen(actual)) == (510));
    for (size_t j = 0; j < 510; ++j)
      REQUIRE((actual[j]) == ('x'));
    root = read_record();
    actual = yyjson_get_str(yyjson_obj_get(yyjson_obj_get(root, "body"), "message"));
    REQUIRE((actual) != nullptr);
    REQUIRE((strlen(actual)) == (511));
    REQUIRE(std::strcmp((actual + 511 - strlen(characters[i])), (characters[i])) == 0);
  }
}

TEST_CASE("json_log/level_names_and_invalid_inputs", "[json_log] [unit]") {
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
    REQUIRE(std::strcmp((yyjson_get_str(yyjson_obj_get(yyjson_obj_get(root, "header"), "level"))), (names[i])) == 0);
  }
  REQUIRE((fgetc(fixture_stream)) == (EOF));
}
