/**
 * @file lib/log/format.c
 * @brief Log format parser implementation
 * @ingroup logging
 */

#include <ascii-chat/log/format.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/* ============================================================================
 * Default Format String (matches hardcoded format)
 * ============================================================================ */

/**
 * @brief Default log format that matches the original hardcoded format
 *
 * This format produces output identical to what was hardcoded before custom
 * formatting support. Used when no --log-format is specified.
 */
static const char DEFAULT_FORMAT[] = "[%time(%H:%M:%S)] [%level_aligned] %message";

const char *log_format_default(void) {
  return DEFAULT_FORMAT;
}

/* ============================================================================
 * Format Parser Implementation
 * ============================================================================ */

/**
 * @brief Parse format string and extract specifiers
 *
 * Tokenizes format string character by character, identifying:
 * - Literal text (copied as-is)
 * - Format specifiers like %time(), %level, %message, etc.
 * - Escaped %% (becomes literal %)
 * - Platform-aware \n (becomes newline)
 *
 * @param format_str Format string to parse
 * @param console_only Applied-to-console-only flag
 * @return Compiled format, or NULL on error
 */
static log_format_t *parse_format_string(const char *format_str, bool console_only) {
  if (!format_str) {
    return NULL;
  }

  /* Validate UTF-8 in format string */
  if (!utf8_is_valid(format_str)) {
    log_error("Invalid UTF-8 in log format string");
    return NULL;
  }

  log_format_t *result = SAFE_CALLOC(1, sizeof(log_format_t), log_format_t *);
  if (!result) {
    return NULL;
  }

  result->original = SAFE_MALLOC(strlen(format_str) + 1, char *);
  if (!result->original) {
    SAFE_FREE(result);
    return NULL;
  }
  strcpy(result->original, format_str);
  result->console_only = console_only;

  /* Pre-allocate spec array (worst case: every char is a specifier)
   * Use CALLOC to zero-initialize to ensure all fields are NULL/0 */
  result->specs = SAFE_CALLOC(strlen(format_str) + 1, sizeof(log_format_spec_t), log_format_spec_t *);
  if (!result->specs) {
    SAFE_FREE(result->original);
    SAFE_FREE(result);
    return NULL;
  }

  const char *p = format_str;
  size_t spec_idx = 0;

  while (*p) {
    /* Check for escape sequences first */
    if (*p == '\\' && *(p + 1)) {
      char next = *(p + 1);

      if (next == 'n') {
        /* \n - platform-aware newline */
        result->specs[spec_idx].type = LOG_FORMAT_NEWLINE;
        spec_idx++;
        p += 2;
      } else if (next == '\\') {
        /* \\ - escaped backslash (output single \) */
        result->specs[spec_idx].type = LOG_FORMAT_LITERAL;
        result->specs[spec_idx].literal = SAFE_MALLOC(2, char *);
        if (!result->specs[spec_idx].literal) {
          goto cleanup;
        }
        result->specs[spec_idx].literal[0] = '\\';
        result->specs[spec_idx].literal[1] = '\0';
        result->specs[spec_idx].literal_len = 1;
        spec_idx++;
        p += 2;
      } else {
        /* Invalid escape sequence - treat backslash as literal */
        result->specs[spec_idx].type = LOG_FORMAT_LITERAL;
        result->specs[spec_idx].literal = SAFE_MALLOC(2, char *);
        if (!result->specs[spec_idx].literal) {
          goto cleanup;
        }
        result->specs[spec_idx].literal[0] = '\\';
        result->specs[spec_idx].literal[1] = '\0';
        result->specs[spec_idx].literal_len = 1;
        spec_idx++;
        p++;
      }
    } else if (*p == '%' && *(p + 1)) {
      if (*(p + 1) == '%') {
        /* %% - escaped percent (output single %) */
        result->specs[spec_idx].type = LOG_FORMAT_LITERAL;
        result->specs[spec_idx].literal = SAFE_MALLOC(2, char *);
        if (!result->specs[spec_idx].literal) {
          goto cleanup;
        }
        result->specs[spec_idx].literal[0] = '%';
        result->specs[spec_idx].literal[1] = '\0';
        result->specs[spec_idx].literal_len = 1;
        spec_idx++;
        p += 2;
      } else {
        /* Start of format specifier */
        p++;

        if (strncmp(p, "time(", 5) == 0) {
          /* Parse %time(format) */
          p += 5;
          const char *fmt_start = p;
          const char *fmt_end = strchr(p, ')');
          if (!fmt_end) {
            /* Parse error: unterminated time format */
            log_error("Invalid %%time format: missing closing )");
            goto cleanup;
          }

          result->specs[spec_idx].type = LOG_FORMAT_TIME;
          size_t fmt_len = fmt_end - fmt_start;
          result->specs[spec_idx].literal = SAFE_MALLOC(fmt_len + 1, char *);
          if (!result->specs[spec_idx].literal) {
            goto cleanup;
          }
          memcpy(result->specs[spec_idx].literal, fmt_start, fmt_len);
          result->specs[spec_idx].literal[fmt_len] = '\0';
          result->specs[spec_idx].literal_len = fmt_len;

          spec_idx++;
          p = fmt_end + 1;
        } else if (strncmp(p, "level_aligned", 13) == 0) {
          result->specs[spec_idx].type = LOG_FORMAT_LEVEL_ALIGNED;
          spec_idx++;
          p += 13;
        } else if (strncmp(p, "level", 5) == 0) {
          result->specs[spec_idx].type = LOG_FORMAT_LEVEL;
          spec_idx++;
          p += 5;
        } else if (strncmp(p, "file", 4) == 0) {
          result->specs[spec_idx].type = LOG_FORMAT_FILE;
          spec_idx++;
          p += 4;
        } else if (strncmp(p, "line", 4) == 0) {
          result->specs[spec_idx].type = LOG_FORMAT_LINE;
          spec_idx++;
          p += 4;
        } else if (strncmp(p, "func", 4) == 0) {
          result->specs[spec_idx].type = LOG_FORMAT_FUNC;
          spec_idx++;
          p += 4;
        } else if (strncmp(p, "tid", 3) == 0) {
          result->specs[spec_idx].type = LOG_FORMAT_TID;
          spec_idx++;
          p += 3;
        } else if (strncmp(p, "message", 7) == 0) {
          result->specs[spec_idx].type = LOG_FORMAT_MESSAGE;
          spec_idx++;
          p += 7;
        } else if (strncmp(p, "colorlog_level_string_to_color", 30) == 0) {
          result->specs[spec_idx].type = LOG_FORMAT_COLORLOG_LEVEL;
          spec_idx++;
          p += 30;
        } else {
          /* Unknown specifier */
          log_error("Unknown format specifier: %%%s", p);
          goto cleanup;
        }
      }
    } else {
      /* Literal text until next escape or specifier */
      const char *text_start = p;
      while (*p && *p != '\\' && *p != '%') {
        p++;
      }

      result->specs[spec_idx].type = LOG_FORMAT_LITERAL;
      size_t text_len = p - text_start;
      result->specs[spec_idx].literal = SAFE_MALLOC(text_len + 1, char *);
      if (!result->specs[spec_idx].literal) {
        goto cleanup;
      }

      /* Safe memcpy for UTF-8 literal text (already validated) */
      memcpy(result->specs[spec_idx].literal, text_start, text_len);
      result->specs[spec_idx].literal[text_len] = '\0';
      result->specs[spec_idx].literal_len = text_len;
      spec_idx++;
    }
  }

  result->spec_count = spec_idx;
  return result;

cleanup:
  log_format_free(result);
  return NULL;
}

