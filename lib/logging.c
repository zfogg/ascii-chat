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

/* Windows compatibility for strcasecmp */
#ifdef _WIN32
#define strcasecmp _stricmp
#endif

/* ============================================================================
 * Logging Implementation
 * ============================================================================
 */

/* Default log level based on build type */
#ifdef NDEBUG
#define DEFAULT_LOG_LEVEL LOG_INFO /* Release build: INFO and above */
#else
#define DEFAULT_LOG_LEVEL LOG_DEBUG /* Debug build: DEBUG and above */
#endif

#define MAX_LOG_SIZE (3 * 1024 * 1024) /* 3MB max log file size */

static struct {
  int file;
  log_level_t level;
  mutex_t mutex;
  bool initialized;
  char filename[256];           /* Store filename for rotation */
  size_t current_size;          /* Track current file size */
  bool terminal_output_enabled; /* Control stderr output to terminal */
  bool level_manually_set;      /* Track if level was set manually */
  bool env_checked;             /* Track if we've checked environment */
} g_log = {.file = 0,
           .level = DEFAULT_LOG_LEVEL,
           .initialized = false,
           .filename = {0},
           .current_size = 0,
           .terminal_output_enabled = true,
           .level_manually_set = false,
           .env_checked = false};

static const char *level_strings[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

static const char *level_colors[] = {
    "\x1b[36m", /* DEBUG: Cyan */
    "\x1b[32m", /* INFO: Green */
    "\x1b[33m", /* WARN: Yellow */
    "\x1b[31m", /* ERROR: Red */
    "\x1b[35m"  /* FATAL: Magenta */
};

/* Parse LOG_LEVEL environment variable */
static log_level_t parse_log_level_from_env(void) {
  const char *env_level = SAFE_GETENV("LOG_LEVEL");
  if (!env_level) {
    return DEFAULT_LOG_LEVEL; // Default level based on build type
  }

  // Protect against maliciously large environment variables (DoS prevention)
  // Max reasonable length for log level is "DEBUG" (5 chars) or numeric (1 char)
  size_t len = strnlen(env_level, 64);
  if (len >= 64) {
    return DEFAULT_LOG_LEVEL; // Invalid - too long, use default
  }

  // Case-insensitive comparison
  if (strcasecmp(env_level, "DEBUG") == 0 || strcmp(env_level, "0") == 0) {
    return LOG_DEBUG;
  }
  if (strcasecmp(env_level, "INFO") == 0 || strcmp(env_level, "1") == 0) {
    return LOG_INFO;
  }
  if (strcasecmp(env_level, "WARN") == 0 || strcmp(env_level, "2") == 0) {
    return LOG_WARN;
  }
  if (strcasecmp(env_level, "ERROR") == 0 || strcmp(env_level, "3") == 0) {
    return LOG_ERROR;
  }
  if (strcasecmp(env_level, "FATAL") == 0 || strcmp(env_level, "4") == 0) {
    return LOG_FATAL;
  }

  // Invalid value - return default
  return DEFAULT_LOG_LEVEL;
}

/* Helper function to extract relative path from absolute path
 * NOTE: Uses shared implementation from common.c which handles:
 * - Dynamic PROJECT_SOURCE_ROOT (works with any repo name)
 * - Slash-agnostic path matching (handles Windows/Unix separators)
 * - Fallback to filename extraction
 */
static const char *extract_relative_path(const char *file) {
  return extract_project_relative_path(file);
}

/* Log rotation function - keeps the tail (recent entries) (assumes mutex held) */
static void rotate_log_if_needed_unlocked(void) {
  if (!g_log.file || g_log.file == STDERR_FILENO || strlen(g_log.filename) == 0) {
    return;
  }

  if (g_log.current_size >= MAX_LOG_SIZE) {
    platform_close(g_log.file);

    /* Open file for reading to get the tail */
    int read_file = SAFE_OPEN(g_log.filename, O_RDONLY, 0);
    if (read_file < 0) {
      (void)fprintf(stderr, "Failed to open log file for tail rotation: %s\n", g_log.filename);
      /* Fall back to regular truncation */
      int fd = SAFE_OPEN(g_log.filename, O_CREAT | O_RDWR | O_TRUNC, 0600);
      g_log.file = fd;
      g_log.current_size = 0;
      return;
    }

    /* Seek to position where we want to start keeping data (keep last 2MB) */
    size_t keep_size = MAX_LOG_SIZE * 2 / 3; /* Keep last 2MB of 3MB file */
    if (lseek(read_file, (off_t)(g_log.current_size - keep_size), SEEK_SET) == (off_t)-1) {
      platform_close(read_file);
      /* Fall back to truncation */
      int fd = SAFE_OPEN(g_log.filename, O_CREAT | O_RDWR | O_TRUNC, 0600);
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
    int temp_file = SAFE_OPEN(temp_filename, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (temp_file < 0) {
      platform_close(read_file);
      /* Fall back to truncation */
      int fd = SAFE_OPEN(g_log.filename, O_CREAT | O_RDWR | O_TRUNC, 0600);
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
        int fd = SAFE_OPEN(g_log.filename, O_CREAT | O_RDWR | O_TRUNC, 0600);
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
      int fd = SAFE_OPEN(g_log.filename, O_CREAT | O_RDWR | O_TRUNC, 0600);
      g_log.file = fd;
      g_log.current_size = 0;
      return;
    }

    /* Reopen for appending */
    g_log.file = SAFE_OPEN(g_log.filename, O_CREAT | O_RDWR | O_APPEND, 0600);
    if (g_log.file < 0) {
      (void)fprintf(stderr, "Failed to reopen rotated log file: %s\n", g_log.filename);
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
          ssize_t written = platform_write(g_log.file, log_msg, (size_t)log_msg_len);
          (void)written; // suppress unused warning
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
    int fd = SAFE_OPEN(filename, O_CREAT | O_RDWR | O_TRUNC, 0600);
    g_log.file = fd;
    if (!g_log.file) {
      if (preserve_terminal_output) {
        (void)fprintf(stderr, "Failed to open log file: %s\n", filename);
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

/* Helper: Format a complete log entry into a buffer
 * Returns: number of bytes written (excluding null terminator)
 */
static int format_log_entry(char *buffer, size_t buffer_size, const char *timestamp, log_level_t level,
                            const char *file, int line, const char *func, const char *fmt, va_list args) {
  int offset = 0;

#ifdef NDEBUG
  // Release mode: simplified format (timestamp, level, message only)
  offset = SAFE_SNPRINTF(buffer, buffer_size, "[%s] [%s] ", timestamp, level_strings[level]);
#else
  // Debug mode: full format with file location and function
  const char *rel_file = extract_relative_path(file);
  offset = SAFE_SNPRINTF(buffer, buffer_size, "[%s] [%s] %s:%d in %s(): ", timestamp, level_strings[level], rel_file,
                         line, func);
#endif

  // Add the actual message
  if (offset > 0 && offset < (int)buffer_size) {
    offset += vsnprintf(buffer + offset, buffer_size - offset, fmt, args);
  }

  // Add newline
  if (offset > 0 && offset < (int)buffer_size - 1) {
    buffer[offset++] = '\n';
    buffer[offset] = '\0';
  }

  return offset;
}

/* Helper: Write formatted log entry to actual log file (assumes mutex held)
 * Only writes to real file descriptors, not stderr
 */
static void write_to_log_file_unlocked(const char *buffer, int length) {
  if (g_log.file > 0 && g_log.file != STDERR_FILENO) {
    ssize_t written = platform_write(g_log.file, buffer, length);
    if (written > 0) {
      g_log.current_size += (size_t)written;
    }
  }
}

/* Helper: Write formatted log entry to stderr fallback (assumes mutex held)
 * Used when no log file is configured
 */
static void write_to_stderr_fallback_unlocked(const char *buffer) {
  if (g_log.file == STDERR_FILENO) {
    (void)fprintf(stderr, "%s", buffer);
    (void)fflush(stderr);
  }
}

/* Helper: Write colored log entry to terminal (assumes mutex held)
 * Only writes if terminal output is enabled
 */
static void write_to_terminal_unlocked(log_level_t level, const char *timestamp, const char *file, int line,
                                       const char *func, const char *fmt, va_list args) {
  if (!g_log.terminal_output_enabled) {
    return;
  }

  // Choose output stream: errors/warnings to stderr, info/debug to stdout
  FILE *output_stream = (level == LOG_ERROR || level == LOG_WARN) ? stderr : stdout;
  int fd = (level == LOG_ERROR || level == LOG_WARN) ? STDERR_FILENO : STDOUT_FILENO;

  // Only write colors if connected to a TTY
  if (isatty(fd)) {
#ifdef NDEBUG
    // Release mode: simplified format (timestamp, level, message only) WITH COLORS
    (void)fprintf(output_stream, "%s[%s] [%s]\x1b[0m ", level_colors[level], timestamp, level_strings[level]);
#else
    // Debug mode: full format with file location and function WITH COLORS
    const char *rel_file = extract_relative_path(file);
    (void)fprintf(output_stream, "%s[%s] [%s]\x1b[0m %s:%d in %s(): ", level_colors[level], timestamp,
                  level_strings[level], rel_file, line, func);
#endif

    (void)vfprintf(output_stream, fmt, args);
    (void)fprintf(output_stream, "\n");
    (void)fflush(output_stream);
  } else {
    // Not a TTY (piped/redirected) - output without colors
#ifdef NDEBUG
    // Release mode: simplified format (timestamp, level, message only) WITHOUT COLORS
    (void)fprintf(output_stream, "[%s] [%s] ", timestamp, level_strings[level]);
#else
    // Debug mode: full format with file location and function WITHOUT COLORS
    const char *rel_file = extract_relative_path(file);
    (void)fprintf(output_stream, "[%s] [%s] %s:%d in %s(): ", timestamp, level_strings[level], rel_file, line, func);
#endif

    (void)vfprintf(output_stream, fmt, args);
    (void)fprintf(output_stream, "\n");
    (void)fflush(output_stream);
  }
}

void log_msg(log_level_t level, const char *file, int line, const char *func, const char *fmt, ...) {
  // Simple approach: just check if initialized
  // If not initialized, just don't log (main() should have initialized it)
  if (!g_log.initialized) {
    return; // Don't log if not initialized - this prevents the deadlock
  }

  // Skip logging entirely if we're shutting down to avoid mutex issues
  if (shutdown_is_requested()) {
    return; // Don't try to log during shutdown - avoids deadlocks
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
  int msg_len = format_log_entry(log_buffer, sizeof(log_buffer), time_buf_ms, level, file, line, func, fmt, args);
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
