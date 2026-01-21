/**
 * @file mmap.c
 * @brief Lock-free memory-mapped text logging implementation
 * @ingroup logging
 *
 * Writes human-readable log text directly to a memory-mapped file.
 * On crash, the log file is immediately readable with cat/tail.
 */

#include "log/mmap.h"
#include "log/logging.h"
#include "platform/mmap.h"
#include "platform/system.h"
#include "video/ansi.h"
#include "util/time.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <signal.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/* ============================================================================
 * Global State
 * ============================================================================ */

static struct {
  platform_mmap_t mmap;                     /* Memory-mapped file handle */
  char *text_region;                        /* Pointer to text area (entire file is text) */
  size_t text_capacity;                     /* Total file size */
  _Atomic uint64_t write_pos;               /* Current write position (in memory only) */
  bool initialized;                         /* Initialization flag */
  char file_path[PLATFORM_MAX_PATH_LENGTH]; /* Path to log file for truncation */

  /* Statistics */
  _Atomic uint64_t bytes_written;
  _Atomic uint64_t wrap_count;
} g_mmap_log = {
    .initialized = false,
    .file_path = {0},
};

/* ============================================================================
 * Time Utilities
 * ============================================================================ */

static void format_timestamp(char *buf, size_t buf_size) {
  uint64_t current_time_ns = time_get_realtime_ns();
  time_t seconds = (time_t)(current_time_ns / NS_PER_SEC_INT);
  uint64_t microseconds = time_ns_to_us(current_time_ns % NS_PER_SEC_INT);

  struct tm tm_info;
  platform_localtime(&seconds, &tm_info);

  size_t len = strftime(buf, buf_size, "%H:%M:%S", &tm_info);
  if (len > 0 && len < buf_size - 10) {
    snprintf(buf + len, buf_size - len, ".%06llu", (unsigned long long)microseconds);
  }
}

/* ============================================================================
 * Signal Handlers for Crash Safety
 * ============================================================================ */

static volatile sig_atomic_t g_crash_in_progress = 0;

#ifndef _WIN32
static void crash_signal_handler(int sig) {
  /* Prevent recursive crashes */
  if (g_crash_in_progress) {
    _exit(128 + sig);
  }
  g_crash_in_progress = 1;

  /* Write crash marker directly to mmap'd log */
  if (g_mmap_log.initialized && g_mmap_log.text_region) {
    const char *crash_msg = "\n=== CRASH DETECTED (signal %d) ===\n";
    char msg_buf[64];
    int len = snprintf(msg_buf, sizeof(msg_buf), crash_msg, sig);
    if (len > 0) {
      uint64_t pos = atomic_fetch_add(&g_mmap_log.write_pos, (uint64_t)len);
      if (pos + (uint64_t)len <= g_mmap_log.text_capacity) {
        memcpy(g_mmap_log.text_region + pos, msg_buf, (size_t)len);
      }
    }
  }

  /* Sync mmap to disk */
  if (g_mmap_log.initialized) {
    platform_mmap_sync(&g_mmap_log.mmap, true); /* sync = true for immediate flush */
  }

  /* Re-raise with default handler for core dump */
  signal(sig, SIG_DFL);
  raise(sig);
}
#endif /* !_WIN32 */

#ifdef _WIN32
/**
 * @brief Windows unhandled exception filter for crash safety
 *
 * Writes crash marker and syncs mmap to disk before Windows terminates the process.
 */
