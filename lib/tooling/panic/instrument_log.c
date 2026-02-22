// SPDX-License-Identifier: MIT
// Debug instrumentation logging runtime for ascii-chat line tracing

#include <ascii-chat/tooling/panic/instrument_log.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/util.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/util/time.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#if !defined(_WIN32)
#define ASCII_INSTR_SOURCE_PRINT_HAVE_REGEX 1
#include <regex.h>
#else
#define ASCII_INSTR_SOURCE_PRINT_HAVE_REGEX 0
// Windows doesn't have mode_t
typedef int mode_t;
#endif

#ifdef _WIN32
#include <direct.h>
#ifndef mkdir
#define mkdir(path, mode) _mkdir(path)
#endif
// Windows uses _write instead of write
#define posix_write _write
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif
#else
#define posix_write write
#endif

#ifndef ASCII_INSTR_SOURCE_PRINT_DEFAULT_BASENAME
#define ASCII_INSTR_SOURCE_PRINT_DEFAULT_BASENAME "ascii-instr"
#endif

#ifndef ASCII_INSTR_SOURCE_PRINT_MAX_LINE
#define ASCII_INSTR_SOURCE_PRINT_MAX_LINE 4096
#endif

#ifndef ASCII_INSTR_SOURCE_PRINT_MAX_SNIPPET
#define ASCII_INSTR_SOURCE_PRINT_MAX_SNIPPET 2048
#endif

typedef enum asciichat_instr_selector_type {
  ASCII_INSTR_SOURCE_PRINT_SELECTOR_FILE_SUBSTRING = 0,
  ASCII_INSTR_SOURCE_PRINT_SELECTOR_FILE_GLOB = 1,
  ASCII_INSTR_SOURCE_PRINT_SELECTOR_FUNCTION_GLOB = 2,
  ASCII_INSTR_SOURCE_PRINT_SELECTOR_MODULE = 3,
} asciichat_instr_selector_type_t;

/**
 * @brief Selector for filtering instrumentation output by file, function, or module
 */
typedef struct asciichat_instr_only_selector {
  asciichat_instr_selector_type_t type; ///< Type of selector (file substring, glob, function glob, or module)
  char *pattern;                        ///< Pattern to match (file/function glob pattern)
  char *module;                         ///< Module name for module-based filtering
} asciichat_instr_only_selector_t;

/**
 * @brief Dynamic array of instrumentation selectors for filtering output
 */
typedef struct asciichat_instr_only_list {
  asciichat_instr_only_selector_t *items; ///< Array of selectors
  size_t count;                           ///< Number of active selectors
  size_t capacity;                        ///< Allocated capacity
} asciichat_instr_only_list_t;

/**
 * @brief Per-thread instrumentation runtime state
 *
 * Tracks state for source code instrumentation logging, including file descriptors,
 * filter configuration, and rate limiting for the current thread.
 */
typedef struct asciichat_instr_runtime {
  int fd;                              ///< Log file descriptor (-1 if not open)
  int pid;                             ///< Process ID
  uint64_t thread_id;                  ///< Thread ID
  uint64_t sequence;                   ///< Sequence number for log entries
  uint64_t call_counter;               ///< Total number of instrumentation calls
  char log_path[PATH_MAX];             ///< Path to log file
  bool filters_enabled;                ///< Whether any filters are active
  const char *filter_include;          ///< File path substring to include (from env var)
  const char *filter_exclude;          ///< File path substring to exclude (from env var)
  const char *filter_function_include; ///< Function name substring to include (from env var)
  const char *filter_function_exclude; ///< Function name substring to exclude (from env var)
  const char *filter_thread;           ///< Thread ID filter (from env var)
#if ASCII_INSTR_SOURCE_PRINT_HAVE_REGEX
  regex_t include_regex;             ///< Compiled regex for file path inclusion
  bool include_regex_valid;          ///< Whether include_regex was successfully compiled
  regex_t exclude_regex;             ///< Compiled regex for file path exclusion
  bool exclude_regex_valid;          ///< Whether exclude_regex was successfully compiled
  regex_t function_include_regex;    ///< Compiled regex for function name inclusion
  bool function_include_regex_valid; ///< Whether function_include_regex was successfully compiled
  regex_t function_exclude_regex;    ///< Compiled regex for function name exclusion
  bool function_exclude_regex_valid; ///< Whether function_exclude_regex was successfully compiled
#endif
  uint32_t rate;                              ///< Rate limiting: log every Nth call
  bool rate_enabled;                          ///< Whether rate limiting is enabled
  bool stderr_fallback;                       ///< Use stderr if log file can't be opened
  asciichat_instr_only_list_t only_selectors; ///< List of "only" selectors for filtering
} asciichat_instr_runtime_t;

static tls_key_t g_runtime_key;
static static_mutex_t g_runtime_mutex = STATIC_MUTEX_INIT;
static bool g_runtime_initialized = false;
static char g_output_dir[PATH_MAX];
static bool g_output_dir_set = false;
static bool g_disable_write = false;
static uint64_t g_start_ns = 0;
static bool g_ticks_initialized = false;
static bool g_coverage_enabled = false;
static bool g_echo_to_stderr = false;
static bool g_echo_to_stderr_initialized = false;

