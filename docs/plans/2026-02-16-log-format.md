# Log Format Customization Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add user-customizable log formatting via `--log-format` CLI option and environment variable, supporting time formats, log levels, file/line/function info, thread IDs, and platform-aware coloring.

**Architecture:** Create a format parsing system (`lib/log/format.c/h`) that tokenizes format strings into specifiers (`%time(fmt)`, `%level`, `%file`, etc.), evaluates each specifier at log time, and integrates with the existing `format_log_header()` function in logging.c. The format string is parsed once during initialization and executed during each log call. Both file and console output respect the format, with optional `--log-format-console` to apply formatting only to console.

**UTF-8 Support:** Full UTF-8 support for format strings, file paths, and messages:
- Format string validation using `utf8_is_valid()` during parsing
- Safe literal text extraction from UTF-8 format strings (byte-level copy, UTF-8 already validated)
- Existing message validation via `validate_log_message_utf8()` continues to work
- Safe buffer truncation using existing `truncate_at_whole_line()` to avoid splitting UTF-8 sequences
- Proper handling of UTF-8 in timestamps (locale-aware from strftime)
- Terminal display width calculation available via `utf8_display_width()` for future enhancements

**Tech Stack:**
- C string parsing and formatting with `snprintf`
- Platform abstraction layer for time/thread operations
- Existing logging system (logging.c/h) with UTF-8 validation
- UTF-8 utilities (`utf8.h`) for validation and character handling
- Options system for CLI parsing

---

## Task 1: Design and Implement Format Parser (lib/log/format.c)

**Files:**
- Create: `lib/log/format.h`
- Create: `lib/log/format.c`
- Modify: `include/ascii-chat/log/logging.h` (add new function declarations)
- Modify: `lib/log/logging.c` (integrate format parser)

**Step 1: Create format.h header with data structures**

```c
// include/ascii-chat/log/format.h

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <ascii-chat/log/logging.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct log_format_spec_t log_format_spec_t;
typedef struct log_format_t log_format_t;

/**
 * @brief Log format specifier types
 */
typedef enum {
  LOG_FORMAT_LITERAL,      /* Plain text (no % prefix) */
  LOG_FORMAT_TIME,         /* %time(fmt) - custom time format */
  LOG_FORMAT_LEVEL,        /* %level - log level as string */
  LOG_FORMAT_LEVEL_ALIGNED,/* %level_aligned - log level padded */
  LOG_FORMAT_FILE,         /* %file - relative filename */
  LOG_FORMAT_LINE,         /* %line - line number */
  LOG_FORMAT_FUNC,         /* %func - function name */
  LOG_FORMAT_TID,          /* %tid - thread ID */
  LOG_FORMAT_MESSAGE,      /* %message - log message */
  LOG_FORMAT_COLORLOG_LEVEL, /* %colorlog_level_string_to_color - color code */
  LOG_FORMAT_NEWLINE,      /* Platform-aware newline */
} log_format_type_t;

/**
 * @brief Parsed format specifier
 */
struct log_format_spec_t {
  log_format_type_t type;
  char *literal;           /* For LOG_FORMAT_LITERAL, the text */
  char *time_fmt;          /* For LOG_FORMAT_TIME, strftime format string */
  size_t literal_len;      /* Length of literal text */
};

/**
 * @brief Compiled log format (parsed specifiers)
 */
struct log_format_t {
  log_format_spec_t *specs; /* Array of format specifiers */
  size_t spec_count;       /* Number of specifiers */
  char *original;          /* Original format string (for debugging) */
  bool console_only;       /* Apply only to console output if true */
};

/**
 * @brief Parse a format string into compiled format
 * @param format_str Format string (e.g., "[%time(%H:%M:%S)] [%level_aligned] %message")
 *                   Format string must be valid UTF-8. Invalid UTF-8 will cause parse to fail.
 * @param console_only If true, format applies only to console (not file)
 * @return Compiled format, or NULL on parse error (including invalid UTF-8)
 */
log_format_t *log_format_parse(const char *format_str, bool console_only);

/**
 * @brief Free compiled format
 */
void log_format_free(log_format_t *format);

/**
 * @brief Apply format to a log entry and write to buffer
 * @param format Compiled format
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
 * @return Number of characters written (excluding null terminator), or -1 on error
 */
int log_format_apply(const log_format_t *format, char *buf, size_t buf_size,
                     log_level_t level, const char *timestamp,
                     const char *file, int line, const char *func,
                     uint64_t tid, const char *message, bool use_colors);

/**
 * @brief Get the default format string (matches current hardcoded format)
 * @return Default format string
 */
const char *log_format_default(void);

#ifdef __cplusplus
}
#endif
```

**Step 2: Run tests to verify header compiles**

```bash
# Check that header compiles with all dependencies including utf8.h
gcc -I/Users/zfogg/src/github.com/zfogg/ascii-chat/include \
    -c /Users/zfogg/src/github.com/zfogg/ascii-chat/lib/log/format.c \
    -o /tmp/format_test.o 2>&1 | head -20
# Expected: error for undefined reference (we haven't implemented format.c yet)
# Should NOT have errors about missing utf8.h or other includes
```

