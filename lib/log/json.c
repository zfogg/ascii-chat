/**
 * @file json.c
 * @ingroup logging
 * @brief 📝 JSON structured logging output using yyjson
 */

#include <ascii-chat/common.h>
#include <ascii-chat/log/json.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/util/time.h>
#include <yyjson.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * Note: log_set_json_output() is implemented in logging.c where the g_log
 * struct is defined.
 * ============================================================================ */

/**
 * @brief Format nanoseconds as HH:MM:SS.microseconds
 *
 * @param time_nanoseconds Nanoseconds since epoch
 * @param buf Output buffer
 * @param buf_size Buffer size (must be at least 32 bytes)
 * @return Number of characters written, or 0 on error
 */
static size_t format_timestamp_microseconds(uint64_t time_nanoseconds, char *buf, size_t buf_size) {
  if (buf_size < 32) {
    return 0;
  }

  /* Extract seconds and nanoseconds */
  time_t seconds = (time_t)(time_nanoseconds / NS_PER_SEC_INT);
  long nanoseconds = (long)(time_nanoseconds % NS_PER_SEC_INT);

  /* Convert seconds to struct tm */
  struct tm tm_info;
  platform_localtime(&seconds, &tm_info);

  /* Format the time part (HH:MM:SS) */
  size_t len = strftime(buf, buf_size, "%H:%M:%S", &tm_info);
  if (len == 0 || len >= buf_size) {
    return 0;
  }

  /* Convert nanoseconds to microseconds for display */
  long microseconds = nanoseconds / 1000;
  if (microseconds < 0) {
    microseconds = 0;
  }
  if (microseconds > 999999) {
    microseconds = 999999;
  }

  /* Append microseconds */
  int result = safe_snprintf(buf + len, buf_size - len, ".%06ld", microseconds);
  if (result < 0 || result >= (int)(buf_size - len)) {
    return 0;
  }

  return len + (size_t)result;
}

/**
 * @brief Convert log level to string
 *
 * @param level Log level
 * @return String representation of level
 */
static const char *log_level_to_string(log_level_t level) {
  switch (level) {
  case LOG_DEBUG:
    return "DEBUG";
  case LOG_INFO:
    return "INFO";
  case LOG_WARN:
    return "WARN";
  case LOG_ERROR:
    return "ERROR";
  case LOG_FATAL:
    return "FATAL";
  default:
    return "UNKNOWN";
  }
}

void log_json_write(int fd, log_level_t level, uint64_t time_nanoseconds, const char *file, int line, const char *func,
                    const char *message) {
  if (fd < 0 || !message) {
    return;
  }

  /* Create root object */
  yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
  if (!doc) {
    return;
  }

  yyjson_mut_val *root = yyjson_mut_obj(doc);
  if (!root) {
    yyjson_mut_doc_free(doc);
    return;
  }
  yyjson_mut_doc_set_root(doc, root);

  /* Create "header" object */
  yyjson_mut_val *header = yyjson_mut_obj(doc);
  if (header) {
    yyjson_mut_obj_add_val(doc, root, "header", header);

    /* Add timestamp (HH:MM:SS.microseconds) */
    char timestamp_buf[32];
    size_t ts_len = format_timestamp_microseconds(time_nanoseconds, timestamp_buf, sizeof(timestamp_buf));
    if (ts_len > 0) {
      yyjson_mut_obj_add_strncpy(doc, header, "timestamp", timestamp_buf, ts_len);
    }

    /* Add level */
    yyjson_mut_obj_add_str(doc, header, "level", log_level_to_string(level));

    /* Add thread ID */
    uint64_t tid = (uint64_t)asciichat_thread_current_id();
    yyjson_mut_obj_add_uint(doc, header, "tid", tid);

    /* Add file (if not NULL) - normalize to project-relative path */
    if (file) {
      // Import from path.h for relative path extraction
      extern const char *extract_project_relative_path(const char *file);
      const char *rel_file = extract_project_relative_path(file);
      yyjson_mut_obj_add_str(doc, header, "file", rel_file);
    }

    /* Add line (if > 0) */
    if (line > 0) {
      yyjson_mut_obj_add_int(doc, header, "line", line);
    }

    /* Add func (if not NULL) */
    if (func) {
      yyjson_mut_obj_add_str(doc, header, "func", func);
    }
  }

  /* Create "body" object with message */
  yyjson_mut_val *body = yyjson_mut_obj(doc);
  if (body) {
    yyjson_mut_obj_add_val(doc, root, "body", body);
    yyjson_mut_obj_add_str(doc, body, "message", message);
  }

  /* Serialize to compact JSON (single line, no pretty-printing) */
  size_t json_len = 0;
  char *json_str = yyjson_mut_write(doc, 0, &json_len);

  if (json_str && json_len > 0) {
    /* Write JSON string */
    platform_write(fd, (const uint8_t *)json_str, json_len);

    /* Write newline for NDJSON format */
    platform_write(fd, (const uint8_t *)"\n", 1);

    /* Free serialized JSON string */
    free(json_str);
  }

  /* Free document */
  yyjson_mut_doc_free(doc);
}