static void asciichat_instr_runtime_init_once(void);
static void asciichat_instr_runtime_tls_destructor(void *ptr);
static int asciichat_instr_open_log_file(asciichat_instr_runtime_t *runtime);
static void asciichat_instr_runtime_configure(asciichat_instr_runtime_t *runtime);
static bool asciichat_instr_should_log(const asciichat_instr_runtime_t *runtime, const char *file_path,
                                       uint32_t line_number, const char *function_name);
static int asciichat_instr_write_full(int fd, const char *buffer, size_t len);
static bool asciichat_instr_env_is_enabled(const char *value);
static bool asciichat_instr_parse_positive_uint32(const char *value, uint32_t *out_value);
#if ASCII_INSTR_SOURCE_PRINT_HAVE_REGEX
static bool asciichat_instr_compile_regex(regex_t *regex, const char *pattern);
#endif
static void asciichat_instr_only_list_destroy(asciichat_instr_only_list_t *list);
static bool asciichat_instr_only_list_append(asciichat_instr_only_list_t *list, asciichat_instr_selector_type_t type,
                                             const char *module, const char *pattern);
static void asciichat_instr_trim(char *value);
static bool asciichat_instr_parse_only_filters(asciichat_instr_runtime_t *runtime, const char *value);
static bool asciichat_instr_only_selectors_match(const asciichat_instr_runtime_t *runtime, const char *file_path,
                                                 const char *function_name);
static bool asciichat_instr_match_glob(const char *pattern, const char *value);
static const char *asciichat_instr_basename(const char *path);
static bool asciichat_instr_path_contains_module(const char *file_path, const char *module_name);

static _Thread_local bool g_logging_reentry_guard = false;
static bool g_instrumentation_enabled = false;
static bool g_instrumentation_enabled_checked = false;

asciichat_instr_runtime_t *asciichat_instr_runtime_get(void) {
  if (g_disable_write) {
    return NULL;
  }

  // Initialize runtime once using mutex-protected initialization
  if (!g_runtime_initialized) {
    static_mutex_lock(&g_runtime_mutex);
    if (!g_runtime_initialized) {
      asciichat_instr_runtime_init_once();
    }
    static_mutex_unlock(&g_runtime_mutex);
  }

  asciichat_instr_runtime_t *runtime = ascii_tls_get(g_runtime_key);
  if (runtime != NULL) {
    return runtime;
  }

  runtime = SAFE_CALLOC(1, sizeof(*runtime), asciichat_instr_runtime_t *);
  if (runtime == NULL) {
    return NULL;
  }

  runtime->pid = platform_get_pid();
  runtime->thread_id = asciichat_thread_current_id();
  runtime->sequence = 0;
  runtime->call_counter = 0;
  runtime->fd = -1;
  runtime->filter_include = SAFE_GETENV("ASCII_INSTR_SOURCE_PRINT_INCLUDE");
  runtime->filter_exclude = SAFE_GETENV("ASCII_INSTR_SOURCE_PRINT_EXCLUDE");
  runtime->filter_thread = SAFE_GETENV("ASCII_INSTR_SOURCE_PRINT_THREAD");
  runtime->filter_function_include = NULL;
  runtime->filter_function_exclude = NULL;
  runtime->filters_enabled = false;
  runtime->rate = 1;
  runtime->rate_enabled = false;

  asciichat_instr_runtime_configure(runtime);

  if (ascii_tls_set(g_runtime_key, runtime) != 0) {
    SAFE_FREE(runtime);
    return NULL;
  }

  return runtime;
}

void asciichat_instr_runtime_destroy(asciichat_instr_runtime_t *runtime) {
  if (runtime == NULL) {
    return;
  }

  if (runtime->fd >= 0) {
    platform_close(runtime->fd);
    runtime->fd = -1;
  }

#if ASCII_INSTR_SOURCE_PRINT_HAVE_REGEX
  if (runtime->include_regex_valid) {
    regfree(&runtime->include_regex);
    runtime->include_regex_valid = false;
  }
  if (runtime->exclude_regex_valid) {
    regfree(&runtime->exclude_regex);
    runtime->exclude_regex_valid = false;
  }
  if (runtime->function_include_regex_valid) {
    regfree(&runtime->function_include_regex);
    runtime->function_include_regex_valid = false;
  }
  if (runtime->function_exclude_regex_valid) {
    regfree(&runtime->function_exclude_regex);
    runtime->function_exclude_regex_valid = false;
  }
#endif

  asciichat_instr_only_list_destroy(&runtime->only_selectors);
  SAFE_FREE(runtime);
}

void asciichat_instr_runtime_global_destroy(void) {
  static_mutex_lock(&g_runtime_mutex);

  if (g_runtime_initialized) {
    g_disable_write = true;
    ascii_tls_key_delete(g_runtime_key);
    g_runtime_initialized = false;
    g_ticks_initialized = false;
    g_start_ns = 0;
    g_coverage_enabled = false;
    g_output_dir_set = false;
    g_output_dir[0] = '\0';
    g_instrumentation_enabled_checked = false;
    g_instrumentation_enabled = false;
    g_echo_to_stderr_initialized = false;
    g_echo_to_stderr = false;
  }

  // Reset g_disable_write so instrumentation can be re-enabled in subsequent tests
  g_disable_write = false;

  static_mutex_unlock(&g_runtime_mutex);
}