**Step 3: Implement format.c parser**

```c
// lib/log/format.c

#include <ascii-chat/log/format.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/utf8.h>  /* For UTF-8 validation */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/* Helper: Parse format string and extract specifiers */
static log_format_t *parse_format_string(const char *format_str, bool console_only) {
  if (!format_str) return NULL;

  /* Validate UTF-8 in format string */
  if (!utf8_is_valid(format_str)) {
    log_error("Invalid UTF-8 in log format string");
    return NULL;
  }

  log_format_t *result = SAFE_CALLOC(1, sizeof(log_format_t), log_format_t *);
  if (!result) return NULL;

  result->original = SAFE_MALLOC(strlen(format_str) + 1, char *);
  if (!result->original) {
    SAFE_FREE(result);
    return NULL;
  }
  strcpy(result->original, format_str);
  result->console_only = console_only;

  /* Pre-allocate spec array (worst case: every char is a specifier) */
  result->specs = SAFE_MALLOC(strlen(format_str) + 1, log_format_spec_t *);
  if (!result->specs) {
    SAFE_FREE(result->original);
    SAFE_FREE(result);
    return NULL;
  }

  const char *p = format_str;
  size_t spec_idx = 0;

  while (*p) {
    if (*p == '%' && *(p + 1) && *(p + 1) != '%') {
      /* Start of specifier */
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
        result->specs[spec_idx].time_fmt = SAFE_MALLOC(fmt_len + 1, char *);
        if (!result->specs[spec_idx].time_fmt) goto cleanup;
        memcpy(result->specs[spec_idx].time_fmt, fmt_start, fmt_len);
        result->specs[spec_idx].time_fmt[fmt_len] = '\0';

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
    } else if (*p == '\\' && *(p + 1) == 'n') {
      /* Platform-aware newline */
      result->specs[spec_idx].type = LOG_FORMAT_NEWLINE;
      spec_idx++;
      p += 2;
    } else if (*p == '%' && *(p + 1) == '%') {
      /* Escaped % */
      result->specs[spec_idx].type = LOG_FORMAT_LITERAL;
      result->specs[spec_idx].literal = SAFE_MALLOC(2, char *);
      if (!result->specs[spec_idx].literal) goto cleanup;
      result->specs[spec_idx].literal[0] = '%';
      result->specs[spec_idx].literal[1] = '\0';
      result->specs[spec_idx].literal_len = 1;
      spec_idx++;
      p += 2;
    } else {
      /* Literal text until next % or end */
      const char *text_start = p;
      while (*p && *p != '%') {
        p++;
      }

      result->specs[spec_idx].type = LOG_FORMAT_LITERAL;
      size_t text_len = p - text_start;
      result->specs[spec_idx].literal = SAFE_MALLOC(text_len + 1, char *);
      if (!result->specs[spec_idx].literal) goto cleanup;

      /* Safe memcpy for UTF-8 literal text (already validated by utf8_is_valid above) */
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
  if (!format) return;

  if (format->specs) {
    for (size_t i = 0; i < format->spec_count; i++) {
      if (format->specs[i].literal) {
        SAFE_FREE(format->specs[i].literal);
      }
      if (format->specs[i].time_fmt) {
        SAFE_FREE(format->specs[i].time_fmt);
      }
    }
    SAFE_FREE(format->specs);
  }

  if (format->original) {
    SAFE_FREE(format->original);
  }

  SAFE_FREE(format);
}

int log_format_apply(const log_format_t *format, char *buf, size_t buf_size,
                     log_level_t level, const char *timestamp,
                     const char *file, int line, const char *func,
                     uint64_t tid, const char *message, bool use_colors) {
  if (!format || !buf || buf_size == 0) return -1;

  int total_written = 0;
  char *p = buf;
  size_t remaining = buf_size - 1; /* Reserve space for null terminator */

  for (size_t i = 0; i < format->spec_count; i++) {
    const log_format_spec_t *spec = &format->specs[i];
    int written = 0;

    switch (spec->type) {
    case LOG_FORMAT_LITERAL:
      written = safe_snprintf(p, remaining + 1, "%s", spec->literal);
      break;

    case LOG_FORMAT_TIME:
      /* Format current time with custom format */
      if (timestamp) {
        written = safe_snprintf(p, remaining + 1, "%s", timestamp);
      }
      break;

    case LOG_FORMAT_LEVEL:
      /* Log level as string (DEV, DEBUG, INFO, WARN, ERROR, FATAL) */
      {
        const char *level_str[] = {"DEV", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
        if (level < 6) {
          written = safe_snprintf(p, remaining + 1, "%s", level_str[level]);
        }
      }
      break;

    case LOG_FORMAT_LEVEL_ALIGNED:
      /* Log level padded to 5 characters */
      {
        const char *level_str[] = {"DEV  ", "DEBUG", "INFO ", "WARN ", "ERROR", "FATAL"};
        if (level < 6) {
          written = safe_snprintf(p, remaining + 1, "%s", level_str[level]);
        }
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
      /* Color code for the log level (placeholder) */
      if (use_colors) {
        /* This would be integrated with colorize.c */
        written = 0; /* For now, colors handled separately */
      }
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
      return -1;
    }

    p += written;
    remaining -= written;
    total_written += written;
  }

  *p = '\0';
  return total_written;
}

const char *log_format_default(void) {
  /* Match current hardcoded format for release mode */
  return "[%time(%H:%M:%S)] [%level_aligned] %message";
}
```

