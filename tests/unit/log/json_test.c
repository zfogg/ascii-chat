#include <criterion/criterion.h>
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
  cr_assert_eq(platform_create_temp_file(fixture_path, sizeof(fixture_path), "json-log", &fixture_fd), 0);
  cr_assert_eq(platform_temp_file_open("json-log", fixture_path, &fixture_fd), ASCIICHAT_OK);
  fixture_stream = platform_fdopen("json-log", fixture_fd, "w+");
  cr_assert_not_null(fixture_stream);
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

TestSuite(json_log, .init = setup, .fini = cleanup);

static yyjson_val *read_record(void) {
  char record[8192];
  cr_assert_not_null(fgets(record, sizeof(record), fixture_stream));
  size_t length = strlen(record);
  cr_assert_gt(length, 1);
  cr_assert_eq(record[length - 1], '\n', "each record must end with one newline");
  if (parsed)
    yyjson_doc_free(parsed);
  parsed = yyjson_read(record, length, 0);
  cr_assert_not_null(parsed, "log output must be valid standalone JSON: %s", record);
  return yyjson_doc_get_root(parsed);
}

Test(json_log, structured_records_preserve_escaped_values_and_optional_metadata) {
  const char message[] = "quote \" slash \\ newline\ncarriage\rtab\tcontrol\x01 unicode \xc3\xa9";
  const uint64_t timestamp = UINT64_C(1700000000123456789);
  log_json_write(fixture_fd, LOG_WARN, timestamp, "lib/log/json.c", 42, "test_json", message);
  log_json_write(fixture_fd, LOG_INFO, timestamp, NULL, 0, NULL, "second record");
  rewind(fixture_stream);
  yyjson_val *root = read_record();
  yyjson_val *header = yyjson_obj_get(root, "header");
  cr_assert_str_eq(yyjson_get_str(yyjson_obj_get(header, "level")), "WARN");
  cr_assert_str_eq(yyjson_get_str(yyjson_obj_get(header, "file")), "lib/log/json.c");
  cr_assert_eq(yyjson_get_int(yyjson_obj_get(header, "line")), 42);
  cr_assert_str_eq(yyjson_get_str(yyjson_obj_get(header, "func")), "test_json");
  cr_assert(yyjson_is_uint(yyjson_obj_get(header, "tid")));
  const char *time = yyjson_get_str(yyjson_obj_get(header, "timestamp"));
  cr_assert_not_null(time);
  cr_assert_eq(strlen(time), 15);
  cr_assert_str_eq(time + 8, ".123456", "timestamp must retain microseconds");
  cr_assert_str_eq(yyjson_get_str(yyjson_obj_get(yyjson_obj_get(root, "body"), "message")), message);
  root = read_record();
  header = yyjson_obj_get(root, "header");
  cr_assert_null(yyjson_obj_get(header, "file"));
  cr_assert_null(yyjson_obj_get(header, "line"));
  cr_assert_null(yyjson_obj_get(header, "func"));
  cr_assert_str_eq(yyjson_get_str(yyjson_obj_get(yyjson_obj_get(root, "body"), "message")), "second record");
  cr_assert_eq(fgetc(fixture_stream), EOF);
}

Test(json_log, async_records_escape_controls_and_remain_one_line) {
  const char message[] = "\"\\\n\r\t\x01 text";
  log_json_async_safe(fixture_fd, LOG_ERROR, "lib/quoted\"file.c", 9, "function\\name", message);
  log_json_async_safe(fixture_fd, LOG_FATAL, NULL, 0, NULL, "");
  rewind(fixture_stream);
  yyjson_val *root = read_record();
  yyjson_val *header = yyjson_obj_get(root, "header");
  cr_assert_str_eq(yyjson_get_str(yyjson_obj_get(header, "level")), "ERROR");
  cr_assert_str_eq(yyjson_get_str(yyjson_obj_get(header, "file")), "lib/quoted\"file.c");
  cr_assert_str_eq(yyjson_get_str(yyjson_obj_get(header, "func")), "function\\name");
  cr_assert_str_eq(yyjson_get_str(yyjson_obj_get(yyjson_obj_get(root, "body"), "message")), "\"\\\n\r\t text");
  root = read_record();
  header = yyjson_obj_get(root, "header");
  cr_assert_str_eq(yyjson_get_str(yyjson_obj_get(header, "file")), "");
  cr_assert_str_eq(yyjson_get_str(yyjson_obj_get(header, "func")), "");
  cr_assert_str_eq(yyjson_get_str(yyjson_obj_get(header, "level")), "FATAL");
  cr_assert_eq(fgetc(fixture_stream), EOF);
}

Test(json_log, async_truncation_does_not_split_escape_sequences) {
  char message[700];
  memset(message, 'x', sizeof(message));
  message[510] = '"';
  message[sizeof(message) - 1] = '\0';
  log_json_async_safe(fixture_fd, LOG_DEBUG, NULL, 1, NULL, message);
  rewind(fixture_stream);
  yyjson_val *root = read_record();
  const char *actual = yyjson_get_str(yyjson_obj_get(yyjson_obj_get(root, "body"), "message"));
  cr_assert_not_null(actual);
  cr_assert_eq(strlen(actual), 510);
  cr_assert_eq(memcmp(actual, message, 510), 0);
}

Test(json_log, async_truncation_keeps_complete_utf8_characters) {
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
    cr_assert_not_null(actual);
    cr_assert_eq(strlen(actual), 510, "a character that cannot fit must be omitted completely");
    for (size_t j = 0; j < 510; ++j)
      cr_assert_eq(actual[j], 'x');
    root = read_record();
    actual = yyjson_get_str(yyjson_obj_get(yyjson_obj_get(root, "body"), "message"));
    cr_assert_not_null(actual);
    cr_assert_eq(strlen(actual), 511);
    cr_assert_str_eq(actual + 511 - strlen(characters[i]), characters[i]);
  }
}

Test(json_log, level_names_and_invalid_inputs) {
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
    cr_assert_str_eq(yyjson_get_str(yyjson_obj_get(yyjson_obj_get(root, "header"), "level")), names[i]);
  }
  cr_assert_eq(fgetc(fixture_stream), EOF);
}