void asciichat_instr_log_line(const char *file_path, uint32_t line_number, const char *function_name,
                              const char *snippet, uint8_t is_macro_expansion) {
  // Instrumentation is enabled by default when the binary is built with Source Print.
  // Set ASCII_INSTR_SOURCE_PRINT_ENABLE=0 to disable tracing at runtime.
  if (!g_instrumentation_enabled_checked) {
    const char *enable_env = SAFE_GETENV("ASCII_INSTR_SOURCE_PRINT_ENABLE");
    // Default to enabled (true) - only disable if explicitly set to "0", "false", "off", or "no"
    if (enable_env != NULL && enable_env[0] != '\0') {
      g_instrumentation_enabled = asciichat_instr_env_is_enabled(enable_env);
    } else {
      g_instrumentation_enabled = true; // Default to enabled when instrumented
    }
    g_instrumentation_enabled_checked = true;
  }

  if (!g_instrumentation_enabled) {
    return;
  }

  if (g_disable_write) {
    return;
  }

  if (g_logging_reentry_guard) {
    return;
  }

  g_logging_reentry_guard = true;

  asciichat_instr_runtime_t *runtime = asciichat_instr_runtime_get();
  if (runtime == NULL) {
    goto cleanup;
  }

  if (!asciichat_instr_should_log(runtime, file_path, line_number, function_name)) {
    goto cleanup;
  }

  runtime->call_counter++;
  if (runtime->rate_enabled) {
    const uint64_t counter = runtime->call_counter;
    if (((counter - 1U) % runtime->rate) != 0U) {
      goto cleanup;
    }
  }

  if (runtime->fd < 0) {
    if (asciichat_instr_open_log_file(runtime) != 0) {
      runtime->stderr_fallback = true;
    }
  }

  const int fd = runtime->stderr_fallback ? STDERR_FILENO : runtime->fd;
  char buffer[ASCII_INSTR_SOURCE_PRINT_MAX_LINE];
  size_t pos = 0;

  uint64_t realtime_ns = time_get_realtime_ns();
  time_t sec = (time_t)(realtime_ns / NS_PER_SEC_INT);
  long nsec = (long)(realtime_ns % NS_PER_SEC_INT);
  struct tm tm_now;
  memset(&tm_now, 0, sizeof(tm_now));
  if (platform_gtime(&sec, &tm_now) != ASCIICHAT_OK) {
    memset(&tm_now, 0, sizeof(tm_now));
  }

  char timestamp[32];
  size_t ts_len = strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &tm_now);
  if (ts_len == 0) {
    SAFE_STRNCPY(timestamp, "1970-01-01T00:00:00", sizeof(timestamp));
    ts_len = strlen(timestamp);
  }

  char elapsed_buf[32];
  elapsed_buf[0] = '\0';
  if (g_ticks_initialized) {
    uint64_t now_ns = time_get_ns();
    uint64_t elapsed_ns = time_elapsed_ns(g_start_ns, now_ns);
    if (format_duration_ns((double)elapsed_ns, elapsed_buf, sizeof(elapsed_buf)) < 0) {
      elapsed_buf[0] = '\0';
    }
  }

  runtime->sequence++;

  const char *elapsed_field = (elapsed_buf[0] != '\0') ? elapsed_buf : "-";

  const char *safe_file_path = (file_path != NULL) ? file_path : "<unknown>";
  pos += safe_snprintf(buffer + pos, sizeof(buffer) - pos,
                       "pid=%d tid=%llu seq=%llu ts=%.*s.%09ldZ elapsed=%s file=%s line=%u func=%s macro=%u snippet=",
                       runtime->pid, (unsigned long long)runtime->thread_id, (unsigned long long)runtime->sequence,
                       (int)ts_len, timestamp, nsec, elapsed_field, safe_file_path, line_number,
                       function_name ? function_name : "<unknown>", (unsigned)is_macro_expansion);

  if (snippet != NULL) {
    size_t snippet_len = strnlen(snippet, ASCII_INSTR_SOURCE_PRINT_MAX_SNIPPET);
    for (size_t i = 0; i < snippet_len && pos < sizeof(buffer) - 2; ++i) {
      const char ch = snippet[i];
      switch (ch) {
      case '\n':
        buffer[pos++] = '\\';
        buffer[pos++] = 'n';
        break;
      case '\r':
        buffer[pos++] = '\\';
        buffer[pos++] = 'r';
        break;
      case '\t':
        buffer[pos++] = '\\';
        buffer[pos++] = 't';
        break;
      default:
        buffer[pos++] = ch;
        break;
      }
    }
  }

  if (pos >= sizeof(buffer) - 1) {
    pos = sizeof(buffer) - 2;
  }

  buffer[pos++] = '\n';
  buffer[pos] = '\0';

  asciichat_instr_write_full(fd, buffer, pos);

  // Also echo to stderr if requested via environment variable
  if (!g_echo_to_stderr_initialized) {
    const char *echo_env = SAFE_GETENV("ASCII_CHAT_DEBUG_SELF_SOURCE_CODE_LOG_STDERR");
    g_echo_to_stderr = asciichat_instr_env_is_enabled(echo_env);
    g_echo_to_stderr_initialized = true;
  }

  if (g_echo_to_stderr && !runtime->stderr_fallback) {
    // Write to stderr in addition to the log file
    // Suppress Windows deprecation warning for POSIX write function
#ifdef _WIN32
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    (void)posix_write(STDERR_FILENO, buffer, pos);
#ifdef _WIN32
#pragma clang diagnostic pop
#endif
  }

cleanup:
  g_logging_reentry_guard = false;
}

