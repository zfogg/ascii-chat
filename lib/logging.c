#include "asciichat_errno.h"
#include "common.h"
#include "platform/abstraction.h"
#include "util/path.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "logging.h"
#include "platform/terminal.h"

/* Terminal capabilities cache */
static terminal_capabilities_t g_terminal_caps = {0};
static bool g_terminal_caps_initialized = false;

/* Windows compatibility for strcasecmp */
#ifdef _WIN32
#define strcasecmp _stricmp
#endif

/* ============================================================================
 * Logging Implementation
 * ============================================================================
 */

/* Parse LOG_LEVEL environment variable */
static log_level_t parse_log_level_from_env(void) {
  const char *env_level = SAFE_GETENV("LOG_LEVEL");
  if (!env_level) {
    return DEFAULT_LOG_LEVEL; // Default level based on build type
  }
  size_t len = strnlen(env_level, 64);
  if (len >= 64) {
    return DEFAULT_LOG_LEVEL; // Invalid - too long, use default
  }

  // Case-insensitive comparison
  if (strcasecmp(env_level, "DEBUG") == 0 || strcmp(env_level, "0") == 0) {
    return LOG_DEBUG;
  } else if (strcasecmp(env_level, "INFO") == 0 || strcmp(env_level, "1") == 0) {
    return LOG_INFO;
  } else if (strcasecmp(env_level, "WARN") == 0 || strcmp(env_level, "2") == 0) {
    return LOG_WARN;
  } else if (strcasecmp(env_level, "ERROR") == 0 || strcmp(env_level, "3") == 0) {
    return LOG_ERROR;
  } else if (strcasecmp(env_level, "FATAL") == 0 || strcmp(env_level, "4") == 0) {
    return LOG_FATAL;
  } else if (strcasecmp(env_level, "DEV") == 0 || strcmp(env_level, "5") == 0) {
    return LOG_DEV;
  }

  // Invalid value - return default
  return DEFAULT_LOG_LEVEL;
}

