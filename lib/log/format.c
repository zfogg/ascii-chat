/**
 * @file lib/log/format.c
 * @brief Log format parser implementation
 * @ingroup logging
 */

#include <ascii-chat/log/format.h>
#include <ascii-chat/log/colorize.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/debug/named.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/* ============================================================================
 * Default Format String (mode-aware)
 * ============================================================================ */

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
static log_template_t *parse_format_string(const char *format_str, bool console_only) {
  if (!format_str) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid format string: %s", format_str);
    return NULL;
  }

  bool is_valid = utf8_is_valid(format_str);

  if (!is_valid) {
    SET_ERRNO(ERROR_INVALID_STATE, "Invalid UTF-8 in log format string");
    return NULL;
  }

  // Use regular malloc/calloc instead of SAFE_* to avoid debug tracking overhead at startup
  // This avoids mutex contention and memory tracking overhead during log initialization
  log_template_t *result = (log_template_t *)calloc(1, sizeof(log_template_t));
  if (!result) {
    return NULL;
  }

  result->original = (char *)malloc(strlen(format_str) + 1);
  if (!result->original) {
    free(result);
    return NULL;
  }
  strcpy(result->original, format_str);
  result->console_only = console_only;

  /* Pre-allocate spec array (worst case: every char is a specifier) */
  result->specs = (log_format_spec_t *)calloc(strlen(format_str) + 1, sizeof(log_format_spec_t));
  if (!result->specs) {
    free(result->original);
    free(result);
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
        result->specs[spec_idx].literal = (char *)malloc(2);
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
        result->specs[spec_idx].literal = (char *)malloc(2);
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
        result->specs[spec_idx].literal = (char *)malloc(2);
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
          result->specs[spec_idx].literal = (char *)malloc(fmt_len + 1);
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
        } else if (strncmp(p, "file_relative", 13) == 0) {
          result->specs[spec_idx].type = LOG_FORMAT_FILE_RELATIVE;
          spec_idx++;
          p += 13;
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
        } else if (strncmp(p, "tname", 5) == 0) {
          result->specs[spec_idx].type = LOG_FORMAT_TNAME;
          spec_idx++;
          p += 5;
        } else if (strncmp(p, "tid", 3) == 0) {
          result->specs[spec_idx].type = LOG_FORMAT_TID;
          spec_idx++;
          p += 3;
        } else if (strncmp(p, "colored_message", 15) == 0) {
          result->specs[spec_idx].type = LOG_FORMAT_COLORED_MESSAGE;
          spec_idx++;
          p += 15;
        } else if (strncmp(p, "ms", 2) == 0) {
          result->specs[spec_idx].type = LOG_FORMAT_MICROSECONDS;
          spec_idx++;
          p += 2;
        } else if (strncmp(p, "ns", 2) == 0) {
          result->specs[spec_idx].type = LOG_FORMAT_NANOSECONDS;
          spec_idx++;
          p += 2;
        } else if (strncmp(p, "message", 7) == 0) {
          result->specs[spec_idx].type = LOG_FORMAT_MESSAGE;
          spec_idx++;
          p += 7;
        } else if (strncmp(p, "colorlog_level_string_to_color", 30) == 0) {
          result->specs[spec_idx].type = LOG_FORMAT_COLORLOG_LEVEL;
          spec_idx++;
          p += 30;
        } else if (strncmp(p, "color(", 6) == 0) {
          /* Parse %color(LEVEL, content) */
          p += 6; /* Skip "color(" */

          /* Find the matching closing paren */
          int paren_depth = 1;
          const char *color_start = p;
          while (*p && paren_depth > 0) {
            if (*p == '(')
              paren_depth++;
            else if (*p == ')')
              paren_depth--;
            if (paren_depth > 0)
              p++;
          }

          if (!*p || paren_depth != 0) {
            /* Parse error: unterminated color format */
            log_error("Invalid %%color format: missing closing )");
            goto cleanup;
          }

          size_t color_arg_len = p - color_start;

          /* Store as "LEVEL|content" for later parsing */
          result->specs[spec_idx].type = LOG_FORMAT_COLOR;
          result->specs[spec_idx].literal = (char *)malloc(color_arg_len + 1);
          if (!result->specs[spec_idx].literal) {
            goto cleanup;
          }
          memcpy(result->specs[spec_idx].literal, color_start, color_arg_len);
          result->specs[spec_idx].literal[color_arg_len] = '\0';
          result->specs[spec_idx].literal_len = color_arg_len;

          spec_idx++;
          p++; /* Skip closing paren */
        } else {
          /* Unknown ascii-chat specifier - treat as strftime format code and pass to strftime
           * This allows strftime formats like %H, %M, %S, %A, %B, etc. to work
           * without needing custom parsing for each one. strftime will handle validation. */
          const char *fmt_start = p;
          size_t fmt_len = 0;

          /* Collect the format code (usually 1-2 chars, but allow flexibility) */
          while (*p && *p != '%' && *p != '\\' && fmt_len < 8) {
            p++;
            fmt_len++;
          }

          if (fmt_len == 0) {
            log_error("Empty format specifier: %%");
            goto cleanup;
          }

          result->specs[spec_idx].type = LOG_FORMAT_STRFTIME_CODE;
          result->specs[spec_idx].literal = (char *)malloc(fmt_len + 1);
          if (!result->specs[spec_idx].literal) {
            goto cleanup;
          }
          memcpy(result->specs[spec_idx].literal, fmt_start, fmt_len);
          result->specs[spec_idx].literal[fmt_len] = '\0';
          result->specs[spec_idx].literal_len = fmt_len;

          spec_idx++;
          /* p already advanced in the while loop above */
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
      result->specs[spec_idx].literal = (char *)malloc(text_len + 1);
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
  log_template_free(result);
  return NULL;
}