log_format_t *log_format_parse(const char *format_str, bool console_only) {
  return parse_format_string(format_str, console_only);
}

void log_format_free(log_format_t *format) {
  if (!format) {
    return;
  }

  if (format->specs) {
    for (size_t i = 0; i < format->spec_count; i++) {
      if (format->specs[i].literal) {
        SAFE_FREE(format->specs[i].literal);
      }
    }
    SAFE_FREE(format->specs);
  }

  if (format->original) {
    SAFE_FREE(format->original);
  }

  SAFE_FREE(format);
}

/* ============================================================================
 * Format Application Implementation
 * ============================================================================ */

/**
 * @brief Get padded level string for consistent alignment
 *
 * Returns level names padded to 5 characters for visual alignment.
 *
 * @param level Log level
 * @return Padded level string (e.g., "INFO ", "WARN ", "DEBUG")
 */
// get_level_string_padded is declared in logging.h and defined in logging.c

/**
 * @brief Get unpadded level string
 *
 * @param level Log level
 * @return Level string without padding
 */
static const char *get_level_string(log_level_t level) {
  switch (level) {
  case LOG_DEV:
    return "DEV";
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
    return "";
  }
}

int log_format_apply(const log_format_t *format, char *buf, size_t buf_size, log_level_t level, const char *timestamp,
                     const char *file, int line, const char *func, uint64_t tid, const char *message, bool use_colors) {
  if (!format || !buf || buf_size == 0) {
    return -1;
  }

  int total_written = 0;
  char *p = buf;
  size_t remaining = buf_size - 1; /* Reserve space for null terminator */

  (void)timestamp; /* May be used in future; for now, LOG_FORMAT_TIME uses custom formatting */

  for (size_t i = 0; i < format->spec_count; i++) {
    const log_format_spec_t *spec = &format->specs[i];
    int written = 0;

    switch (spec->type) {
    case LOG_FORMAT_LITERAL:
      written = safe_snprintf(p, remaining + 1, "%s", spec->literal ? spec->literal : "");
      break;

    case LOG_FORMAT_TIME:
      /* Format time using custom time formatter */
      if (spec->literal) {
        written = time_format_now(spec->literal, p, remaining + 1);
        if (written <= 0) {
          log_debug("time_format_now failed for format: %s", spec->literal);
          written = 0;
        }
      }
      break;

    case LOG_FORMAT_LEVEL:
      /* Log level as string (DEV, DEBUG, INFO, WARN, ERROR, FATAL) */
      if (use_colors) {
        const char *colored = colored_string((log_color_t)level, get_level_string(level));
        written = safe_snprintf(p, remaining + 1, "%s", colored);
      } else {
        written = safe_snprintf(p, remaining + 1, "%s", get_level_string(level));
      }
      break;

    case LOG_FORMAT_LEVEL_ALIGNED:
      /* Log level padded to 5 characters */
      if (use_colors) {
        const char *colored = colored_string((log_color_t)level, get_level_string_padded(level));
        written = safe_snprintf(p, remaining + 1, "%s", colored);
      } else {
        written = safe_snprintf(p, remaining + 1, "%s", get_level_string_padded(level));
      }
      break;

    case LOG_FORMAT_FILE:
      if (file) {
        written = safe_snprintf(p, remaining + 1, "%s", file);
      }
      break;

    case LOG_FORMAT_LINE:
      if (line > 0) {
        written = safe_snprintf(p, remaining + 1, "%d", line);
      }
      break;

    case LOG_FORMAT_FUNC:
      if (func) {
        written = safe_snprintf(p, remaining + 1, "%s", func);
      }
      break;

    case LOG_FORMAT_TID:
      written = safe_snprintf(p, remaining + 1, "%llu", (unsigned long long)tid);
      break;

    case LOG_FORMAT_MESSAGE:
      if (message) {
        written = safe_snprintf(p, remaining + 1, "%s", message);
      }
      break;

    case LOG_FORMAT_COLORLOG_LEVEL:
      /* Color code for the log level (placeholder for future color support) */
      (void)use_colors; /* Suppress unused parameter warning */
      written = 0;
      break;

    case LOG_FORMAT_NEWLINE:
      /* Platform-aware newline */
#ifdef _WIN32
      written = safe_snprintf(p, remaining + 1, "\r\n");
#else
      written = safe_snprintf(p, remaining + 1, "\n");
#endif
      break;

    default:
      break;
    }

    if (written < 0) {
      /* snprintf error */
      return -1;
    }

    if ((size_t)written > remaining) {
      /* Buffer overflow prevention */
      log_debug("log_format_apply: buffer overflow prevented");
      return -1;
    }

    p += written;
    remaining -= written;
    total_written += written;
  }

  *p = '\0';
  return total_written;
}