static LONG WINAPI windows_crash_handler(EXCEPTION_POINTERS *exception_info) {
  (void)exception_info; /* Unused for now, could log exception code */

  /* Prevent recursive crashes */
  if (g_crash_in_progress) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  g_crash_in_progress = 1;

  /* Write crash marker directly to mmap'd log */
  if (g_mmap_log.initialized && g_mmap_log.text_region) {
    DWORD exception_code = exception_info ? exception_info->ExceptionRecord->ExceptionCode : 0;
    const char *crash_msg = "\n=== CRASH DETECTED (exception 0x%08lX) ===\n";
    char msg_buf[64];
    int len = snprintf(msg_buf, sizeof(msg_buf), crash_msg, (unsigned long)exception_code);
    if (len > 0) {
      uint64_t pos = atomic_fetch_add(&g_mmap_log.write_pos, (uint64_t)len);
      if (pos + (uint64_t)len <= g_mmap_log.text_capacity) {
        memcpy(g_mmap_log.text_region + pos, msg_buf, (size_t)len);
      }
    }
  }

  /* Sync mmap to disk */
  if (g_mmap_log.initialized) {
    platform_mmap_sync(&g_mmap_log.mmap, true);
  }

  /* Let Windows continue with default crash handling (creates minidump if configured) */
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif /* _WIN32 */

void log_mmap_install_crash_handlers(void) {
#ifndef _WIN32
  struct sigaction sa = {0};
  sa.sa_handler = crash_signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = (int)SA_RESETHAND; /* One-shot */

  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGABRT, &sa, NULL);
  sigaction(SIGBUS, &sa, NULL);
  sigaction(SIGFPE, &sa, NULL);
  sigaction(SIGILL, &sa, NULL);
#else
  SetUnhandledExceptionFilter(windows_crash_handler);
#endif
}

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * @brief Find the end of existing log content by scanning for last newline
 *
 * Scans backwards from the end to find where actual content ends.
 * Content ends at the last newline before trailing spaces/nulls.
 */
static size_t find_content_end(const char *text, size_t capacity) {
  /* Scan backwards to find last non-space/non-null byte */
  size_t pos = capacity;
  while (pos > 0 && (text[pos - 1] == ' ' || text[pos - 1] == '\0' || text[pos - 1] == '\n')) {
    pos--;
  }

  /* Now find the newline after the last content */
  while (pos < capacity && text[pos] != '\n' && text[pos] != ' ' && text[pos] != '\0') {
    pos++;
  }

  /* Include the newline if present */
  if (pos < capacity && text[pos] == '\n') {
    pos++;
  }

  return pos;
}

asciichat_error_t log_mmap_init(const log_mmap_config_t *config) {
  if (!config || !config->log_path) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "mmap log: config or log_path is NULL");
  }

  if (g_mmap_log.initialized) {
    log_warn("mmap log: already initialized, destroying first");
    log_mmap_destroy();
  }

  /* Determine file size */
  size_t file_size = config->max_size > 0 ? config->max_size : LOG_MMAP_DEFAULT_SIZE;
  if (file_size < 1024) {
    file_size = 1024; /* Minimum reasonable size */
  }

  /* Store file path for later truncation */
  SAFE_STRNCPY(g_mmap_log.file_path, config->log_path, sizeof(g_mmap_log.file_path) - 1);

  /* Open mmap file */
  platform_mmap_init(&g_mmap_log.mmap);
  asciichat_error_t result = platform_mmap_open(config->log_path, file_size, &g_mmap_log.mmap);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  /* Entire file is text - no header */
  g_mmap_log.text_region = (char *)g_mmap_log.mmap.addr;
  g_mmap_log.text_capacity = file_size;

  /* Find where existing content ends (scan for last newline before spaces/nulls) */
  size_t existing_pos = find_content_end(g_mmap_log.text_region, file_size);
  atomic_store(&g_mmap_log.write_pos, existing_pos);

  /* Clear unused portion with newlines (grep-friendly without needing -a flag)
   * We truncate the file on clean shutdown to save space */
  if (existing_pos < file_size) {
    memset(g_mmap_log.text_region + existing_pos, '\n', file_size - existing_pos);
  }

  if (existing_pos > 0) {
    log_info("mmap log: resumed existing log at position %zu", existing_pos);
  } else {
    log_info("mmap log: created new log file %s (%zu bytes)", config->log_path, file_size);
  }

  /* Reset statistics */
  atomic_store(&g_mmap_log.bytes_written, 0);
  atomic_store(&g_mmap_log.wrap_count, 0);

  /* Install crash handlers */
  log_mmap_install_crash_handlers();

  g_mmap_log.initialized = true;

  /* Write startup marker */
  log_mmap_write(1 /* LOG_INFO */, NULL, 0, NULL, "=== Log started (mmap text mode, %zu bytes) ===", file_size);

  return ASCIICHAT_OK;
}