bool asciichat_instr_coverage_enabled(void) {
  if (g_disable_write) {
    return false;
  }

  // Initialize runtime once using mutex-protected initialization
  if (!g_runtime_initialized) {
    static_mutex_lock(&g_runtime_mutex);
    if (!g_runtime_initialized) {
      asciichat_instr_runtime_init_once();
    }
    static_mutex_unlock(&g_runtime_mutex);
  }

  return g_coverage_enabled;
}

void asciichat_instr_log_pc(uintptr_t program_counter) {
  if (!asciichat_instr_coverage_enabled()) {
    return;
  }

  char snippet[64];
  safe_snprintf(snippet, sizeof(snippet), "pc=0x%zx", (size_t)program_counter);
  asciichat_instr_log_line("__coverage__", 0, "<coverage>", snippet, ASCII_INSTR_SOURCE_PRINT_MACRO_NONE);
}

static void asciichat_instr_runtime_init_once(void) {
  // NOTE: This function is always called with g_runtime_mutex held by the caller
  // so we don't need to lock/unlock here
  if (!g_runtime_initialized) {
    (void)ascii_tls_key_create(&g_runtime_key, asciichat_instr_runtime_tls_destructor);
    const char *output_dir_env = SAFE_GETENV("ASCII_INSTR_SOURCE_PRINT_OUTPUT_DIR");
    if (output_dir_env != NULL && output_dir_env[0] != '\0') {
      char *normalized_output_dir = NULL;
      asciichat_error_t validation_result =
          path_validate_user_path(output_dir_env, PATH_ROLE_LOG_FILE, &normalized_output_dir);
      if (validation_result == ASCIICHAT_OK && normalized_output_dir != NULL) {
        SAFE_STRNCPY(g_output_dir, normalized_output_dir, sizeof(g_output_dir));
        g_output_dir[sizeof(g_output_dir) - 1] = '\0';
        g_output_dir_set = true;
      } else {
        log_warn("Ignoring invalid ASCII_INSTR_SOURCE_PRINT_OUTPUT_DIR path: %s", output_dir_env);
      }
      SAFE_FREE(normalized_output_dir);
    }
    const char *coverage_env = SAFE_GETENV("ASCII_INSTR_SOURCE_PRINT_ENABLE_COVERAGE");
    g_coverage_enabled = asciichat_instr_env_is_enabled(coverage_env);
    g_start_ns = time_get_ns();
    g_ticks_initialized = true;
    g_runtime_initialized = true;
  }
}

static void asciichat_instr_runtime_tls_destructor(void *ptr) {
  asciichat_instr_runtime_destroy((asciichat_instr_runtime_t *)ptr);
}

static bool asciichat_instr_build_log_path(asciichat_instr_runtime_t *runtime) {
  // Check for custom log file path first
  const char *custom_log_file = SAFE_GETENV("ASCII_CHAT_DEBUG_SELF_SOURCE_CODE_LOG_FILE");
  bool is_custom_path = (custom_log_file != NULL && custom_log_file[0] != '\0');

  if (is_custom_path) {
    const char *debug_env = SAFE_GETENV("ASCII_INSTR_SOURCE_PRINT_ECHO_STDERR");
    if (debug_env && debug_env[0] == '1') {
      fprintf(stderr, "ASCII_INSTR: Using custom log path: %s\n", custom_log_file);
    }

    // For instrumentation log files, bypass strict path validation since this is a debug feature
    // Just normalize the path to an absolute path without security checks
    char *expanded = expand_path(custom_log_file);
    if (!expanded) {
      log_warn("Failed to expand ASCII_CHAT_DEBUG_SELF_SOURCE_CODE_LOG_FILE path: %s", custom_log_file);
      return false;
    }

    // Convert to absolute path if relative
    char absolute_buf[PATH_MAX];
    if (!path_is_absolute(expanded)) {
      char cwd_buf[PATH_MAX];
      if (!platform_get_cwd(cwd_buf, sizeof(cwd_buf))) {
        SAFE_FREE(expanded);
        return false;
      }
      // Check if expanded already starts with a separator to avoid double separators
      if (strlen(expanded) > 0 && expanded[0] == PATH_DELIM) {
        safe_snprintf(absolute_buf, sizeof(absolute_buf), "%s%s", cwd_buf, expanded);
      } else {
        safe_snprintf(absolute_buf, sizeof(absolute_buf), "%s%c%s", cwd_buf, PATH_DELIM, expanded);
      }
      SAFE_FREE(expanded);
      expanded = platform_strdup(absolute_buf);
      if (!expanded) {
        return false;
      }
    }

    SAFE_STRNCPY(runtime->log_path, expanded, sizeof(runtime->log_path));
    runtime->log_path[sizeof(runtime->log_path) - 1] = '\0';
    SAFE_FREE(expanded);

    if (debug_env && debug_env[0] == '1') {
      fprintf(stderr, "ASCII_INSTR: Resolved custom log path: %s\n", runtime->log_path);
    }

    // Don't check if file exists - allow appending to existing file
    // Path already normalized, skip validation below
  } else {
    // Determine output directory
    char output_dir_buf[PATH_MAX];
    if (g_output_dir_set) {
      // Use the output directory from ASCII_INSTR_SOURCE_PRINT_OUTPUT_DIR
      SAFE_STRNCPY(output_dir_buf, g_output_dir, sizeof(output_dir_buf));
    } else if (!platform_get_cwd(output_dir_buf, sizeof(output_dir_buf))) {
      // Fallback to temp directory if cwd fails
      const char *fallback = SAFE_GETENV("TMPDIR");
      if (fallback == NULL) {
        fallback = SAFE_GETENV("TEMP");
      }
      if (fallback == NULL) {
        fallback = SAFE_GETENV("TMP");
      }
      if (fallback == NULL) {
        fallback = "/tmp";
      }
      SAFE_STRNCPY(output_dir_buf, fallback, sizeof(output_dir_buf));
    }

    // Build log file name
    if (g_output_dir_set) {
      // When output directory is explicitly set, use unique naming with pid and tid
      if (safe_snprintf(runtime->log_path, sizeof(runtime->log_path), "%s%c%s-%d-%llu.log", output_dir_buf, PATH_DELIM,
                        ASCII_INSTR_SOURCE_PRINT_DEFAULT_BASENAME, runtime->pid,
                        (unsigned long long)runtime->thread_id) >= (int)sizeof(runtime->log_path)) {
        return false;
      }
    } else {
      // Use simple "trace.log" name in current directory
      if (safe_snprintf(runtime->log_path, sizeof(runtime->log_path), "%s%ctrace.log", output_dir_buf, PATH_DELIM) >=
          (int)sizeof(runtime->log_path)) {
        return false;
      }
    }
  }

  // Only validate auto-generated paths (custom paths already validated above)
  if (!is_custom_path) {
    char *validated_log_path = NULL;
    asciichat_error_t validate_result =
        path_validate_user_path(runtime->log_path, PATH_ROLE_LOG_FILE, &validated_log_path);
    if (validate_result != ASCIICHAT_OK || validated_log_path == NULL) {
      SAFE_FREE(validated_log_path);
      log_warn("Failed to validate instrumentation log path: %s", runtime->log_path);
      return false;
    }
    SAFE_STRNCPY(runtime->log_path, validated_log_path, sizeof(runtime->log_path));
    runtime->log_path[sizeof(runtime->log_path) - 1] = '\0';
    SAFE_FREE(validated_log_path);
  }

  // Find last path separator
  const char *last_sep = strrchr(runtime->log_path, PATH_DELIM);
  if (last_sep != NULL && last_sep != runtime->log_path) {
    const size_t dir_path_len = (size_t)(last_sep - runtime->log_path);
    char dir_path[PATH_MAX];
    memcpy(dir_path, runtime->log_path, dir_path_len);
    dir_path[dir_path_len] = '\0';
    if (mkdir(dir_path, DIR_PERM_PRIVATE) != 0) {
      if (errno != EEXIST) {
        return false;
      }
    }
  }

  return true;
}

