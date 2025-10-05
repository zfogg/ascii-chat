#include "common.h"
#include "platform/abstraction.h"
#include "platform/file.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>

/* ============================================================================
 * Logging Implementation
 * ============================================================================
 */

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
} g_log = {.file = 0,
           .level = LOG_INFO,
           .mutex = {0},
           .initialized = false,
           .filename = {0},
           .current_size = 0,
           .terminal_output_enabled = true,
           .level_manually_set = false};

static const char *level_strings[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};

static const char *level_colors[] = {
    "\x1b[36m", /* DEBUG: Cyan */
    "\x1b[32m", /* INFO: Green */
    "\x1b[33m", /* WARN: Yellow */
    "\x1b[31m", /* ERROR: Red */
    "\x1b[35m"  /* FATAL: Magenta */
};

/* Helper function to extract relative path from absolute path */
static const char *extract_relative_path(const char *file) {
  if (!file)
    return "unknown";

  /* Look for ascii-chat repository root directory */
  const char *repo_name = "ascii-chat";
  const char *repo_pos = strstr(file, repo_name);

  if (repo_pos) {
    /* Move past the repo name and directory separator */
    const char *after_repo = repo_pos + strlen(repo_name);

    /* Skip the path separator if present */
    if (*after_repo == '/' || *after_repo == '\\') {
      after_repo++;
    }

    /* Return the path relative to repo root */
    if (*after_repo != '\0') {
      return after_repo;
    }
  }

  /* Fallback: try to find just the filename */
  const char *last_slash = strrchr(file, '/');
  const char *last_backslash = strrchr(file, '\\');
  const char *last_sep = (last_slash > last_backslash) ? last_slash : last_backslash;

  if (last_sep) {
    return last_sep + 1;
  }

  /* If no separators found, return the original string */
  return file;
}

/* Log rotation function - keeps the tail (recent entries) */
static void rotate_log_if_needed(void) {
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

  g_log.level = level;
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
      rotate_log_if_needed();
    }
  }

  mutex_unlock(&g_log.mutex);
}

extern atomic_bool g_should_exit;

void log_msg(log_level_t level, const char *file, int line, const char *func, const char *fmt, ...) {
  // Simple approach: just check if initialized
  // If not initialized, just don't log (main() should have initialized it)
  if (!g_log.initialized) {
    return; // Don't log if not initialized - this prevents the deadlock
  }

  // Skip logging entirely if we're shutting down to avoid mutex issues
  if (atomic_load(&g_should_exit)) {
    return; // Don't try to log during shutdown - avoids deadlocks
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
  rotate_log_if_needed();

  FILE *log_file = NULL;

  if (g_log.file == STDERR_FILENO) {
    log_file = stderr;
  } else if (g_log.file > 0) {
    // Write directly using the file descriptor to avoid FILE* resource leak
    // Format the log message into a buffer first
    char log_buffer[4096];
    int offset = 0;

    // Add timestamp, level, location
    const char *rel_file = extract_relative_path(file);
    offset = SAFE_SNPRINTF(log_buffer, sizeof(log_buffer), "[%s] [%s] %s:%d in %s(): ", time_buf_ms,
                           level_strings[level], rel_file, line, func);

    // Add the actual message
    va_list args;
    va_start(args, fmt);
    offset += vsnprintf(log_buffer + offset, sizeof(log_buffer) - offset, fmt, args);
    va_end(args);

    // Add newline
    if (offset < (int)sizeof(log_buffer) - 1) {
      log_buffer[offset++] = '\n';
    }

    // Write to file descriptor directly
    ssize_t written = platform_write(g_log.file, log_buffer, offset);
    if (written > 0) {
      g_log.current_size += (size_t)written;
    }

    // NOTE: No need to flush with direct platform_write() - it bypasses stdio buffering
  }

  // Create va_list once and reuse it
  va_list args;
  va_start(args, fmt);

  // Handle stderr output separately - only if terminal output is enabled
  if (log_file != NULL && log_file == stderr && g_log.terminal_output_enabled) {
    const char *rel_file = extract_relative_path(file);
    (void)fprintf(log_file, "[%s] [%s] %s:%d in %s(): ", time_buf_ms, level_strings[level], rel_file, line, func);

    // Create a copy of va_list for this use
    va_list args_copy;
    va_copy(args_copy, args);
    (void)vfprintf(log_file, fmt, args_copy);
    va_end(args_copy);

    (void)fprintf(log_file, "\n");
    (void)fflush(log_file);
  }

  /* Print to stdout (INFO/DEBUG) or stderr (ERROR/WARN) with colors if terminal output is enabled */
  if (g_log.terminal_output_enabled) {
    /* Choose output stream based on log level */
    FILE *output_stream = (level == LOG_ERROR || level == LOG_WARN) ? stderr : stdout;
    int fd = (level == LOG_ERROR || level == LOG_WARN) ? STDERR_FILENO : STDOUT_FILENO;

    if (isatty(fd)) {
      const char *rel_file = extract_relative_path(file);
      (void)fprintf(output_stream, "%s[%s] [%s]\x1b[0m %s:%d in %s(): ", level_colors[level], time_buf_ms,
                    level_strings[level], rel_file, line, func);

      // Create a copy of va_list for this use
      va_list args_copy2;
      va_copy(args_copy2, args);
      (void)vfprintf(output_stream, fmt, args_copy2);
      va_end(args_copy2);

      (void)fprintf(output_stream, "\n");
      (void)fflush(output_stream);
    }
  }

  // Clean up the original va_list
  va_end(args);

  // Always unlock the mutex after ALL output is complete
  mutex_unlock(&g_log.mutex);
}