asciichat_error_t log_mmap_init_simple(const char *log_path, size_t max_size) {
  log_mmap_config_t config = {
      .log_path = log_path,
      .max_size = max_size,
  };
  return log_mmap_init(&config);
}

void log_mmap_destroy(void) {
  if (!g_mmap_log.initialized) {
    return;
  }

  /* Write shutdown marker */
  log_mmap_write(1 /* LOG_INFO */, NULL, 0, NULL, "=== Log ended ===");

  /* Sync to disk */
  platform_mmap_sync(&g_mmap_log.mmap, true);

  /* Truncate file to actual content size to save space
   * This converts the large mmap file (4MB with newlines) to just the actual log content */
  uint64_t final_pos = atomic_load(&g_mmap_log.write_pos);
  if (final_pos < g_mmap_log.text_capacity && strlen(g_mmap_log.file_path) > 0) {
#ifdef _WIN32
    /* Windows: Close mmap first, then truncate file, then reopen for truncation */
    platform_mmap_close(&g_mmap_log.mmap);
    HANDLE hFile =
        CreateFileA(g_mmap_log.file_path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
      LARGE_INTEGER size;
      size.QuadPart = (LONGLONG)final_pos;
      if (SetFilePointerEx(hFile, size, NULL, FILE_BEGIN)) {
        SetEndOfFile(hFile);
      }
      CloseHandle(hFile);
      log_debug("mmap log: truncated %s to %zu bytes (was %zu MB)", g_mmap_log.file_path, (size_t)final_pos,
                g_mmap_log.text_capacity / 1024 / 1024);
    }
#else
    /* POSIX: Use ftruncate() on the file descriptor */
    if (g_mmap_log.mmap.fd >= 0) {
      if (ftruncate(g_mmap_log.mmap.fd, (off_t)final_pos) == 0) {
        log_debug("mmap log: truncated %s to %zu bytes (was %zu MB)", g_mmap_log.file_path, (size_t)final_pos,
                  g_mmap_log.text_capacity / 1024 / 1024);
      }
    }
    platform_mmap_close(&g_mmap_log.mmap);
#endif
  } else {
    /* No truncation needed or path not set */
    platform_mmap_close(&g_mmap_log.mmap);
  }

  g_mmap_log.text_region = NULL;
  g_mmap_log.file_path[0] = '\0';

  g_mmap_log.initialized = false;
  log_debug("mmap log: destroyed");
}