/* Log rotation function - keeps the tail (recent entries) (assumes mutex held) */
static void rotate_log_if_needed_unlocked(void) {
  if (!g_log.file || g_log.file == STDERR_FILENO || strlen(g_log.filename) == 0) {
    return;
  }

  if (g_log.current_size >= MAX_LOG_SIZE) {
    platform_close(g_log.file);

    /* Open file for reading to get the tail */
    int read_file = platform_open(g_log.filename, O_RDONLY, 0);
    if (read_file < 0) {
      safe_fprintf(stderr, "Failed to open log file for tail rotation: %s\n", g_log.filename);
      /* Fall back to regular truncation */
      int fd = platform_open(g_log.filename, O_CREAT | O_RDWR | O_TRUNC, 0600);
      g_log.file = fd;
      g_log.current_size = 0;
      return;
    }

    /* Seek to position where we want to start keeping data (keep last 2MB) */
    size_t keep_size = MAX_LOG_SIZE * 2 / 3; /* Keep last 2MB of 3MB file */
    if (lseek(read_file, (off_t)(g_log.current_size - keep_size), SEEK_SET) == (off_t)-1) {
      platform_close(read_file);
      /* Fall back to truncation */
      int fd = platform_open(g_log.filename, O_CREAT | O_RDWR | O_TRUNC, 0600);
      g_log.file = fd;
      g_log.current_size = 0;
      return;
    }

    /* Skip to next line boundary to avoid partial lines */
    char c;
    while (platform_read(read_file, &c, 1) > 0 && c != '\n') {
      /* Skip characters until newline */
    }

    /* Read the tail into a temporary file */
    char temp_filename[512];
    SAFE_SNPRINTF(temp_filename, sizeof(temp_filename), "%s.tmp", g_log.filename);
    int temp_file = platform_open(temp_filename, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (temp_file < 0) {
      platform_close(read_file);
      /* Fall back to truncation */
      int fd = platform_open(g_log.filename, O_CREAT | O_RDWR | O_TRUNC, 0600);
      g_log.file = fd;
      g_log.current_size = 0;
      return;
    }

    /* Copy tail to temp file */
    char buffer[8192];
    ssize_t bytes_read;
    size_t new_size = 0;
    while ((bytes_read = platform_read(read_file, buffer, sizeof(buffer))) > 0) {
      ssize_t written = platform_write(temp_file, buffer, bytes_read);
      if (written != bytes_read) {
        platform_close(read_file);
        platform_close(temp_file);
        unlink(temp_filename);
        /* Fall back to truncation */
        int fd = platform_open(g_log.filename, O_CREAT | O_RDWR | O_TRUNC, 0600);
        g_log.file = fd;
        g_log.current_size = 0;
        return;
      }
      new_size += (size_t)bytes_read;
    }

    platform_close(read_file);
    platform_close(temp_file);

    /* Replace original with temp file */
    if (rename(temp_filename, g_log.filename) != 0) {
      unlink(temp_filename); /* Clean up temp file */
      /* Fall back to truncation */
      int fd = platform_open(g_log.filename, O_CREAT | O_RDWR | O_TRUNC, 0600);
      g_log.file = fd;
      g_log.current_size = 0;
      return;
    }

    /* Reopen for appending */
    g_log.file = platform_open(g_log.filename, O_CREAT | O_RDWR | O_APPEND, 0600);
    if (g_log.file < 0) {
      safe_fprintf(stderr, "Failed to reopen rotated log file: %s\n", g_log.filename);
      g_log.file = STDERR_FILENO;
      g_log.filename[0] = '\0';
    } else {
      g_log.current_size = new_size;
      /* Log the rotation event */
      {
        char log_msg[256];
        int log_msg_len = SAFE_SNPRINTF(log_msg, sizeof(log_msg), "[%s] [INFO] Log tail-rotated (kept %zu bytes)\n",
                                        "00:00:00.000000", new_size);
        if (log_msg_len > 0) {
          (void)platform_write(g_log.file, log_msg, (size_t)log_msg_len);
        }
      }
    }
  }
}

void log_init(const char *filename, log_level_t level) {
  // Initialize mutex if this is the first time
  static bool mutex_initialized = false;
  if (!mutex_initialized) {
    mutex_init(&g_log.mutex);
    mutex_initialized = true;
  }

  mutex_lock(&g_log.mutex);

  // Preserve the terminal output setting
  bool preserve_terminal_output = g_log.terminal_output_enabled;

  if (g_log.initialized) {
    if (g_log.file && g_log.file != STDERR_FILENO) {
      platform_close(g_log.file);
    }
  }

  // Check LOG_LEVEL environment variable on first initialization
  // log_init() is an explicit call, so it overrides manual level setting
  if (!g_log.env_checked) {
    const char *env_level_str = SAFE_GETENV("LOG_LEVEL");
    if (env_level_str) {
      // Environment variable is set - use it
      log_level_t env_level = parse_log_level_from_env();
      g_log.level = env_level;
    } else {
      // Environment variable not set - use the provided level parameter
      g_log.level = level;
    }
    g_log.env_checked = true;
  } else {
    // On subsequent calls to log_init, check if env var is set
    const char *env_level_str = SAFE_GETENV("LOG_LEVEL");
    if (env_level_str) {
      // Environment variable takes precedence
      log_level_t env_level = parse_log_level_from_env();
      g_log.level = env_level;
    } else {
      // No env var - use the provided level parameter
      g_log.level = level;
    }
  }
  // Reset the manual flag since log_init() is an explicit call
  g_log.level_manually_set = false;
  g_log.current_size = 0;

  if (filename) {
    /* Store filename for rotation */
    SAFE_STRNCPY(g_log.filename, filename, sizeof(g_log.filename) - 1);
    int fd = platform_open(filename, O_CREAT | O_RDWR | O_TRUNC, 0600);
    g_log.file = fd;
    if (!g_log.file) {
      if (preserve_terminal_output) {
        safe_fprintf(stderr, "Failed to open log file: %s\n", filename);
      }
      g_log.file = STDERR_FILENO;
      g_log.filename[0] = '\0'; /* Clear filename on failure */
    } else {
      /* File was truncated, so size starts at 0 */
      g_log.current_size = 0;
    }
  } else {
    g_log.file = STDERR_FILENO;
    g_log.filename[0] = '\0';
  }

  g_log.initialized = true;

  // Now that logging is initialized, we can safely detect terminal capabilities
  log_redetect_terminal_capabilities();

  // Restore the terminal output setting
  g_log.terminal_output_enabled = preserve_terminal_output;

  mutex_unlock(&g_log.mutex);
}

void log_destroy(void) {
  mutex_lock(&g_log.mutex);

  if (g_log.file && g_log.file != STDERR_FILENO) {
    platform_close(g_log.file);
  }

  g_log.file = 0;
  g_log.initialized = false;

  mutex_unlock(&g_log.mutex);
}

void log_set_level(log_level_t level) {
  mutex_lock(&g_log.mutex);
  g_log.level = level;
  g_log.level_manually_set = true;
  mutex_unlock(&g_log.mutex);
}

log_level_t log_get_level(void) {
  mutex_lock(&g_log.mutex);
  log_level_t level = g_log.level;
  mutex_unlock(&g_log.mutex);
  return level;
}

void log_set_terminal_output(bool enabled) {
  mutex_lock(&g_log.mutex);
  g_log.terminal_output_enabled = enabled;
  mutex_unlock(&g_log.mutex);
}

bool log_get_terminal_output(void) {
  mutex_lock(&g_log.mutex);
  bool enabled = g_log.terminal_output_enabled;
  mutex_unlock(&g_log.mutex);
  return enabled;
}

void log_truncate_if_large(void) {
  mutex_lock(&g_log.mutex);

  if (g_log.file && g_log.file != STDERR_FILENO && strlen(g_log.filename) > 0) {
    /* Check if current log is too large */
    struct stat st;
    if (fstat(g_log.file, &st) == 0 && st.st_size > MAX_LOG_SIZE) {
      /* Save the current size and trigger rotation logic */
      g_log.current_size = (size_t)st.st_size;

      /* Use the same tail-keeping rotation logic */
      rotate_log_if_needed_unlocked();
    }
  }

  mutex_unlock(&g_log.mutex);
}


/* Helper: Write formatted log entry to actual log file (assumes mutex held)
 * Only writes to real file descriptors, not stderr
 */
static void write_to_log_file_unlocked(const char *buffer, int length) {
  if (length == 0 || buffer == NULL) {
    SET_ERRNO(ERROR_INVALID_PARAM, "No log message to write or buffer is NULL");
    return;
  }
  if (length > MAX_LOG_SIZE) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Log message is too long");
    return;
  }
  if (g_log.file > 0 && g_log.file != STDERR_FILENO) {
    ssize_t written = platform_write(g_log.file, buffer, length);
    if (written > 0) {
      g_log.current_size += (size_t)written;
    } else {
      safe_fprintf(stderr, "Failed to write to log file: %s\n", g_log.filename);
      log_file_msg("Failed to write to log file: %s", g_log.filename);
    }
  }
}