static int asciichat_instr_open_log_file(asciichat_instr_runtime_t *runtime) {
  if (!asciichat_instr_build_log_path(runtime)) {
    const char *debug_env = SAFE_GETENV("ASCII_INSTR_SOURCE_PRINT_ECHO_STDERR");
    if (debug_env && debug_env[0] == '1') {
      fprintf(stderr, "ASCII_INSTR: Failed to build log path\n");
    }
    return -1;
  }

  // Check if this is a custom log file (env var was set)
  const char *custom_log_file = SAFE_GETENV("ASCII_CHAT_DEBUG_SELF_SOURCE_CODE_LOG_FILE");
  const bool is_custom_file = (custom_log_file != NULL && custom_log_file[0] != '\0');

  // For custom files, allow appending to existing file (no O_EXCL)
  // For auto-generated files, use O_EXCL to avoid conflicts
  int flags;
  if (is_custom_file) {
    flags = PLATFORM_O_WRONLY | PLATFORM_O_CREAT | PLATFORM_O_APPEND | PLATFORM_O_BINARY;
  } else {
    flags = PLATFORM_O_WRONLY | PLATFORM_O_CREAT | PLATFORM_O_EXCL | PLATFORM_O_APPEND | PLATFORM_O_BINARY;
  }

  const char *debug_env = SAFE_GETENV("ASCII_INSTR_SOURCE_PRINT_ECHO_STDERR");
  if (debug_env && debug_env[0] == '1') {
    fprintf(stderr, "ASCII_INSTR: Opening log file: %s (custom=%d)\n", runtime->log_path, is_custom_file);
  }

  const mode_t mode = S_IRUSR | S_IWUSR;
  int fd = platform_open(runtime->log_path, flags, mode);
  if (fd < 0) {
    if (debug_env && debug_env[0] == '1') {
      fprintf(stderr, "ASCII_INSTR: Failed to open log file: %s (errno=%d)\n", runtime->log_path, errno);
    }
    return -1;
  }

  if (debug_env && debug_env[0] == '1') {
    fprintf(stderr, "ASCII_INSTR: Successfully opened log file: %s (fd=%d)\n", runtime->log_path, fd);
  }

  runtime->fd = fd;
  return 0;
}