log_template_t *log_template_parse(const char *format_str, bool console_only) {
  return parse_format_string(format_str, console_only);
}

void log_template_free(log_template_t *format) {
  if (!format) {
    SET_ERRNO(ERROR_INVALID_PARAM, "null format");
    return;
  }

  if (format->specs) {
    for (size_t i = 0; i < format->spec_count; i++) {
      if (format->specs[i].literal) {
        free(format->specs[i].literal);
      }
    }
    free(format->specs);
  }

  if (format->original) {
    free(format->original);
  }

  free(format);
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

/**
 * @brief Parse a level name string to log_color_t
 *
 * Maps level names like "INFO", "DEBUG", "ERROR" to their corresponding
 * log_color_t values. Special cases:
 * - "*" means use the current log level
 * - "GREY" maps to LOG_COLOR_GREY
 *
 * @param level_name String like "INFO", "DEBUG", "ERROR", "*", "GREY", etc.
 * @param current_level Current log level (used when level_name is "*")
 * @return log_color_t value, or LOG_COLOR_RESET if not recognized
 */
static log_color_t parse_color_level(const char *level_name, log_level_t current_level) {
  if (!level_name) {
    return LOG_COLOR_RESET;
  }

  /* Special case: "*" means use the current log level */
  if (strcmp(level_name, "*") == 0) {
    switch (current_level) {
    case LOG_DEV:
      return LOG_COLOR_DEV;
    case LOG_DEBUG:
      return LOG_COLOR_DEBUG;
    case LOG_INFO:
      return LOG_COLOR_INFO;
    case LOG_WARN:
      return LOG_COLOR_WARN;
    case LOG_ERROR:
      return LOG_COLOR_ERROR;
    case LOG_FATAL:
      return LOG_COLOR_FATAL;
    default:
      return LOG_COLOR_RESET;
    }
  }

  /* Log level names */
  if (strcmp(level_name, "DEV") == 0) {
    return LOG_COLOR_DEV;
  } else if (strcmp(level_name, "DEBUG") == 0) {
    return LOG_COLOR_DEBUG;
  } else if (strcmp(level_name, "INFO") == 0) {
    return LOG_COLOR_INFO;
  } else if (strcmp(level_name, "WARN") == 0) {
    return LOG_COLOR_WARN;
  } else if (strcmp(level_name, "ERROR") == 0) {
    return LOG_COLOR_ERROR;
  } else if (strcmp(level_name, "FATAL") == 0) {
    return LOG_COLOR_FATAL;
  }

  /* Literal color names */
  if (strcmp(level_name, "GREY") == 0 || strcmp(level_name, "GRAY") == 0) {
    return LOG_COLOR_GREY;
  }

  return LOG_COLOR_RESET;
}

/**
 * @brief Recursively render format content (internal helper)
 *
 * Renders content string which may contain format specifiers like %tid, %level, etc.
 * This is used for the content part of %color(LEVEL, content).
 *
 * @param content Format string to render (e.g., "tid:%tid" or just "%tid")
 * @param buf Output buffer
 * @param buf_size Output buffer size
 * @param level Log level
 * @param timestamp Pre-formatted timestamp string
 * @param file Source file name (or NULL)
 * @param line Source line number (or 0)
 * @param func Function name (or NULL)
 * @param tid Thread ID
 * @param message Log message text
 * @param use_colors If true, apply ANSI color codes
 * @param time_nanoseconds Raw wall-clock time in nanoseconds
 * @return Number of characters written (excluding null terminator), or -1 on error
 */
static int render_format_content(const char *content, char *buf, size_t buf_size, log_level_t level,
                                 const char *timestamp, const char *file, int line, const char *func, uint64_t tid,
                                 const char *message, bool use_colors, uint64_t time_nanoseconds) {
  if (!content || !buf || buf_size == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid arguments: content=%p, buf=%p, buf_size=%zu", content, buf, buf_size);
    return -1;
  }

  /* Parse content as a format string and apply it */
  log_template_t *content_format = log_template_parse(content, false);
  if (!content_format) {
    return -1;
  }

  int written = log_template_apply(content_format, buf, buf_size, level, timestamp, file, line, func, tid, message,
                                   use_colors, time_nanoseconds);
  log_template_free(content_format);

  return written;
}

int log_template_apply(const log_template_t *format, char *buf, size_t buf_size, log_level_t level,
                       const char *timestamp, const char *file, int line, const char *func, uint64_t tid,
                       const char *message, bool use_colors, uint64_t time_nanoseconds) {
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
      written = safe_snprintf(p, remaining + 1, "%s", get_level_string(level));
      break;

    case LOG_FORMAT_LEVEL_ALIGNED:
      /* Log level padded to exactly 5 characters (prevents truncation to [DEBU]/[ERRO]) */
      written = safe_snprintf(p, remaining + 1, "%s", get_level_string_padded(level));
      break;

    case LOG_FORMAT_FILE:
      if (file) {
        written = safe_snprintf(p, remaining + 1, "%s", file);
      }
      break;

    case LOG_FORMAT_FILE_RELATIVE:
      if (file) {
        const char *rel_file = extract_project_relative_path(file);
        written = safe_snprintf(p, remaining + 1, "%s", rel_file);
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

    case LOG_FORMAT_TNAME: {
      asciichat_thread_t thread = (asciichat_thread_t)tid;
      uintptr_t key = asciichat_thread_to_key(thread);
      const char *tname = named_get(key);
      if (tname) {
        written = safe_snprintf(p, remaining + 1, "%s", tname);
      } else {
        written = safe_snprintf(p, remaining + 1, "%llu", (unsigned long long)tid);
      }
      break;
    }

    case LOG_FORMAT_MICROSECONDS: {
      /* Extract microseconds from nanoseconds (ns_value % 1_000_000_000 / 1000) */
      long nanoseconds = (long)(time_nanoseconds % NS_PER_SEC_INT);
      long microseconds = nanoseconds / 1000;
      if (microseconds < 0)
        microseconds = 0;
      if (microseconds > 999999)
        microseconds = 999999;
      written = safe_snprintf(p, remaining + 1, "%06ld", microseconds);
      break;
    }

    case LOG_FORMAT_NANOSECONDS: {
      /* Extract nanoseconds component (ns_value % 1_000_000_000) */
      long nanoseconds = (long)(time_nanoseconds % NS_PER_SEC_INT);
      if (nanoseconds < 0)
        nanoseconds = 0;
      if (nanoseconds > 999999999)
        nanoseconds = 999999999;
      written = safe_snprintf(p, remaining + 1, "%09ld", nanoseconds);
      break;
    }

    case LOG_FORMAT_STRFTIME_CODE: {
      /* strftime format code (like %H, %M, %S, %A, %B, etc.)
       * Let strftime handle all the parsing and validation */
      if (spec->literal) {
        /* Construct format string with % prefix */
        size_t fmt_len = spec->literal_len + 1;
        char *format_str = (char *)malloc(fmt_len + 1);
        if (format_str) {
          format_str[0] = '%';
          memcpy(format_str + 1, spec->literal, spec->literal_len);
          format_str[fmt_len] = '\0';
          written = time_format_now(format_str, p, remaining + 1);
          if (written <= 0) {
            log_debug("time_format_now failed for format code: %s", format_str);
            written = 0;
          }
          SAFE_FREE(format_str);
        }
      }
      break;
    }

    case LOG_FORMAT_MESSAGE:
      if (message) {
        written = safe_snprintf(p, remaining + 1, "%s", message);
      }
      break;

    case LOG_FORMAT_COLORED_MESSAGE: {
      /* Apply colorize_log_message() for number/unit/hex highlighting (keeps text white) */
      if (message) {
        const char *colorized_msg = colorize_log_message(message);
        written = safe_snprintf(p, remaining + 1, "%s", colorized_msg);
      }
      break;
    }

    case LOG_FORMAT_COLORLOG_LEVEL:
      /* Color code for the log level (placeholder for future color support) */
      (void)use_colors; /* Suppress unused parameter warning */
      written = 0;
      break;

    case LOG_FORMAT_COLOR: {
      /* Parse and apply %color(LEVEL, content)
       * literal stores "LEVEL,content" where LEVEL is the color level name (or "*" for current level)
       * and content is a format string to render and colorize */
      if (!spec->literal || spec->literal_len == 0) {
        written = 0;
        break;
      }

      /* Find the comma separating LEVEL and content */
      const char *comma_pos = strchr(spec->literal, ',');
      if (!comma_pos) {
        /* Invalid format - no comma found */
        log_debug("log_template_apply: %%color format missing comma in: %s", spec->literal);
        written = 0;
        break;
      }

      /* Extract level name (before comma) */
      size_t level_len = comma_pos - spec->literal;
      char level_name[32];
      if (level_len >= sizeof(level_name)) {
        /* Level name too long */
        written = 0;
        break;
      }
      memcpy(level_name, spec->literal, level_len);
      level_name[level_len] = '\0';

      /* Parse level name to log_color_t (pass current level for "*" support) */
      log_color_t color = parse_color_level(level_name, level);

      /* Extract content (after comma), skip leading whitespace */
      const char *content_start = comma_pos + 1;
      while (*content_start == ' ' || *content_start == '\t') {
        content_start++;
      }

      /* Render content recursively */
      char content_buf[512];
      int content_len = render_format_content(content_start, content_buf, sizeof(content_buf), level, timestamp, file,
                                              line, func, tid, message, use_colors, time_nanoseconds);

      if (content_len < 0 || content_len >= (int)sizeof(content_buf)) {
        written = 0;
        break;
      }

      /* Apply color to rendered content using colored_string */
      const char *colored_content = colored_string(color, content_buf);

      /* Copy colored content to output buffer */
      written = safe_snprintf(p, remaining + 1, "%s", colored_content);
      break;
    }

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
      log_debug("log_template_apply: buffer overflow prevented");
      return -1;
    }

    p += written;
    remaining -= written;
    total_written += written;
  }

  *p = '\0';
  return total_written;
}