/* Helper: Write formatted log entry to stderr fallback (assumes mutex held)
 * Used when no log file is configured
 */
static void write_to_stderr_fallback_unlocked(const char *buffer) {
  if (g_log.file == STDERR_FILENO) {
    safe_fprintf(stderr, "%s", buffer);
    (void)fflush(stderr);
  }
}

/* Helper: Format log message header (timestamp, level, location info)
 * Returns the number of characters written to the buffer
 */
static int format_log_header(char *buffer, size_t buffer_size, log_level_t level, const char *timestamp,
                             const char *file, int line, const char *func, bool use_colors) {
  const char **colors = use_colors ? log_get_color_array() : NULL;
  const char *color = use_colors ? colors[level] : "";
  const char *reset = use_colors ? colors[LOGGING_COLOR_RESET] : "";

  const char *level_string = level_strings[level];
  if (level_string == level_strings[LOG_INFO]) {
    level_string = "INFO ";
  } else if (level_string == level_strings[LOG_WARN]) {
    level_string = "WARN ";
  } else if (level_string == level_strings[LOG_DEV]) {
    level_string = "DEV  ";
  }

#ifdef NDEBUG
  // Release mode: simplified format (timestamp, level, message only)
  return snprintf(buffer, buffer_size, "[%s%s%s] [%s%s%s] ", color, timestamp, reset, color, level_string, reset);
#else
  // Debug mode: full format with file location and function
  const char *rel_file = extract_project_relative_path(file);
  if (use_colors) {
    return snprintf(buffer, buffer_size, "[%s%s%s] [%s%s%s] %s%s%s:%s%d%s in %s%s%s(): ",
                    color, timestamp, reset, color, level_string, reset,
                    colors[LOGGING_COLOR_WARN], rel_file, reset,
                    colors[LOGGING_COLOR_FATAL], line, reset,
                    colors[LOGGING_COLOR_DEV], func, reset);
  }
  return snprintf(buffer, buffer_size, "[%s] [%s] %s:%d in %s(): ", timestamp, level_strings[level], rel_file, line, func);
#endif
}