static void asciichat_instr_runtime_configure(asciichat_instr_runtime_t *runtime) {
  runtime->filter_function_include = SAFE_GETENV("ASCII_INSTR_SOURCE_PRINT_FUNCTION_INCLUDE");
  runtime->filter_function_exclude = SAFE_GETENV("ASCII_INSTR_SOURCE_PRINT_FUNCTION_EXCLUDE");

  const char *only_env = SAFE_GETENV("ASCII_INSTR_SOURCE_PRINT_ONLY");
  asciichat_instr_parse_only_filters(runtime, only_env);

#if ASCII_INSTR_SOURCE_PRINT_HAVE_REGEX
  const char *include_regex = SAFE_GETENV("ASCII_INSTR_SOURCE_PRINT_INCLUDE_REGEX");
  if (include_regex != NULL && include_regex[0] != '\0') {
    runtime->include_regex_valid = asciichat_instr_compile_regex(&runtime->include_regex, include_regex);
  }

  const char *exclude_regex = SAFE_GETENV("ASCII_INSTR_SOURCE_PRINT_EXCLUDE_REGEX");
  if (exclude_regex != NULL && exclude_regex[0] != '\0') {
    runtime->exclude_regex_valid = asciichat_instr_compile_regex(&runtime->exclude_regex, exclude_regex);
  }

  const char *function_include_regex = SAFE_GETENV("ASCII_INSTR_SOURCE_PRINT_FUNCTION_INCLUDE_REGEX");
  if (function_include_regex != NULL && function_include_regex[0] != '\0') {
    runtime->function_include_regex_valid =
        asciichat_instr_compile_regex(&runtime->function_include_regex, function_include_regex);
  }

  const char *function_exclude_regex = SAFE_GETENV("ASCII_INSTR_SOURCE_PRINT_FUNCTION_EXCLUDE_REGEX");
  if (function_exclude_regex != NULL && function_exclude_regex[0] != '\0') {
    runtime->function_exclude_regex_valid =
        asciichat_instr_compile_regex(&runtime->function_exclude_regex, function_exclude_regex);
  }
#endif

  const char *rate_env = SAFE_GETENV("ASCII_INSTR_SOURCE_PRINT_RATE");
  uint32_t rate_value = 0;
  if (asciichat_instr_parse_positive_uint32(rate_env, &rate_value) && rate_value > 1U) {
    runtime->rate = rate_value;
    runtime->rate_enabled = true;
  }

  runtime->filters_enabled = (runtime->filter_include != NULL) || (runtime->filter_exclude != NULL) ||
                             (runtime->filter_thread != NULL) || (runtime->filter_function_include != NULL) ||
                             (runtime->filter_function_exclude != NULL)
#if ASCII_INSTR_SOURCE_PRINT_HAVE_REGEX
                             || runtime->include_regex_valid || runtime->exclude_regex_valid ||
                             runtime->function_include_regex_valid || runtime->function_exclude_regex_valid
#endif
                             || runtime->only_selectors.count > 0;
}

static bool asciichat_instr_env_is_enabled(const char *value) {
  if (value == NULL) {
    return false;
  }

  while (*value != '\0' && isspace((unsigned char)*value) != 0) {
    value++;
  }

  if (*value == '\0') {
    return false;
  }

  if (strcmp(value, STR_ZERO) == 0) {
    return false;
  }

  char lowered[8];
  size_t len = 0;
  while (value[len] != '\0' && len < sizeof(lowered) - 1) {
    lowered[len] = (char)tolower((unsigned char)value[len]);
    len++;
  }
  lowered[len] = '\0';

  return !(strcmp(lowered, STR_FALSE) == 0 || strcmp(lowered, STR_OFF) == 0 || strcmp(lowered, STR_NO) == 0);
}

static bool asciichat_instr_parse_positive_uint32(const char *value, uint32_t *out_value) {
  if (value == NULL || out_value == NULL) {
    return false;
  }

  while (*value != '\0' && isspace((unsigned char)*value) != 0) {
    value++;
  }

  if (*value == '\0') {
    return false;
  }

  errno = 0;
  char *endptr = NULL;
  unsigned long long parsed = strtoull(value, &endptr, 10);
  if (errno != 0 || endptr == value || parsed == 0ULL || parsed > UINT32_MAX) {
    return false;
  }

  *out_value = (uint32_t)parsed;
  return true;
}

#if ASCII_INSTR_SOURCE_PRINT_HAVE_REGEX
static bool asciichat_instr_compile_regex(regex_t *regex, const char *pattern) {
  if (regex == NULL || pattern == NULL) {
    return false;
  }
  int ret = regcomp(regex, pattern, REG_EXTENDED | REG_NOSUB);
  return ret == 0;
}
#endif

static void asciichat_instr_only_list_destroy(asciichat_instr_only_list_t *list) {
  if (list == NULL) {
    return;
  }
  if (list->items != NULL) {
    for (size_t i = 0; i < list->count; ++i) {
      asciichat_instr_only_selector_t *selector = &list->items[i];
      SAFE_FREE(selector->pattern);
      SAFE_FREE(selector->module);
    }
    SAFE_FREE(list->items);
  }
  list->count = 0;
  list->capacity = 0;
}