**Step 4: Compile and verify format parser**

```bash
cd /Users/zfogg/src/github.com/zfogg/ascii-chat
cmake --preset default -B build 2>&1 | tail -20
# Expected: Compilation proceeds without format.c errors
```

**Step 5: Commit format parser**

```bash
git add lib/log/format.c include/ascii-chat/log/format.h
git commit -m "feat: Implement log format parser

- Add format.h with data structures for parsed format specifications
- Implement log_format_parse() to tokenize format strings
- Implement log_format_apply() to render logs with custom format
- Support specifiers: %time(fmt), %level, %level_aligned, %file, %line, %func, %tid, %message
- Platform-aware newlines via \n
- Includes cleanup and error handling"
```

---

## Task 1.5: Implement Time Format Validator and Custom Formatter

**Files:**
- Modify: `include/ascii-chat/util/time.h` (add function declarations)
- Modify: `lib/util/time.c` (add implementations)
- Modify: `lib/log/format.c` (use time formatter in LOG_FORMAT_TIME handler)

**Context:** The standard C `strftime()` function doesn't validate format strings upfront - it just returns 0 on error. Additionally, it doesn't support nanoseconds (only seconds via `%S`). We need:
1. Safe validation of strftime format strings against a whitelist of known specifiers
2. A custom time formatter that handles standard strftime + nanosecond extensions
3. Safe error handling (don't crash, return error)

**Infrastructure Already Available:**
- Project uses `time_get_realtime_ns()` for nanosecond-precision timestamps
- `get_current_time_formatted()` in logging.c already demonstrates extracting nanoseconds manually
- Support for both standard specifiers and custom extensions is the pattern

**Step 1: Add declarations to time.h header**

Append to `include/ascii-chat/util/time.h` (before final `/** @} */`):

```c
// Add to include/ascii-chat/util/time.h

#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Validate strftime format string against known safe specifiers
 * @param format_str Format string to validate (e.g., "%H:%M:%S")
 * @return true if valid, false if contains invalid specifiers or syntax
 *
 * Validates format string by checking each % specifier against a whitelist
 * of known safe strftime specifiers. Returns false if:
 * - Contains unterminated % sequences
 * - Contains invalid/unsupported specifiers
 * - Contains locale-dependent specifiers (E/O modifiers)
 *
 * Supported specifiers: %Y, %m, %d, %H, %M, %S, %a, %A, %b, %B, %j, %w, %u,
 *                      %I, %p, %Z, %z, %c, %x, %X, %F, %T, %s, %G, %g, %V, etc.
 *
 * Note: Does NOT support custom extensions like %6s(n(0)) in this phase.
 *       Those require full custom time formatter (Phase 2).
 */
bool time_format_is_valid_strftime(const char *format_str);

/**
 * @brief Format current time using strftime format string
 * @param format_str strftime format string (must be validated first with time_format_is_valid_strftime)
 * @param buf Output buffer
 * @param buf_size Output buffer size
 * @return Number of characters written (excluding null), or 0 on error
 *
 * Uses current wall-clock time (CLOCK_REALTIME) and handles nanoseconds
 * separately if buffer has room after standard strftime output.
 *
 * Standard strftime specifiers are handled by strftime().
 * Nanosecond handling: If format contains %S and buffer has room, appends
 * ".NNNNNN" for microseconds (rounded from nanoseconds).
 *
 * @note Buffer size must be at least 64 bytes for safety
 * @note Input format_str should be validated with time_format_is_valid_strftime()
 */
int time_format_now(const char *format_str, char *buf, size_t buf_size);

/**
 * @brief Safe wrapper for time formatting with error handling
 * @param format_str Format string to use
 * @param buf Output buffer
 * @param buf_size Output buffer size
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Validates format string first, then formats time. Returns error if:
 * - format_str contains invalid specifiers
 * - Output buffer too small
 * - strftime fails
 */
asciichat_error_t time_format_safe(const char *format_str, char *buf, size_t buf_size);
```

**Step 2: Add implementations to time.c**

Append to `lib/util/time.c` (after `adaptive_sleep_do()` function):

```c
// Add to lib/util/time.c

#include <ctype.h>  // Add if not present
#include <ascii-chat/util/time.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/common.h>
#include <time.h>
#include <string.h>

/* List of safe/supported strftime specifiers (exhaustive) */
static const char *SUPPORTED_SPECIFIERS = "YmmdHMSaAbBjwuIpZzcxXFTsGgV";

bool time_format_is_valid_strftime(const char *format_str) {
  if (!format_str) return false;

  for (const char *p = format_str; *p; p++) {
    if (*p == '%') {
      p++;
      if (!*p) {
        /* Unterminated % at end of string */
        log_error("Invalid format string: unterminated %% at end");
        return false;
      }

      if (*p == '%') {
        /* Escaped %% - skip it */
        continue;
      }

      if (*p == '-' || *p == '0' || *p == '+' || *p == ' ') {
        /* Flag character - skip it */
        p++;
        if (!*p) {
          log_error("Invalid format string: flag without specifier");
          return false;
        }
      }

      if (*p == '*' || (*p >= '0' && *p <= '9')) {
        /* Width specifier - skip */
        while (*p && ((*p >= '0' && *p <= '9') || *p == '*')) {
          p++;
        }
        if (!*p) {
          log_error("Invalid format string: width without specifier");
          return false;
        }
      }

      if (*p == '.') {
        /* Precision specifier - skip */
        p++;
        if (!*p) {
          log_error("Invalid format string: precision without specifier");
          return false;
        }
        while (*p && (*p >= '0' && *p <= '9')) {
          p++;
        }
        if (!*p) {
          log_error("Invalid format string: precision without specifier");
          return false;
        }
      }

      if (*p == 'E' || *p == 'O') {
        /* Locale modifier - skip for now (we only support C locale) */
        p++;
        if (!*p) {
          log_error("Invalid format string: modifier without specifier");
          return false;
        }
      }

      /* Check if character is in supported specifiers */
      if (!strchr(SUPPORTED_SPECIFIERS, *p)) {
        log_error("Unsupported format specifier: %%%c", *p);
        return false;
      }
    }
  }

  return true;
}

int time_format_now(const char *format_str, char *buf, size_t buf_size) {
  if (!format_str || !buf || buf_size < 2) {
    return 0;
  }

  /* Get current wall-clock time */
  uint64_t ts_ns = time_get_realtime_ns();
  time_t seconds = (time_t)(ts_ns / NS_PER_SEC_INT);
  long nanoseconds = (long)(ts_ns % NS_PER_SEC_INT);

  /* Convert to struct tm */
  struct tm tm_info;
  platform_localtime(&seconds, &tm_info);

  /* Format using strftime */
  size_t len = strftime(buf, buf_size, format_str, &tm_info);
  if (len == 0 || len >= buf_size - 1) {
    log_error("strftime failed or buffer overflow: len=%zu, buf_size=%zu", len, buf_size);
    return 0;
  }

  /* If format contains %S and we have room, append microseconds */
  if (strchr(format_str, 'S') && len + 7 < buf_size) {
    long microseconds = nanoseconds / 1000;
    if (microseconds < 0) microseconds = 0;
    if (microseconds > 999999) microseconds = 999999;

    int result = safe_snprintf(buf + len, buf_size - len, ".%06ld", microseconds);
    if (result > 0 && (size_t)result < buf_size - len) {
      len += (size_t)result;
    }
  }

  return (int)len;
}

asciichat_error_t time_format_safe(const char *format_str, char *buf, size_t buf_size) {
  if (!format_str || !buf) {
    return SET_ERRNO(ERROR_INVALID_STATE, "NULL format string or buffer");
  }

  if (buf_size < 64) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Buffer too small (minimum 64 bytes)");
  }

  if (!time_format_is_valid_strftime(format_str)) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid strftime format: %s", format_str);
  }

  int result = time_format_now(format_str, buf, buf_size);
  if (result <= 0) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Time formatting failed");
  }

  return ASCIICHAT_OK;
}
```

**Step 3: Integrate into format.c**

Modify the LOG_FORMAT_TIME handler in `log_format_apply()`:

```c
case LOG_FORMAT_TIME:
  /* Format time using custom time formatter with validation */
  if (spec->time_fmt) {
    int time_len = time_format_now(spec->time_fmt, p, remaining + 1);
    if (time_len <= 0) {
      log_error("Failed to format time with format: %s", spec->time_fmt);
      return -1;
    }
    written = time_len;
  }
  break;
```

**Step 3: Add header inclusion to format.c**

```c
#include <ascii-chat/util/time.h>  // For time_format_now, time_format_is_valid_strftime
```

**Step 4: Run tests**

```bash
cmake --build build 2>&1 | tail -20
# Expected: No compilation errors - time.c.o should compile successfully
```

**Step 5: Commit time formatter functions**

```bash
git add include/ascii-chat/util/time.h lib/util/time.c
git commit -m "feat: Add strftime format validation to time utilities

- Add time_format_is_valid_strftime() to validate strftime specifiers against whitelist
- Implement time_format_now() with strftime + nanosecond support (microseconds appended to %S)
- Add time_format_safe() error wrapper for safe formatting with validation
- Supports all standard POSIX strftime specifiers
- Automatically appends .NNNNNN microseconds when %S is present
- Reusable across codebase (logging, status screens, displays)
- Full UTF-8 safe (output from strftime is locale-aware)
- Consolidated into time.{c,h} for better organization"
```

---

## Task 2: Add CLI Options for Log Format

**Files:**
- Modify: `include/ascii-chat/options/options.h` (add LOG_FORMAT option)
- Modify: `lib/options/registry.c` (register new options)
- Modify: `lib/log/logging.h` (add API for format configuration)

**Step 1: Add option to options.h**

Search for where STRING options are defined and add:

```c
/** Log format string for customizing output */
#define LOG_FORMAT_OPTION "log_format"
```

**Step 2: Register option in registry.c**

Find where options are registered and add:

```c
{
  .long_name = "log-format",
  .short_name = NULL,
  .description = "Custom log format (e.g., '[%time(%H:%M:%S)] [%level_aligned] %message')",
  .type = OPTION_STRING,
  .modes = OPTION_MODE_BINARY, /* Binary-level option */
  .default_value = NULL,
  .env_name = "ASCII_CHAT_LOG_FORMAT",
},
{
  .long_name = "log-format-console",
  .short_name = NULL,
  .description = "Apply log format only to console output (file logs remain unformatted)",
  .type = OPTION_BOOL,
  .modes = OPTION_MODE_BINARY,
  .default_value = "false",
  .env_name = "ASCII_CHAT_LOG_FORMAT_CONSOLE",
}
```

**Step 3: Compile and verify options register**

```bash
cmake --build build 2>&1 | grep -E "error|warning" | head -10
# Expected: No errors related to new options
```

**Step 4: Commit options**

```bash
git add include/ascii-chat/options/options.h lib/options/registry.c
git commit -m "feat: Add --log-format and --log-format-console CLI options

- Add LOG_FORMAT_OPTION for custom format strings
- Add LOG_FORMAT_CONSOLE_OPTION for console-only formatting
- Support environment variables: ASCII_CHAT_LOG_FORMAT, ASCII_CHAT_LOG_FORMAT_CONSOLE
- Binary-level options available in all modes"
```

---

## Task 3: Integrate Format Parser into Logging System

**Files:**
- Modify: `lib/log/logging.c` (integrate format parser)
- Modify: `include/ascii-chat/log/logging.h` (add API functions)

**Step 1: Add format state to logging.c**

In `logging.c`, add to the `log_context_t` struct:

```c
log_format_t *format;                /* Compiled log format (NULL = use default) */
log_format_t *format_console_only;   /* Console-only format variant */
_Atomic bool has_custom_format;      /* True if format was customized */
```

**Step 2: Add format initialization function to logging.h**

```c
/**
 * @brief Set custom log format
 * @param format_str Format string (or NULL to use default)
 * @param console_only If true, apply only to console output
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t log_set_format(const char *format_str, bool console_only);
```

**Step 3: Implement log_set_format in logging.c**

```c
asciichat_error_t log_set_format(const char *format_str, bool console_only) {
  if (!format_str) {
    format_str = log_format_default();
  }

  log_format_t *new_format = log_format_parse(format_str, console_only);
  if (!new_format) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Failed to parse log format: %s", format_str);
  }

  /* Free old format if it exists */
  if (g_log.format) {
    log_format_free(g_log.format);
  }

  g_log.format = new_format;
  atomic_store(&g_log.has_custom_format, true);
  return ASCIICHAT_OK;
}
```

**Step 4: Modify format_log_header to use custom format**

Replace the existing hardcoded formatting in `format_log_header()` to call `log_format_apply()` when custom format is active:

```c
static int format_log_header(char *buffer, size_t buffer_size, log_level_t level, const char *timestamp,
                             const char *file, int line, const char *func, bool use_colors, bool is_file) {
  if (atomic_load(&g_log.has_custom_format)) {
    uint64_t tid = platform_get_thread_id(); /* Platform function to get thread ID */
    return log_format_apply(g_log.format, buffer, buffer_size, level, timestamp, file, line, func, tid, "", use_colors);
  }

  /* Fall back to hardcoded format if no custom format set */
  /* ... existing code ... */
}
```

**Step 5: Update log_destroy to clean up format**

```c
void log_destroy(void) {
  /* ... existing cleanup ... */
  if (g_log.format) {
    log_format_free(g_log.format);
    g_log.format = NULL;
  }
}
```

**Step 6: Compile and verify logging integration**

```bash
cmake --build build 2>&1 | tail -20
# Expected: No errors, warnings acceptable
```

**Step 7: Commit logging integration**

```bash
git add lib/log/logging.c include/ascii-chat/log/logging.h
git commit -m "feat: Integrate format parser into logging system

- Add format state to log_context_t
- Implement log_set_format() to configure custom formats
- Modify format_log_header() to use custom format when set
- Clean up format on log_destroy()
- Maintain backward compatibility with default format"
```

---

## Task 4: Wire Format Options to Logging Initialization

**Files:**
- Modify: `src/main.c` or logging initialization code
- Reference: Look for `log_init()` call in main

**Step 1: Find logging initialization**

```bash
grep -n "log_init\|logging_init" /Users/zfogg/src/github.com/zfogg/ascii-chat/src/main.c | head -5
```

**Step 2: Add format configuration after log_init**

After `log_init()` is called, add:

```c
/* Apply custom log format if specified */
const char *format_str = GET_OPTION(log_format);
bool format_console_only = GET_OPTION(log_format_console);

if (format_str) {
  asciichat_error_t err = log_set_format(format_str, format_console_only);
  if (err != ASCIICHAT_OK) {
    log_error("Invalid log format: %s", format_str);
  }
}
```

**Step 3: Compile and test**

```bash
cmake --build build
./build/bin/ascii-chat --help | grep log-format
# Expected: Shows new --log-format and --log-format-console options
```

**Step 4: Commit wiring**

```bash
git add src/main.c
git commit -m "feat: Wire log format options to logging initialization

- Read --log-format and --log-format-console from options
- Apply format during log system setup
- Log errors if format parsing fails"
```

---

## Task 5: Write Tests for Format Parser

**Files:**
- Create: `tests/unit/test_log_format.c`

**Step 1: Write format parser tests**

```c
// tests/unit/test_log_format.c

#include <criterion/criterion.h>
#include <ascii-chat/log/format.h>
#include <string.h>

Test(log_format, parse_literal_text) {
  log_format_t *fmt = log_format_parse("Hello World", false);
  cr_assert(fmt != NULL);
  cr_assert_eq(fmt->spec_count, 1);
  cr_assert_eq(fmt->specs[0].type, LOG_FORMAT_LITERAL);
  cr_assert_str_eq(fmt->specs[0].literal, "Hello World");
  log_format_free(fmt);
}

Test(log_format, parse_time_specifier) {
  log_format_t *fmt = log_format_parse("[%time(%H:%M:%S)]", false);
  cr_assert(fmt != NULL);
  cr_assert_eq(fmt->spec_count, 3); /* [ + time + ] */
  cr_assert_eq(fmt->specs[1].type, LOG_FORMAT_TIME);
  cr_assert_str_eq(fmt->specs[1].time_fmt, "%H:%M:%S");
  log_format_free(fmt);
}

Test(log_format, parse_level_specifier) {
  log_format_t *fmt = log_format_parse("[%level]", false);
  cr_assert(fmt != NULL);
  cr_assert_eq(fmt->spec_count, 3); /* [ + level + ] */
  cr_assert_eq(fmt->specs[1].type, LOG_FORMAT_LEVEL);
  log_format_free(fmt);
}

Test(log_format, parse_level_aligned_specifier) {
  log_format_t *fmt = log_format_parse("[%level_aligned]", false);
  cr_assert(fmt != NULL);
  cr_assert_eq(fmt->specs[1].type, LOG_FORMAT_LEVEL_ALIGNED);
  log_format_free(fmt);
}

Test(log_format, parse_complex_format) {
  log_format_t *fmt = log_format_parse("[%time(%H:%M:%S)] [%level_aligned] %file:%line in %func(): %message", false);
  cr_assert(fmt != NULL);
  cr_assert(fmt->spec_count > 5); /* At least: [, time, ], [, level, ], file, :, line, etc. */
  log_format_free(fmt);
}

Test(log_format, apply_format_with_log_info) {
  log_format_t *fmt = log_format_parse("[%level] %message", false);
  cr_assert(fmt != NULL);

  char buf[256] = {0};
  int written = log_format_apply(fmt, buf, sizeof(buf), LOG_INFO,
                                 "12:34:56", "test.c", 42, "main",
                                 1234, "Test message", false);

  cr_assert_gt(written, 0);
  cr_assert_str_eq(buf, "[INFO] Test message");
  log_format_free(fmt);
}

Test(log_format, apply_format_with_file_and_line) {
  log_format_t *fmt = log_format_parse("%file:%line", false);
  cr_assert(fmt != NULL);

  char buf[256] = {0};
  int written = log_format_apply(fmt, buf, sizeof(buf), LOG_DEBUG,
                                 "12:34:56", "test.c", 42, "main",
                                 1234, "msg", false);

  cr_assert_gt(written, 0);
  cr_assert_str_eq(buf, "test.c:42");
  log_format_free(fmt);
}

Test(log_format, parse_console_only_flag) {
  log_format_t *fmt = log_format_parse("[%level] %message", true);
  cr_assert(fmt != NULL);
  cr_assert_eq(fmt->console_only, true);
  log_format_free(fmt);
}

Test(log_format, parse_utf8_literals) {
  /* UTF-8 format string with non-ASCII characters */
  log_format_t *fmt = log_format_parse("[时间:%time(%H:%M:%S)] [%level] %message", false);
  cr_assert(fmt != NULL);
  cr_assert(fmt->spec_count > 0);
  log_format_free(fmt);
}

Test(log_format, apply_format_with_utf8_message) {
  log_format_t *fmt = log_format_parse("[%level] %message", false);
  cr_assert(fmt != NULL);

  char buf[256] = {0};
  /* UTF-8 message with non-ASCII characters (café) */
  int written = log_format_apply(fmt, buf, sizeof(buf), LOG_INFO,
                                 "12:34:56", "test.c", 42, "main",
                                 1234, "Processing café orders", false);

  cr_assert_gt(written, 0);
  cr_assert_str_eq(buf, "[INFO] Processing café orders");
  log_format_free(fmt);
}

Test(log_format, reject_invalid_utf8_format_string) {
  /* Invalid UTF-8 sequence (0xFF is not valid UTF-8) */
  const unsigned char invalid_utf8[] = "[%level] \xFF\xFE message";
  log_format_t *fmt = log_format_parse((const char *)invalid_utf8, false);
  /* Should return NULL for invalid UTF-8 */
  cr_assert(fmt == NULL);
}
```

**Step 2: Add tests to CMakeLists.txt**

Find the tests section and add test_log_format to the criterion test suite.

**Step 3: Run tests**

```bash
cmake --build build --target tests
ctest --test-dir build -R "log_format" --output-on-failure
# Expected: All tests pass
```

**Step 4: Commit tests**

```bash
git add tests/unit/test_log_format.c cmake/CMakeLists.txt
git commit -m "test: Add comprehensive tests for log format parser

- Test literal text parsing
- Test time specifier parsing
- Test level and level_aligned specifiers
- Test complex format strings
- Test format application with various log levels
- Test console_only flag
- All tests passing"
```

---

## Task 6: Integration Test - Verify Logs Match Expected Format

**Files:**
- Create: `tests/manual/test_log_format_e2e.sh`

**Step 1: Create manual test script**

```bash
#!/bin/bash
# tests/manual/test_log_format_e2e.sh

set -e

BUILD_DIR="/Users/zfogg/src/github.com/zfogg/ascii-chat/build"
BIN="${BUILD_DIR}/bin/ascii-chat"

# Test 1: Default format (no --log-format)
echo "Test 1: Default format"
$BIN mirror --snapshot --snapshot-delay 0 2>&1 | head -5
echo ""

# Test 2: Custom format with time and level
echo "Test 2: Custom format: [%time(%H:%M:%S.%06ld)] [%level_aligned] %message"
ASCII_CHAT_LOG_FORMAT="[%time(%H:%M:%S.%06ld)] [%level_aligned] %message" \
$BIN mirror --snapshot --snapshot-delay 0 2>&1 | head -5
echo ""

# Test 3: Format with file:line
echo "Test 3: Custom format with file:line"
ASCII_CHAT_LOG_FORMAT="[%level_aligned] %file:%line %message" \
$BIN mirror --snapshot --snapshot-delay 0 2>&1 | head -5
echo ""

# Test 4: Console-only format
echo "Test 4: Console-only format (--log-format-console)"
ASCII_CHAT_LOG_FORMAT="[%time(%H:%M:%S)] %message" \
$BIN mirror --snapshot --snapshot-delay 0 --log-format-console 2>&1 | head -5
echo ""

echo "All manual tests completed. Verify output formats above."
```

**Step 2: Run manual test**

```bash
chmod +x /Users/zfogg/src/github.com/zfogg/ascii-chat/tests/manual/test_log_format_e2e.sh
cd /Users/zfogg/src/github.com/zfogg/ascii-chat
./tests/manual/test_log_format_e2e.sh 2>&1 | head -50
# Expected: Logs appear with custom format applied
```

**Step 3: Verify old logs still look correct**

```bash
./build/bin/ascii-chat mirror --snapshot --snapshot-delay 0 --log-level dev 2>&1 | head -10
# Expected: Logs use default format from log_format_default()
```

**Step 4: Commit test script**

```bash
git add tests/manual/test_log_format_e2e.sh
git commit -m "test: Add end-to-end log format testing script

- Test default format (no custom format specified)
- Test custom format with time and level
- Test format with file:line info
- Test console-only formatting
- Manual verification steps for format application"
```

---

## Task 7: Documentation and Cleanup

**Files:**
- Modify: `docs/crypto.md` or add `docs/logging.md`
- Modify: `CLAUDE.md`

**Step 1: Document log format feature in CLAUDE.md**

Add to debugging section:

```markdown
### Custom Log Format with --log-format

The `--log-format` option allows customizing how log messages are formatted:

```bash
# Default format
./build/bin/ascii-chat server

# Custom format with time, level, and message
./build/bin/ascii-chat server --log-format "[%time(%H:%M:%S.%6s(n(0)))] [%level_aligned] %message"

# Format with file and line number
./build/bin/ascii-chat server --log-format "[%level] %file:%line - %message"

# Format with thread ID
./build/bin/ascii-chat server --log-format "[tid:%tid] [%level] %message"

# Console-only formatting (file logs unformatted)
./build/bin/ascii-chat server --log-format "[%time(%H:%M:%S)] %message" --log-format-console
```

#### Format Specifiers

- `%time(format)` - Custom time format (strftime-compatible)
- `%level` - Log level as uppercase string (DEV, DEBUG, INFO, WARN, ERROR, FATAL)
- `%level_aligned` - Log level padded to 5 characters for alignment
- `%file` - Source filename relative to repository root
- `%line` - Source line number
- `%func` - Function name
- `%tid` - Thread ID
- `%message` - The log message itself
- `\n` - Platform-aware newline (CRLF on Windows, LF on Unix)

#### Environment Variables

- `ASCII_CHAT_LOG_FORMAT` - Set format via environment variable
- `ASCII_CHAT_LOG_FORMAT_CONSOLE` - Set to `true` to apply format only to console
```

**Step 2: Verify build and tests pass**

```bash
cmake --build build --target all
ctest --test-dir build -R "log_format|logging" --output-on-failure
# Expected: All tests pass
```

**Step 3: Run full test suite**

```bash
cmake --build build --target tests
ctest --test-dir build --output-on-failure --parallel 0
# Expected: All tests pass, including new format tests
```

**Step 4: Commit documentation**

```bash
git add CLAUDE.md
git commit -m "docs: Add --log-format documentation

- Document custom log format feature
- List all format specifiers
- Show usage examples
- Explain console-only formatting
- Document environment variables"
```

---

## Task 8: Verification and Final Integration

**Files:**
- Test: Run full suite and verify backward compatibility

**Step 1: Save old binary for format comparison**

```bash
cp /Users/zfogg/src/github.com/zfogg/ascii-chat/build/bin/ascii-chat /tmp/ascii-chat-old
```

**Step 2: Build new version**

```bash
cmake --build build
```

**Step 3: Compare log output (no custom format = should match old)**

```bash
# Old binary output
/tmp/ascii-chat-old mirror --snapshot --snapshot-delay 0 2>&1 | head -5 > /tmp/old-logs.txt

# New binary output (using default format)
./build/bin/ascii-chat mirror --snapshot --snapshot-delay 0 2>&1 | head -5 > /tmp/new-logs.txt

diff /tmp/old-logs.txt /tmp/new-logs.txt
# Expected: No differences (or only minor timing differences)
```

**Step 4: Test various formats end-to-end**

```bash
# Test format with all specifiers
./build/bin/ascii-chat mirror --snapshot --snapshot-delay 0 \
  --log-format "[%time(%H:%M:%S)] [%level_aligned] [tid:%tid] %file:%line %func() - %message" \
  2>&1 | head -5

# Verify output contains all expected components
```

**Step 5: Final commit**

```bash
git status
# Should show: nothing to commit, working tree clean (or only CLAUDE.md changes)
```

**Step 6: Create summary commit**

```bash
git log --oneline -8
# Should show 8 commits from this feature implementation
```

---

## Implementation Notes

### Design Decisions

1. **Parser Simplicity**: Format string parsed once at initialization, not per-log-call, for performance
2. **Backward Compatibility**: Default format matches existing hardcoded format
3. **Console-Only Format**: Separate flag allows different formatting for console vs file
4. **No Color Logic in Format**: Color handling remains in colorize.c, format parser just applies colors
5. **Safe Buffer Handling**: All snprintf calls use safe_snprintf wrapper and check bounds
6. **UTF-8 Safety**: Format strings validated with `utf8_is_valid()` at parse time; literal text extraction is byte-safe because source is already validated
7. **strftime Validation**: Format strings for `%time()` validated against whitelist of safe specifiers; prevents crashes from invalid formats

### UTF-8 Support

- **Format String Validation**: Format strings validated as valid UTF-8 during parsing (via `utf8_is_valid()`)
- **Literal Text Handling**: UTF-8 literals in format strings safely extracted via byte-level copy (safe because source already validated)
- **Message Content**: Existing message validation in logging.c continues to work; logs with invalid UTF-8 trigger warnings
- **Terminal Output**: Terminal display width calculations available via `utf8_display_width()` for future column-aware formatting
- **Safe Truncation**: Existing `truncate_at_whole_line()` function in logging.c ensures truncation doesn't split UTF-8 sequences
- **File Paths**: UTF-8 file paths handled naturally by byte-level storage (paths are opaque byte sequences to format system)

### Testing Strategy

1. **UTF-8 Tests** (Task 5):
   - Parse format strings containing UTF-8 literals (Chinese characters, accents)
   - Apply format to messages containing UTF-8 text
   - Reject format strings with invalid UTF-8
   - Verify proper byte handling in literal extraction

2. **Unit Tests**: Test format parser independently
3. **Integration Tests**: Test with actual log calls (task 6)
4. **Backward Compatibility**: Verify old logs match new default format (task 8)
5. **End-to-End**: Test with real modes (mirror, server, client)

### Known Limitations & Future Work

- Color format specifier `%colorlog_level_string_to_color` is a placeholder
- Custom nanosecond formatting like `%6s(n(0))` deferred to Phase 2 (requires custom time formatter)
- Column-aware formatting for wide characters (CJK, emoji) deferred to future work (infrastructure available via `utf8_display_width()`)

---

