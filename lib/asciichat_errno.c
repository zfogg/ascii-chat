/**
 * @file asciichat_errno.c
 * @ingroup errno
 * @brief 🚨 Custom error code system with formatted messages, thread-local storage, and errno mapping
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#ifndef _WIN32
#include <errno.h>
#endif

#include "asciichat_errno.h"
#include "util/path.h"
#include "platform/system.h"
#include "common.h"
#include "logging.h"

/* ============================================================================
 * Thread-Local Storage
 * ============================================================================
 */

__thread asciichat_error_context_t asciichat_errno_context = {.code = ASCIICHAT_OK,
                                                              .file = NULL,
                                                              .line = 0,
                                                              .function = NULL,
                                                              .context_message = NULL,
                                                              .timestamp = 0,
                                                              .system_errno = 0,
                                                              .backtrace = {0},
                                                              .backtrace_symbols = NULL,
                                                              .stack_depth = 0,
                                                              .has_system_error = false};

__thread asciichat_error_t asciichat_errno = ASCIICHAT_OK;

// Suppression flag to prevent error context allocation during cleanup
static bool g_suppress_error_context = false;

/* ============================================================================
 * Error Statistics
 * ============================================================================
 */

static asciichat_error_stats_t error_stats = {0};
static bool stats_initialized = false;

/* ============================================================================
 * Thread-Safe Error Storage
 * ============================================================================
 */

#define MAX_THREAD_ERRORS 64
static struct {
  int thread_id;
  asciichat_error_t error_code;
  bool valid;
} thread_errors[MAX_THREAD_ERRORS] = {0};

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

static uint64_t get_timestamp_microseconds(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
    // Convert seconds and nanoseconds to microseconds
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000);
  }
  return 0;
}

static void capture_backtrace(void **backtrace, char ***backtrace_symbols, int *stack_depth) {
#ifndef NDEBUG // Capture in Debug and Dev modes
  *stack_depth = platform_backtrace(backtrace, 32);
  *backtrace_symbols = platform_backtrace_symbols(backtrace, *stack_depth);
#else
  (void)backtrace;
  (void)backtrace_symbols;
  *stack_depth = 0;
#endif
}

static bool skip_backtrace_frame(const char *frame) {
  return (strstr(frame, "BaseThreadInitThunk") != NULL || strstr(frame, "RtlUserThreadStart") != NULL ||
          strstr(frame, "__scrt_common_main_seh") != NULL || strstr(frame, "capture_backtrace") != NULL ||
          strstr(frame, "asciichat_set_errno") != NULL || strstr(frame, "asciichat_set_errno_with_message") != NULL ||
          strstr(frame, "SET_ERRNO") != NULL || strstr(frame, "platform_backtrace") != NULL ||
          strstr(frame, "asciichat_fatal_with_context") != NULL ||
          strstr(frame, "asciichat_print_error_context") != NULL);
}

void log_labeled(const char *label, logging_color_t color, const char *message, ...) {
  va_list args;
  va_start(args, message);
  char *formatted_message = format_message(message, args);
  va_end(args);

  safe_fprintf(stderr, "%s%s%s: %s\n", log_level_color(color), label, log_level_color(LOGGING_COLOR_RESET),
               formatted_message);

  log_file("%s: %s", label, formatted_message);
}

/* ============================================================================
 * Core Error Setting Functions
 * ============================================================================
 */