static bool asciichat_instr_only_list_append(asciichat_instr_only_list_t *list, asciichat_instr_selector_type_t type,
                                             const char *module, const char *pattern) {
  if (list == NULL) {
    return false;
  }

  if (type == ASCII_INSTR_SOURCE_PRINT_SELECTOR_FILE_SUBSTRING || type == ASCII_INSTR_SOURCE_PRINT_SELECTOR_FILE_GLOB ||
      type == ASCII_INSTR_SOURCE_PRINT_SELECTOR_FUNCTION_GLOB) {
    if (pattern == NULL || pattern[0] == '\0') {
      return false;
    }
  }

  if (type == ASCII_INSTR_SOURCE_PRINT_SELECTOR_MODULE) {
    if (module == NULL || module[0] == '\0') {
      return false;
    }
  }

  if (list->count == list->capacity) {
    size_t new_capacity = (list->capacity == 0) ? 4U : list->capacity * 2U;
    asciichat_instr_only_selector_t *new_items =
        SAFE_REALLOC(list->items, new_capacity * sizeof(*new_items), asciichat_instr_only_selector_t *);
    if (new_items == NULL) {
      return false; // SAFE_REALLOC already called FATAL, but satisfy analyzer
    }
    list->items = new_items;
    list->capacity = new_capacity;
  }

  asciichat_instr_only_selector_t *selector = &list->items[list->count];
  memset(selector, 0, sizeof(*selector));
  selector->type = type;

  if (module != NULL && module[0] != '\0') {
    SAFE_STRDUP(selector->module, module);
  }
  if (pattern != NULL && pattern[0] != '\0') {
    SAFE_STRDUP(selector->pattern, pattern);
  }

  list->count++;
  return true;
}

static void asciichat_instr_trim(char *value) {
  if (value == NULL) {
    return;
  }

  char *start = value;
  while (*start != '\0' && isspace((unsigned char)*start) != 0) {
    start++;
  }

  char *end = start + strlen(start);
  while (end > start && isspace((unsigned char)end[-1]) != 0) {
    end--;
  }

  const size_t length = (size_t)(end - start);
  if (start != value && length > 0U) {
    memmove(value, start, length);
  }
  value[length] = '\0';
}

static bool asciichat_instr_parse_only_filters(asciichat_instr_runtime_t *runtime, const char *value) {
  if (runtime == NULL) {
    return false;
  }

  asciichat_instr_only_list_destroy(&runtime->only_selectors);

  if (value == NULL || value[0] == '\0') {
    return true;
  }

  char *mutable_value = NULL;
  SAFE_STRDUP(mutable_value, value);
  if (mutable_value == NULL) {
    return false;
  }

  char *cursor = mutable_value;
  while (cursor != NULL && *cursor != '\0') {
    char *token_start = cursor;
    while (*cursor != '\0' && *cursor != ',') {
      cursor++;
    }
    if (*cursor == ',') {
      *cursor = '\0';
      cursor++;
    } else {
      cursor = NULL;
    }

    asciichat_instr_trim(token_start);
    if (*token_start == '\0') {
      continue;
    }

    char *equal_sign = strchr(token_start, '=');
    if (equal_sign != NULL) {
      *equal_sign = '\0';
      char *kind = token_start;
      char *spec = equal_sign + 1;
      asciichat_instr_trim(kind);
      asciichat_instr_trim(spec);
      if (*spec == '\0') {
        continue;
      }

      if (strcmp(kind, "func") == 0 || strcmp(kind, "function") == 0) {
        (void)asciichat_instr_only_list_append(&runtime->only_selectors,
                                               ASCII_INSTR_SOURCE_PRINT_SELECTOR_FUNCTION_GLOB, NULL, spec);
      } else if (strcmp(kind, "module") == 0) {
        char *module_value = spec;
        char *module_pattern = strchr(module_value, ':');
        if (module_pattern != NULL) {
          *module_pattern = '\0';
          module_pattern++;
          asciichat_instr_trim(module_pattern);
        }
        asciichat_instr_trim(module_value);
        if (*module_value == '\0') {
          continue;
        }
        const char *pattern_part = (module_pattern != NULL && *module_pattern != '\0') ? module_pattern : NULL;
        (void)asciichat_instr_only_list_append(&runtime->only_selectors, ASCII_INSTR_SOURCE_PRINT_SELECTOR_MODULE,
                                               module_value, pattern_part);
      } else {
        // "file" kind or unknown kinds default to FILE_GLOB
        (void)asciichat_instr_only_list_append(&runtime->only_selectors, ASCII_INSTR_SOURCE_PRINT_SELECTOR_FILE_GLOB,
                                               NULL, spec);
      }
      continue;
    }

    char *colon = strchr(token_start, ':');
    if (colon != NULL) {
      *colon = '\0';
      char *module_name = token_start;
      char *pattern_part = colon + 1;
      asciichat_instr_trim(module_name);
      asciichat_instr_trim(pattern_part);
      if (*module_name == '\0') {
        continue;
      }
      const char *pattern_spec = (*pattern_part != '\0') ? pattern_part : NULL;
      (void)asciichat_instr_only_list_append(&runtime->only_selectors, ASCII_INSTR_SOURCE_PRINT_SELECTOR_MODULE,
                                             module_name, pattern_spec);
      continue;
    }

    (void)asciichat_instr_only_list_append(&runtime->only_selectors, ASCII_INSTR_SOURCE_PRINT_SELECTOR_FILE_SUBSTRING,
                                           NULL, token_start);
  }

  SAFE_FREE(mutable_value);
  return true;
}

static bool asciichat_instr_match_glob(const char *pattern, const char *value) {
  if (pattern == NULL || value == NULL) {
    return false;
  }

  const char *p = pattern;
  const char *v = value;
  const char *star = NULL;
  const char *match = NULL;

  while (*v != '\0') {
    if (*p == '*') {
      star = p++;
      match = v;
    } else if (*p == '?' || *p == *v) {
      p++;
      v++;
    } else if (star != NULL) {
      p = star + 1;
      match++;
      v = match;
    } else {
      return false;
    }
  }

  while (*p == '*') {
    p++;
  }

  return *p == '\0';
}