/* Helper: Write colored log entry to terminal (assumes mutex held)
 * Only writes if terminal output is enabled
 */
static void write_to_terminal_unlocked(log_level_t level, const char *timestamp, const char *file, int line,
                                       const char *func, const char *fmt, va_list args) {
  (void)file;
  (void)line;
  (void)func;

  if (!g_log.terminal_output_enabled) {
    return;
  }

  // Choose output stream: errors/warnings to stderr, info/debug to stdout
  FILE *output_stream = (level == LOG_ERROR || level == LOG_WARN) ? stderr : stdout;
  int fd = output_stream == stderr ? STDERR_FILENO : STDOUT_FILENO;

  // Format the header using centralized formatting
  char header_buffer[512];
  bool use_colors = isatty(fd);
  int header_len = format_log_header(header_buffer, sizeof(header_buffer), level, timestamp, file, line, func, use_colors);

  if (header_len > 0 && header_len < (int)sizeof(header_buffer)) {
    safe_fprintf(output_stream, "%s", header_buffer);
    (void)vfprintf(output_stream, fmt, args);

    if (use_colors) {
      const char **colors = log_get_color_array();
      safe_fprintf(output_stream, "%s\n", colors[LOGGING_COLOR_RESET]); // Ensure color is reset after the message
    } else {
      safe_fprintf(output_stream, "\n");
    }
    (void)fflush(output_stream);
  }
}

void log_msg(log_level_t level, const char *file, int line, const char *func, const char *fmt, ...) {
  // Simple approach: just check if initialized
  // If not initialized, just don't log (main() should have initialized it)
  if (!g_log.initialized) {
    return; // Don't log if not initialized - this prevents the deadlock
  }

  // Check environment variable on first log call if not already checked
  // This handles the case where log_msg is called before log_init
  if (!g_log.env_checked && !g_log.level_manually_set) {
    mutex_lock(&g_log.mutex);
    if (!g_log.env_checked && !g_log.level_manually_set) {
      const char *env_level_str = SAFE_GETENV("LOG_LEVEL");
      if (env_level_str) {
        // Environment variable is set - use it
        log_level_t env_level = parse_log_level_from_env();
        g_log.level = env_level;
      }
      // Otherwise keep the default level (DEBUG or INFO) from static initialization
      g_log.env_checked = true;
    }
    mutex_unlock(&g_log.mutex);
  }

  if (level < g_log.level) {
    return;
  }

  mutex_lock(&g_log.mutex);

  /* Get current time in local timezone */
  struct timespec ts;
  (void)clock_gettime(CLOCK_REALTIME, &ts);

  struct tm tm_info;
  platform_localtime(&ts.tv_sec, &tm_info);

  char time_buf[32];
  (void)strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &tm_info);

  char time_buf_ms[64]; // Increased size to prevent truncation
  long microseconds = ts.tv_nsec / 1000;
  // Clamp microseconds to valid range (0-999999) to ensure format safety
  if (microseconds < 0)
    microseconds = 0;
  if (microseconds > 999999)
    microseconds = 999999;
  SAFE_SNPRINTF(time_buf_ms, sizeof(time_buf_ms), "%s.%06ld", time_buf, microseconds);

  /* Check if log rotation is needed */
  rotate_log_if_needed_unlocked();

  /* Format the message once for file output */
  char log_buffer[4096];
  va_list args;
  va_start(args, fmt);

  // Format header without colors for file output
  int header_len = format_log_header(log_buffer, sizeof(log_buffer), level, time_buf_ms, file, line, func, false);

  // Add the actual message
  int msg_len = header_len;
  if (header_len > 0 && header_len < (int)sizeof(log_buffer)) {
    msg_len += vsnprintf(log_buffer + header_len, sizeof(log_buffer) - header_len, fmt, args);
  }

  // Add newline
  if (msg_len > 0 && msg_len < (int)sizeof(log_buffer) - 1) {
    log_buffer[msg_len++] = '\n';
    log_buffer[msg_len] = '\0';
  }

  va_end(args);

  /* Write to log file if configured */
  write_to_log_file_unlocked(log_buffer, msg_len);

  /* Write to stderr if no log file configured */
  write_to_stderr_fallback_unlocked(log_buffer);

  /* Also write to terminal with colors if terminal output enabled */
  va_list args_terminal;
  va_start(args_terminal, fmt);
  write_to_terminal_unlocked(level, time_buf_ms, file, line, func, fmt, args_terminal);
  va_end(args_terminal);

  mutex_unlock(&g_log.mutex);
}