void asciichat_set_errno(asciichat_error_t code, const char *file, int line, const char *function,
                         const char *context_message) {
  // Suppress error context allocation during cleanup to prevent leaks
  if (g_suppress_error_context) {
    return;
  }

  // Clear any existing context message
  if (asciichat_errno_context.context_message) {
    SAFE_FREE(asciichat_errno_context.context_message);
    asciichat_errno_context.context_message = NULL;
  }

  // Set the error context
  asciichat_errno_context.code = code;
  asciichat_errno_context.file = file;
  asciichat_errno_context.line = line;
  asciichat_errno_context.function = function;
  asciichat_errno_context.timestamp = get_timestamp_microseconds();
  asciichat_errno_context.has_system_error = false;

  // Set the simple error code variable
  asciichat_errno = code;

  // Copy context message if provided
  if (context_message == NULL) {
    log_error("context_message is NULL");
    const char *fallback = "No context message (this is invalid - set a context message)";
    size_t len = strlen(fallback) + 1;
    asciichat_errno_context.context_message = SAFE_MALLOC(len, char *);
    if (asciichat_errno_context.context_message) {
      SAFE_STRNCPY(asciichat_errno_context.context_message, fallback, len);
    } else {
      log_error("SAFE_MALLOC failed for fallback context_message");
    }
  } else {
    size_t len = strlen(context_message) + 1;
    asciichat_errno_context.context_message = SAFE_MALLOC(len, char *);
    if (asciichat_errno_context.context_message) {
      SAFE_STRNCPY(asciichat_errno_context.context_message, context_message, len);
    } else {
      log_error("SAFE_MALLOC failed for context_message");
    }
  }

  // Capture stack trace in debug builds
  if (asciichat_errno_context.backtrace_symbols != NULL) {
    platform_backtrace_symbols_free(asciichat_errno_context.backtrace_symbols);
    asciichat_errno_context.backtrace_symbols = NULL;
  }
  capture_backtrace(asciichat_errno_context.backtrace, &asciichat_errno_context.backtrace_symbols,
                    &asciichat_errno_context.stack_depth);

  // Record in statistics
  asciichat_error_stats_record(code);
}

void asciichat_set_errno_with_message(asciichat_error_t code, const char *file, int line, const char *function,
                                      const char *format, ...) {
  va_list args;
  va_start(args, format);

  char *context_message = format_message(format, args);
  asciichat_set_errno(code, file, line, function, context_message);

  if (context_message) {
    SAFE_FREE(context_message);
  }

  va_end(args);
}

void asciichat_set_errno_with_system_error(asciichat_error_t code, const char *file, int line, const char *function,
                                           int sys_errno) {
  asciichat_set_errno(code, file, line, function, NULL);
  asciichat_errno_context.system_errno = sys_errno;
  asciichat_errno_context.has_system_error = true;
}

void asciichat_set_errno_with_system_error_and_message(asciichat_error_t code, const char *file, int line,
                                                       const char *function, int sys_errno, const char *format, ...) {
  va_list args;
  va_start(args, format);

  char *context_message = format_message(format, args);
  asciichat_set_errno(code, file, line, function, context_message);
  asciichat_errno_context.system_errno = sys_errno;
  asciichat_errno_context.has_system_error = true;

  if (context_message) {
    SAFE_FREE(context_message);
  }

  va_end(args);
}

void asciichat_set_errno_with_wsa_error(asciichat_error_t code, const char *file, int line, const char *function,
                                        int wsa_error) {
  asciichat_set_errno(code, file, line, function, NULL);
  asciichat_errno_context.wsa_error = wsa_error;
  asciichat_errno_context.has_wsa_error = true;
}

bool asciichat_has_wsa_error(void) {
  return asciichat_errno_context.has_wsa_error;
}

/* ============================================================================
 * Error Checking and Clearing Functions
 * ============================================================================
 */

bool asciichat_has_errno(asciichat_error_context_t *context) {
  if (asciichat_errno_context.code == ASCIICHAT_OK) {
    return false;
  }

  if (context) {
    *context = asciichat_errno_context;
  }

  return true;
}

void asciichat_clear_errno(void) {
  if (asciichat_errno_context.context_message) {
    SAFE_FREE(asciichat_errno_context.context_message);
    asciichat_errno_context.context_message = NULL;
  }

  if (asciichat_errno_context.backtrace_symbols != NULL) {
    platform_backtrace_symbols_free(asciichat_errno_context.backtrace_symbols);
    asciichat_errno_context.backtrace_symbols = NULL;
  }

  memset(&asciichat_errno_context, 0, sizeof(asciichat_errno_context));
  asciichat_errno_context.code = ASCIICHAT_OK;
  asciichat_errno_context.system_errno = 0;
  asciichat_errno_context.has_system_error = false;
  asciichat_errno_context.has_wsa_error = false;

#ifdef _WIN32
  WSASetLastError(0);
  asciichat_errno_context.wsa_error = 0;
#endif

  errno = 0;
}