void log_mmap_write(int level, const char *file, int line, const char *func, const char *fmt, ...) {
  if (!g_mmap_log.initialized || !g_mmap_log.text_region) {
    return;
  }

  static const char *level_names[] = {"DEV", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
  const char *level_name = (level >= 0 && level < 6) ? level_names[level] : "???";

  /* Format the complete log line into a local buffer first */
  char line_buf[LOG_MMAP_MSG_BUFFER_SIZE];
  char time_buf[LOG_TIMESTAMP_BUFFER_SIZE];
  format_timestamp(time_buf, sizeof(time_buf));

  int prefix_len;
  if (file && func) {
    prefix_len =
        snprintf(line_buf, sizeof(line_buf), "[%s] [%s] %s:%d in %s(): ", time_buf, level_name, file, line, func);
  } else {
    prefix_len = snprintf(line_buf, sizeof(line_buf), "[%s] [%s] ", time_buf, level_name);
  }

  if (prefix_len < 0) {
    return;
  }

  /* Format the message */
  va_list args;
  va_start(args, fmt);
  int msg_len = vsnprintf(line_buf + prefix_len, sizeof(line_buf) - (size_t)prefix_len - 1, fmt, args);
  va_end(args);

  if (msg_len < 0) {
    return;
  }

  /* Add newline */
  size_t total_len = (size_t)prefix_len + (size_t)msg_len;
  if (total_len >= sizeof(line_buf) - 1) {
    total_len = sizeof(line_buf) - 2; /* Truncate if too long */
  }
  line_buf[total_len] = '\n';
  total_len++;
  line_buf[total_len] = '\0';

  /* Strip ANSI escape codes from the log message before writing to file */
  char *stripped = ansi_strip_escapes(line_buf, total_len);
  const char *write_buf = stripped ? stripped : line_buf;
  size_t write_len = stripped ? strlen(stripped) : total_len;

  /* Atomically claim space in the mmap'd region */
  uint64_t pos = atomic_fetch_add(&g_mmap_log.write_pos, write_len);

  /* Check if we exceeded capacity - drop this message if so */
  /* Rotation is handled by maybe_rotate_log() called from logging.c */
  if (pos + write_len > g_mmap_log.text_capacity) {
    /* Undo our claim - we can't fit */
    atomic_fetch_sub(&g_mmap_log.write_pos, write_len);
    if (stripped) {
      SAFE_FREE(stripped);
    }
    return;
  }

  /* Copy formatted text to mmap'd region */
  memcpy(g_mmap_log.text_region + pos, write_buf, write_len);

  atomic_fetch_add(&g_mmap_log.bytes_written, write_len);

  if (stripped) {
    SAFE_FREE(stripped);
  }

  /* Sync for ERROR/FATAL to ensure visibility on crash */
  if (level >= 4 /* LOG_ERROR */) {
    platform_mmap_sync(&g_mmap_log.mmap, false);
  }
}

bool log_mmap_is_active(void) {
  return g_mmap_log.initialized;
}

void log_mmap_sync(void) {
  if (g_mmap_log.initialized) {
    platform_mmap_sync(&g_mmap_log.mmap, true);
  }
}

void log_mmap_get_stats(uint64_t *bytes_written, uint64_t *wrap_count) {
  if (bytes_written) {
    *bytes_written = atomic_load(&g_mmap_log.bytes_written);
  }
  if (wrap_count) {
    *wrap_count = atomic_load(&g_mmap_log.wrap_count);
  }
}

bool log_mmap_get_usage(size_t *used, size_t *capacity) {
  if (!g_mmap_log.initialized) {
    return false;
  }

  if (used) {
    *used = (size_t)atomic_load(&g_mmap_log.write_pos);
  }
  if (capacity) {
    *capacity = g_mmap_log.text_capacity;
  }
  return true;
}

void log_mmap_rotate(void) {
  if (!g_mmap_log.initialized || !g_mmap_log.text_region) {
    return;
  }

  /* NOTE: Caller must hold the rotation mutex from logging.c */

  uint64_t current_pos = atomic_load(&g_mmap_log.write_pos);
  size_t capacity = g_mmap_log.text_capacity;

  /* Keep last 2/3 of the log (same ratio as file rotation) */
  size_t keep_size = capacity * 2 / 3;
  if (current_pos <= keep_size) {
    return;
  }

  /* Find where to start keeping (skip to beginning of current_pos - keep_size) */
  size_t skip_bytes = (size_t)current_pos - keep_size;
  char *keep_start = g_mmap_log.text_region + skip_bytes;

  /* Skip to next line boundary to avoid partial lines */
  size_t skipped = 0;
  while (skipped < keep_size && *keep_start != '\n') {
    keep_start++;
    skipped++;
  }
  if (skipped < keep_size && *keep_start == '\n') {
    keep_start++;
    skipped++;
  }

  size_t actual_keep = keep_size - skipped;
  if (actual_keep == 0) {
    /* Nothing to keep - just reset */
    atomic_store(&g_mmap_log.write_pos, 0);
    memset(g_mmap_log.text_region, '\n', capacity);
    return;
  }

  /* Move the tail to the beginning using memmove (handles overlap) */
  memmove(g_mmap_log.text_region, keep_start, actual_keep);

  /* Clear the rest with newlines (grep-friendly without needing -a flag) */
  memset(g_mmap_log.text_region + actual_keep, '\n', capacity - actual_keep);

  /* Update write position */
  atomic_store(&g_mmap_log.write_pos, actual_keep);

  /* Write rotation marker */
  const char *rotate_msg = "\n=== LOG ROTATED ===\n";
  size_t rotate_len = strlen(rotate_msg);
  if (actual_keep + rotate_len < capacity) {
    memcpy(g_mmap_log.text_region + actual_keep, rotate_msg, rotate_len);
    atomic_store(&g_mmap_log.write_pos, actual_keep + rotate_len);
  }

  atomic_fetch_add(&g_mmap_log.wrap_count, 1);

  /* Sync after rotation */
  platform_mmap_sync(&g_mmap_log.mmap, true);
}