void log_plain_msg(const char *fmt, ...) {
  if (!g_log.initialized) {
    return; // Don't log if not initialized
  }

  // Skip logging entirely if we're shutting down to avoid mutex issues
  if (shutdown_is_requested()) {
    return; // Don't try to log during shutdown - avoids deadlocks
  }

  mutex_lock(&g_log.mutex);

  // Format the message without timestamps or log levels
  char log_buffer[4096];
  va_list args;
  va_start(args, fmt);
  int msg_len = vsnprintf(log_buffer, sizeof(log_buffer), fmt, args);
  va_end(args);

  if (msg_len > 0 && msg_len < (int)sizeof(log_buffer)) {
    /* Write to log file if configured */
    write_to_log_file_unlocked(log_buffer, msg_len);
    write_to_log_file_unlocked("\n", 1);

    /* Always write to stderr for log_plain_msg */
    safe_fprintf(stderr, "%s\n", log_buffer);
    (void)fflush(stderr);
  }

  mutex_unlock(&g_log.mutex);
}

void log_file_msg(const char *fmt, ...) {
  if (!g_log.initialized) {
    return; // Don't log if not initialized
  }

  mutex_lock(&g_log.mutex);

  // Format the message without timestamps or log levels
  char log_buffer[4096];
  va_list args;
  va_start(args, fmt);
  int msg_len = vsnprintf(log_buffer, sizeof(log_buffer), fmt, args);
  va_end(args);

  if (msg_len > 0 && msg_len < (int)sizeof(log_buffer)) {
    /* Write to log file only - no stderr output */
    write_to_log_file_unlocked(log_buffer, msg_len);
    write_to_log_file_unlocked("\n", 1);
  }

  mutex_unlock(&g_log.mutex);
}

/* ============================================================================
 * Color Helper Functions
 * ============================================================================ */

/* Initialize terminal capabilities if not already done */
static void init_terminal_capabilities(void) {
  if (!g_terminal_caps_initialized) {
    // Use safe fallback during logging initialization to avoid recursion
    g_terminal_caps.color_level = TERM_COLOR_16;
    g_terminal_caps.capabilities = TERM_CAP_COLOR_16;
    g_terminal_caps.color_count = 16;
    g_terminal_caps.detection_reliable = false;
    g_terminal_caps_initialized = true;
  }
}

/* Re-detect terminal capabilities after logging is initialized */
void log_redetect_terminal_capabilities(void) {
  g_terminal_caps = detect_terminal_capabilities();
  g_terminal_caps_initialized = true;
}

/* Get the appropriate color array based on terminal capabilities */
const char **log_get_color_array(void) {
  init_terminal_capabilities();

  const char **colors;
  if (g_terminal_caps.color_level >= TERM_COLOR_TRUECOLOR) {
    colors = level_colors_truecolor;
  } else if (g_terminal_caps.color_level >= TERM_COLOR_256) {
    colors = level_colors_256;
  } else {
    colors = level_colors_16;
  }

  return colors;
}

const char *log_level_color(logging_color_t color) {
  const char **colors = log_get_color_array();
  if (color >= 0 && color <= LOGGING_COLOR_RESET) {
    return colors[color];
  }
  return colors[LOGGING_COLOR_RESET]; /* Return reset color if invalid */
}