asciichat_error_t asciichat_get_errno(void) {
  return asciichat_errno_context.code;
}

/* ============================================================================
 * Enhanced FATAL Functions
 * ============================================================================
 */

void asciichat_fatal_with_context(asciichat_error_t code, const char *file, int line, const char *function,
                                  const char *format, ...) {
  (void)file;
  (void)line;
  (void)function;

  // Print library error context if available
  asciichat_error_context_t err_ctx;
  if (HAS_ERRNO(&err_ctx)) {
    log_labeled("\nasciichat_errno: libary code error context", LOGGING_COLOR_ERROR, "");
    asciichat_print_error_context(&err_ctx);
  } else {
    log_plain("WARNING: No error context found (asciichat_errno_context.code=%d)", asciichat_errno_context.code);
  }

  safe_fprintf(stderr, "\n");
  log_labeled("FATAL ERROR", LOGGING_COLOR_FATAL, "exit code %d (%s)", (int)code, asciichat_error_string(code));
#ifndef NDEBUG
  const char *relative_file = extract_project_relative_path(file);
  log_plain("  Location: %s:%d in %s()", relative_file, line, function);
#endif

  if (format) {
    va_list args;
    va_start(args, format);
    const char *formatted_message = format_message(format, args);
    log_plain("  Error message: %s", formatted_message);
    va_end(args);
  }

#ifndef NDEBUG
  // Always print platform backtrace in debug/dev builds
  void *buffer[32];
  int size = platform_backtrace(buffer, 32);
  if (size > 0) {
    log_labeled("\nFATAL BACKTRACE", LOGGING_COLOR_FATAL, "");
    char **symbols = platform_backtrace_symbols(buffer, size);
    if (symbols) {
      for (int i = 0; i < size; i++) {
        if (symbols[i] && !skip_backtrace_frame(symbols[i])) {
          log_plain("  [%s%d%s] %s", log_level_color(LOGGING_COLOR_FATAL), i, log_level_color(LOGGING_COLOR_RESET),
                    symbols[i]);
        }
      }
      platform_backtrace_symbols_free(symbols);
    }
  }
#endif

  exit(code);
}

/* ============================================================================
 * Error Context Printing Functions
 * ============================================================================
 */

void asciichat_print_error_context(const asciichat_error_context_t *context) {
  if (!context || context->code == ASCIICHAT_OK) {
    return;
  }

  if (context->file && context->line && context->function) {
    log_plain("  Location: %s:%d in %s()", extract_project_relative_path(context->file), context->line,
              context->function);
  } else {
    log_plain("  Location: unknown (set by system code)");
  }

  if (context->context_message) {
    safe_fprintf(stderr, "%s  Context:%s %s\n", log_level_color(LOGGING_COLOR_WARN),
                 log_level_color(LOGGING_COLOR_RESET), context->context_message);
    log_file("  Context: %s", context->context_message);
  }

  if (context->has_system_error) {
    log_plain("  System error: %s (code: %d, meaning: %s)", SAFE_STRERROR(context->system_errno), context->system_errno,
              strerror(context->system_errno));
  }

  // Print timestamp
  if (context->timestamp > 0) {
    time_t sec = (time_t)(context->timestamp / 1000000);
    long usec = (long)(context->timestamp % 1000000);
    struct tm *tm_info = localtime(&sec);
    if (tm_info) {
      char time_str[64];
      (void)strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
      log_plain("  Timestamp: %s.%06ld", time_str, usec);
    }
  }

  // Print stack trace from library error
  if (context->stack_depth > 0) {
    log_labeled("\nBacktrace from library error", LOGGING_COLOR_ERROR, "");
    // Print stack trace starting from the first non-internal frame
    for (int i = 0; i < context->stack_depth; i++) {
      if (context->backtrace_symbols && context->backtrace_symbols[i] &&
          !skip_backtrace_frame(context->backtrace_symbols[i])) {
        log_plain("  [%s%d%s] %s", log_level_color(LOGGING_COLOR_FATAL), i, log_level_color(LOGGING_COLOR_RESET),
                  context->backtrace_symbols[i]);
      }
    }
  }
}

/* ============================================================================
 * Error Statistics Functions
 * ============================================================================
 */