static const char *asciichat_instr_basename(const char *path) {
  if (path == NULL) {
    return NULL;
  }

  // Find last path separator
  const char *last_sep = strrchr(path, PATH_DELIM);
  if (last_sep != NULL && last_sep[1] != '\0') {
    return last_sep + 1;
  }
  return path;
}

static bool asciichat_instr_path_contains_module(const char *file_path, const char *module_name) {
  if (file_path == NULL || module_name == NULL || module_name[0] == '\0') {
    return false;
  }

  const size_t module_len = strlen(module_name);
  const char *cursor = file_path;
  while ((cursor = strstr(cursor, module_name)) != NULL) {
    bool left_ok = (cursor == file_path);
    if (!left_ok) {
      const char prev = cursor[-1];
      left_ok = (prev == PATH_DELIM);
    }

    const char tail = cursor[module_len];
    bool right_ok = (tail == '\0' || tail == PATH_DELIM);

    if (left_ok && right_ok) {
      return true;
    }

    cursor = cursor + 1;
  }

  return false;
}

static bool asciichat_instr_only_selectors_match(const asciichat_instr_runtime_t *runtime, const char *file_path,
                                                 const char *function_name) {
  const asciichat_instr_only_list_t *list = &runtime->only_selectors;
  if (list->count == 0) {
    return true;
  }

  const char *base_name = asciichat_instr_basename(file_path);
  for (size_t i = 0; i < list->count; ++i) {
    const asciichat_instr_only_selector_t *selector = &list->items[i];
    switch (selector->type) {
    case ASCII_INSTR_SOURCE_PRINT_SELECTOR_FILE_SUBSTRING:
      if (file_path != NULL && selector->pattern != NULL && strstr(file_path, selector->pattern) != NULL) {
        return true;
      }
      break;
    case ASCII_INSTR_SOURCE_PRINT_SELECTOR_FILE_GLOB:
      if (file_path != NULL && selector->pattern != NULL && asciichat_instr_match_glob(selector->pattern, file_path)) {
        return true;
      }
      break;
    case ASCII_INSTR_SOURCE_PRINT_SELECTOR_FUNCTION_GLOB:
      if (function_name != NULL && selector->pattern != NULL &&
          asciichat_instr_match_glob(selector->pattern, function_name)) {
        return true;
      }
      break;
    case ASCII_INSTR_SOURCE_PRINT_SELECTOR_MODULE:
      if (file_path != NULL && selector->module != NULL &&
          asciichat_instr_path_contains_module(file_path, selector->module)) {
        if (selector->pattern == NULL ||
            (base_name != NULL && asciichat_instr_match_glob(selector->pattern, base_name))) {
          return true;
        }
      }
      break;
    default:
      break;
    }
  }

  return false;
}

static bool asciichat_instr_should_log(const asciichat_instr_runtime_t *runtime, const char *file_path,
                                       uint32_t line_number, const char *function_name) {
  if (!runtime->filters_enabled) {
    return true;
  }

  if (runtime->filter_thread != NULL) {
    char tid_buf[32];
    safe_snprintf(tid_buf, sizeof(tid_buf), "%llu", (unsigned long long)runtime->thread_id);
    if (strstr(runtime->filter_thread, tid_buf) == NULL) {
      return false;
    }
  }

  if (runtime->filter_include != NULL) {
    if (file_path == NULL || strstr(file_path, runtime->filter_include) == NULL) {
      return false;
    }
  }

  if (runtime->filter_exclude != NULL && file_path != NULL) {
    if (strstr(file_path, runtime->filter_exclude) != NULL) {
      return false;
    }
  }

#if ASCII_INSTR_SOURCE_PRINT_HAVE_REGEX
  if (runtime->include_regex_valid) {
    if (file_path == NULL || regexec(&runtime->include_regex, file_path, 0, NULL, 0) != 0) {
      return false;
    }
  }

  if (runtime->exclude_regex_valid && file_path != NULL) {
    if (regexec(&runtime->exclude_regex, file_path, 0, NULL, 0) == 0) {
      return false;
    }
  }
#endif

  if (runtime->filter_function_include != NULL) {
    if (function_name == NULL || strstr(function_name, runtime->filter_function_include) == NULL) {
      return false;
    }
  }

  if (runtime->filter_function_exclude != NULL && function_name != NULL) {
    if (strstr(function_name, runtime->filter_function_exclude) != NULL) {
      return false;
    }
  }

#if ASCII_INSTR_SOURCE_PRINT_HAVE_REGEX
  if (runtime->function_include_regex_valid) {
    if (function_name == NULL || regexec(&runtime->function_include_regex, function_name, 0, NULL, 0) != 0) {
      return false;
    }
  }

  if (runtime->function_exclude_regex_valid && function_name != NULL) {
    if (regexec(&runtime->function_exclude_regex, function_name, 0, NULL, 0) == 0) {
      return false;
    }
  }
#endif

  if (runtime->only_selectors.count > 0) {
    if (!asciichat_instr_only_selectors_match(runtime, file_path, function_name)) {
      return false;
    }
  }

  (void)line_number;
  return true;
}

static int asciichat_instr_write_full(int fd, const char *buffer, size_t len) {
  size_t total = 0;
  while (total < len) {
    ssize_t written = platform_write(fd, buffer + total, len - total);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return -1;
    }
    total += (size_t)written;
  }
  return 0;
}