void asciichat_error_stats_init(void) {
  if (!stats_initialized) {
    memset(&error_stats, 0, sizeof(error_stats));
    stats_initialized = true;
  }
}

void asciichat_error_stats_record(asciichat_error_t code) {
  if (!stats_initialized) {
    asciichat_error_stats_init();
  }

  if (code >= 0 && code < 256) {
    error_stats.error_counts[code]++;
  }
  error_stats.total_errors++;
  error_stats.last_error_time = get_timestamp_microseconds();
  error_stats.last_error_code = code;
}

void asciichat_error_stats_print(void) {
  if (!stats_initialized || error_stats.total_errors == 0) {
    log_plain("No errors recorded.\n");
    return;
  }

  log_plain("\n=== ascii-chat Error Statistics ===\n");
  log_plain("Total errors: %llu\n", (unsigned long long)error_stats.total_errors);

  if (error_stats.last_error_time > 0) {
    time_t sec = (time_t)(error_stats.last_error_time / 1000000);
    struct tm *tm_info = localtime(&sec);
    if (tm_info) {
      char time_str[64];
      size_t result6 = strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
      (void)result6; // Suppress unused result warning
      log_plain("Last error: %s (code %d)\n", time_str, (int)error_stats.last_error_code);
    }
  }

  log_plain("\nError breakdown:\n");
  for (int i = 0; i < 256; i++) {
    if (error_stats.error_counts[i] > 0) {
      log_plain("  %3d (%s): %llu\n", i, asciichat_error_string((asciichat_error_t)i),
                (unsigned long long)error_stats.error_counts[i]);
    }
  }
  log_plain("\n");
}

void asciichat_error_stats_reset(void) {
  memset(&error_stats, 0, sizeof(error_stats));
}

asciichat_error_stats_t asciichat_error_stats_get(void) {
  if (!stats_initialized) {
    asciichat_error_stats_init();
  }
  return error_stats;
}

/* ============================================================================
 * Thread-Safe Error Propagation Functions
 * ============================================================================
 */

asciichat_error_t asciichat_get_thread_error(int thread_id) {
  for (int i = 0; i < MAX_THREAD_ERRORS; i++) {
    if (thread_errors[i].valid && thread_errors[i].thread_id == thread_id) {
      return thread_errors[i].error_code;
    }
  }
  return ASCIICHAT_OK;
}

void asciichat_set_thread_error(int thread_id, asciichat_error_t code) {
  // Find existing entry or empty slot
  int slot = -1;
  for (int i = 0; i < MAX_THREAD_ERRORS; i++) {
    if (thread_errors[i].valid && thread_errors[i].thread_id == thread_id) {
      slot = i;
      break;
    }
    if (!thread_errors[i].valid && slot == -1) {
      slot = i;
    }
  }

  if (slot >= 0) {
    thread_errors[slot].thread_id = thread_id;
    thread_errors[slot].error_code = code;
    thread_errors[slot].valid = true;
  }
}

void asciichat_clear_thread_error(int thread_id) {
  for (int i = 0; i < MAX_THREAD_ERRORS; i++) {
    if (thread_errors[i].valid && thread_errors[i].thread_id == thread_id) {
      thread_errors[i].valid = false;
      break;
    }
  }
}

/* ============================================================================
 * Cleanup
 * ============================================================================
 */

void asciichat_errno_suppress(bool suppress) {
  g_suppress_error_context = suppress;
}

void asciichat_errno_cleanup(void) {
  if (asciichat_errno_context.backtrace_symbols != NULL) {
    platform_backtrace_symbols_free(asciichat_errno_context.backtrace_symbols);
    asciichat_errno_context.backtrace_symbols = NULL;
  }

  if (asciichat_errno_context.context_message != NULL) {
    SAFE_FREE(asciichat_errno_context.context_message);
  }

  // Reset the context to a clean state
  memset(&asciichat_errno_context, 0, sizeof(asciichat_errno_context));
  asciichat_errno_context.code = ASCIICHAT_OK;

  // Suppress any further error context allocation to prevent cleanup-phase leaks
  // This prevents other atexit() functions from allocating new contexts after we've cleaned up
  g_suppress_error_context = true;
}
