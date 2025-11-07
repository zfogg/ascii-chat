// SPDX-License-Identifier: MIT
// Debug instrumentation logging runtime for ascii-chat line tracing

#include "debug/instrument_log.h"

#include "common.h"
#include "platform/internal.h"
#include "platform/system.h"
#include "platform/thread.h"
#include "util/time.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#if !defined(_WIN32)
#define ASCII_INSTR_HAVE_REGEX 1
#include <regex.h>
#else
#define ASCII_INSTR_HAVE_REGEX 0
#endif

#ifdef _WIN32
#include <direct.h>
#ifndef mkdir
#define mkdir(path, mode) _mkdir(path)
#endif
#endif

#ifndef ASCII_INSTR_DEFAULT_BASENAME
#define ASCII_INSTR_DEFAULT_BASENAME "ascii-instr"
#endif

#ifndef ASCII_INSTR_MAX_LINE
#define ASCII_INSTR_MAX_LINE 4096
#endif

#ifndef ASCII_INSTR_MAX_SNIPPET
#define ASCII_INSTR_MAX_SNIPPET 2048
#endif

typedef enum ascii_instr_selector_type {
  ASCII_INSTR_SELECTOR_FILE_SUBSTRING = 0,
  ASCII_INSTR_SELECTOR_FILE_GLOB = 1,
  ASCII_INSTR_SELECTOR_FUNCTION_GLOB = 2,
  ASCII_INSTR_SELECTOR_MODULE = 3,
} ascii_instr_selector_type_t;

typedef struct ascii_instr_only_selector {
  ascii_instr_selector_type_t type;
  char *pattern;
  char *module;
} ascii_instr_only_selector_t;

typedef struct ascii_instr_only_list {
  ascii_instr_only_selector_t *items;
  size_t count;
  size_t capacity;
} ascii_instr_only_list_t;

typedef struct ascii_instr_runtime {
  int fd;
  int pid;
  uint64_t thread_id;
  uint64_t sequence;
  uint64_t call_counter;
  char log_path[PATH_MAX];
  bool filters_enabled;
  const char *filter_include;
  const char *filter_exclude;
  const char *filter_function_include;
  const char *filter_function_exclude;
  const char *filter_thread;
#if ASCII_INSTR_HAVE_REGEX
  regex_t include_regex;
  bool include_regex_valid;
  regex_t exclude_regex;
  bool exclude_regex_valid;
  regex_t function_include_regex;
  bool function_include_regex_valid;
  regex_t function_exclude_regex;
  bool function_exclude_regex_valid;
#endif
  uint32_t rate;
  bool rate_enabled;
  bool stderr_fallback;
  ascii_instr_only_list_t only_selectors;
} ascii_instr_runtime_t;

static pthread_key_t g_runtime_key;
static pthread_once_t g_runtime_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_runtime_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool g_runtime_initialized = false;
static char g_output_dir[PATH_MAX];
static bool g_output_dir_set = false;
static bool g_disable_write = false;
static uint64_t g_start_ticks = 0;
static bool g_ticks_initialized = false;
static bool g_coverage_enabled = false;

static void ascii_instr_runtime_init_once(void);
static void ascii_instr_runtime_tls_destructor(void *ptr);
static int ascii_instr_open_log_file(ascii_instr_runtime_t *runtime);
static void ascii_instr_runtime_configure(ascii_instr_runtime_t *runtime);
static bool ascii_instr_should_log(const ascii_instr_runtime_t *runtime, const char *file_path, uint32_t line_number,
                                   const char *function_name);
static int ascii_instr_write_full(int fd, const char *buffer, size_t len);
static bool ascii_instr_env_is_enabled(const char *value);
static bool ascii_instr_parse_positive_uint32(const char *value, uint32_t *out_value);
#if ASCII_INSTR_HAVE_REGEX
static bool ascii_instr_compile_regex(regex_t *regex, const char *pattern);
#endif
static void ascii_instr_only_list_destroy(ascii_instr_only_list_t *list);
static bool ascii_instr_only_list_append(ascii_instr_only_list_t *list, ascii_instr_selector_type_t type,
                                         const char *module, const char *pattern);
static void ascii_instr_trim(char *value);
static bool ascii_instr_parse_only_filters(ascii_instr_runtime_t *runtime, const char *value);
static bool ascii_instr_only_selectors_match(const ascii_instr_runtime_t *runtime, const char *file_path,
                                             const char *function_name);
static bool ascii_instr_match_glob(const char *pattern, const char *value);
static const char *ascii_instr_basename(const char *path);
static bool ascii_instr_path_contains_module(const char *file_path, const char *module_name);

ascii_instr_runtime_t *ascii_instr_runtime_get(void) {
  if (g_disable_write) {
    return NULL;
  }

  pthread_once(&g_runtime_once, ascii_instr_runtime_init_once);

  ascii_instr_runtime_t *runtime = pthread_getspecific(g_runtime_key);
  if (runtime != NULL) {
    return runtime;
  }

  runtime = SAFE_CALLOC(1, sizeof(*runtime), ascii_instr_runtime_t *);
  if (runtime == NULL) {
    return NULL;
  }

  runtime->pid = platform_get_pid();
  runtime->thread_id = ascii_thread_current_id();
  runtime->sequence = 0;
  runtime->call_counter = 0;
  runtime->fd = -1;
  runtime->filter_include = SAFE_GETENV("ASCII_INSTR_INCLUDE");
  runtime->filter_exclude = SAFE_GETENV("ASCII_INSTR_EXCLUDE");
  runtime->filter_thread = SAFE_GETENV("ASCII_INSTR_THREAD");
  runtime->filter_function_include = NULL;
  runtime->filter_function_exclude = NULL;
  runtime->filters_enabled = false;
  runtime->rate = 1;
  runtime->rate_enabled = false;

  ascii_instr_runtime_configure(runtime);

  if (pthread_setspecific(g_runtime_key, runtime) != 0) {
    SAFE_FREE(runtime);
    return NULL;
  }

  return runtime;
}

void ascii_instr_runtime_destroy(ascii_instr_runtime_t *runtime) {
  if (runtime == NULL) {
    return;
  }

  if (runtime->fd >= 0) {
    platform_close(runtime->fd);
    runtime->fd = -1;
  }

#if ASCII_INSTR_HAVE_REGEX
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

  ascii_instr_only_list_destroy(&runtime->only_selectors);
  SAFE_FREE(runtime);
}

void ascii_instr_runtime_global_shutdown(void) {
  pthread_mutex_lock(&g_runtime_mutex);
  if (g_runtime_initialized) {
    g_disable_write = true;
    pthread_key_delete(g_runtime_key);
    g_runtime_initialized = false;
    g_ticks_initialized = false;
    g_start_ticks = 0;
    g_coverage_enabled = false;
  }
  pthread_mutex_unlock(&g_runtime_mutex);
}

void ascii_instr_log_line(const char *file_path, uint32_t line_number, const char *function_name, const char *snippet,
                          uint8_t is_macro_expansion) {
  if (g_disable_write) {
    return;
  }

  ascii_instr_runtime_t *runtime = ascii_instr_runtime_get();
  if (runtime == NULL) {
    return;
  }

  if (!ascii_instr_should_log(runtime, file_path, line_number, function_name)) {
    return;
  }

  runtime->call_counter++;
  if (runtime->rate_enabled) {
    const uint64_t counter = runtime->call_counter;
    if (((counter - 1U) % runtime->rate) != 0U) {
      return;
    }
  }

  if (runtime->fd < 0) {
    if (ascii_instr_open_log_file(runtime) != 0) {
      runtime->stderr_fallback = true;
    }
  }

  const int fd = runtime->stderr_fallback ? STDERR_FILENO : runtime->fd;
  char buffer[ASCII_INSTR_MAX_LINE];
  size_t pos = 0;

  struct timespec ts;
  memset(&ts, 0, sizeof(ts));
#if defined(CLOCK_REALTIME)
  (void)clock_gettime(CLOCK_REALTIME, &ts);
#endif
  time_t sec = ts.tv_sec;
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
    uint64_t now_ticks = stm_now();
    double elapsed_ns = stm_ns(stm_diff(now_ticks, g_start_ticks));
    if (format_duration_ns(elapsed_ns, elapsed_buf, sizeof(elapsed_buf)) < 0) {
      elapsed_buf[0] = '\0';
    }
  }

  runtime->sequence++;

  const char *elapsed_field = (elapsed_buf[0] != '\0') ? elapsed_buf : "-";

  const char *safe_file_path = (file_path != NULL) ? file_path : "<unknown>";
  pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                  "pid=%d tid=%llu seq=%llu ts=%.*s.%09ldZ elapsed=%s file=%s line=%u func=%s macro=%u snippet=",
                  runtime->pid, (unsigned long long)runtime->thread_id, (unsigned long long)runtime->sequence,
                  (int)ts_len, timestamp, (long)ts.tv_nsec, elapsed_field, safe_file_path, line_number,
                  function_name ? function_name : "<unknown>", (unsigned)is_macro_expansion);

  if (snippet != NULL) {
    size_t snippet_len = strnlen(snippet, ASCII_INSTR_MAX_SNIPPET);
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

  ascii_instr_write_full(fd, buffer, pos);
}

bool ascii_instr_coverage_enabled(void) {
  if (g_disable_write) {
    return false;
  }

  pthread_once(&g_runtime_once, ascii_instr_runtime_init_once);
  return g_coverage_enabled;
}

void ascii_instr_log_pc(uintptr_t program_counter) {
  if (!ascii_instr_coverage_enabled()) {
    return;
  }

  char snippet[64];
  snprintf(snippet, sizeof(snippet), "pc=0x%zx", (size_t)program_counter);
  ascii_instr_log_line("__coverage__", 0, "<coverage>", snippet, ASCII_INSTR_MACRO_NONE);
}

static void ascii_instr_runtime_init_once(void) {
  pthread_mutex_lock(&g_runtime_mutex);
  if (!g_runtime_initialized) {
    (void)pthread_key_create(&g_runtime_key, ascii_instr_runtime_tls_destructor);
    const char *output_dir_env = SAFE_GETENV("ASCII_INSTR_OUTPUT_DIR");
    if (output_dir_env != NULL) {
      const size_t len = strnlen(output_dir_env, sizeof(g_output_dir) - 1);
      memcpy(g_output_dir, output_dir_env, len);
      g_output_dir[len] = '\0';
      g_output_dir_set = true;
    }
    const char *coverage_env = SAFE_GETENV("ASCII_INSTR_ENABLE_COVERAGE");
    g_coverage_enabled = ascii_instr_env_is_enabled(coverage_env);
    stm_setup();
    g_start_ticks = stm_now();
    g_ticks_initialized = true;
    g_runtime_initialized = true;
  }
  pthread_mutex_unlock(&g_runtime_mutex);
}

static void ascii_instr_runtime_tls_destructor(void *ptr) {
  ascii_instr_runtime_destroy((ascii_instr_runtime_t *)ptr);
}

static bool ascii_instr_build_log_path(ascii_instr_runtime_t *runtime) {
  char dir_buf[PATH_MAX];
  const char *output_dir = g_output_dir_set ? g_output_dir : SAFE_GETENV("TMPDIR");
  if (output_dir == NULL) {
    output_dir = SAFE_GETENV("TEMP");
  }
  if (output_dir == NULL) {
    output_dir = SAFE_GETENV("TMP");
  }

  if (output_dir == NULL) {
    output_dir = "/tmp";
  }

  const size_t dir_len = strnlen(output_dir, sizeof(dir_buf) - 1);
  if (dir_len == 0 || dir_len >= sizeof(dir_buf)) {
    return false;
  }

  memcpy(dir_buf, output_dir, dir_len);
  dir_buf[dir_len] = '\0';

  if (snprintf(runtime->log_path, sizeof(runtime->log_path), "%s/%s-%d-%llu.log", dir_buf, ASCII_INSTR_DEFAULT_BASENAME,
               runtime->pid, (unsigned long long)runtime->thread_id) >= (int)sizeof(runtime->log_path)) {
    return false;
  }

  struct stat st;
  if (stat(runtime->log_path, &st) == 0) {
    return false;
  }

  const char *last_sep = strrchr(runtime->log_path, '/');
#ifdef _WIN32
  const char *last_backslash = strrchr(runtime->log_path, '\\');
  if (last_backslash != NULL && (last_sep == NULL || last_backslash > last_sep)) {
    last_sep = last_backslash;
  }
#endif
  if (last_sep != NULL && last_sep != runtime->log_path) {
    const size_t dir_path_len = (size_t)(last_sep - runtime->log_path);
    char dir_path[PATH_MAX];
    memcpy(dir_path, runtime->log_path, dir_path_len);
    dir_path[dir_path_len] = '\0';
    if (mkdir(dir_path, 0700) != 0) {
      if (errno != EEXIST) {
        return false;
      }
    }
  }

  return true;
}

static int ascii_instr_open_log_file(ascii_instr_runtime_t *runtime) {
  if (!ascii_instr_build_log_path(runtime)) {
    return -1;
  }

  const int flags = PLATFORM_O_WRONLY | PLATFORM_O_CREAT | PLATFORM_O_EXCL | PLATFORM_O_APPEND | PLATFORM_O_BINARY;
  const mode_t mode = S_IRUSR | S_IWUSR;
  int fd = platform_open(runtime->log_path, flags, mode);
  if (fd < 0) {
    return -1;
  }

  runtime->fd = fd;
  return 0;
}

static void ascii_instr_runtime_configure(ascii_instr_runtime_t *runtime) {
  runtime->filter_function_include = SAFE_GETENV("ASCII_INSTR_FUNCTION_INCLUDE");
  runtime->filter_function_exclude = SAFE_GETENV("ASCII_INSTR_FUNCTION_EXCLUDE");

  const char *only_env = SAFE_GETENV("ASCII_INSTR_ONLY");
  ascii_instr_parse_only_filters(runtime, only_env);

#if ASCII_INSTR_HAVE_REGEX
  const char *include_regex = SAFE_GETENV("ASCII_INSTR_INCLUDE_REGEX");
  if (include_regex != NULL && include_regex[0] != '\0') {
    runtime->include_regex_valid = ascii_instr_compile_regex(&runtime->include_regex, include_regex);
  }

  const char *exclude_regex = SAFE_GETENV("ASCII_INSTR_EXCLUDE_REGEX");
  if (exclude_regex != NULL && exclude_regex[0] != '\0') {
    runtime->exclude_regex_valid = ascii_instr_compile_regex(&runtime->exclude_regex, exclude_regex);
  }

  const char *function_include_regex = SAFE_GETENV("ASCII_INSTR_FUNCTION_INCLUDE_REGEX");
  if (function_include_regex != NULL && function_include_regex[0] != '\0') {
    runtime->function_include_regex_valid =
        ascii_instr_compile_regex(&runtime->function_include_regex, function_include_regex);
  }

  const char *function_exclude_regex = SAFE_GETENV("ASCII_INSTR_FUNCTION_EXCLUDE_REGEX");
  if (function_exclude_regex != NULL && function_exclude_regex[0] != '\0') {
    runtime->function_exclude_regex_valid =
        ascii_instr_compile_regex(&runtime->function_exclude_regex, function_exclude_regex);
  }
#endif

  const char *rate_env = SAFE_GETENV("ASCII_INSTR_RATE");
  uint32_t rate_value = 0;
  if (ascii_instr_parse_positive_uint32(rate_env, &rate_value) && rate_value > 1U) {
    runtime->rate = rate_value;
    runtime->rate_enabled = true;
  }

  runtime->filters_enabled = (runtime->filter_include != NULL) || (runtime->filter_exclude != NULL) ||
                             (runtime->filter_thread != NULL) || (runtime->filter_function_include != NULL) ||
                             (runtime->filter_function_exclude != NULL)
#if ASCII_INSTR_HAVE_REGEX
                             || runtime->include_regex_valid || runtime->exclude_regex_valid ||
                             runtime->function_include_regex_valid || runtime->function_exclude_regex_valid
#endif
                             || runtime->only_selectors.count > 0;
}

static bool ascii_instr_env_is_enabled(const char *value) {
  if (value == NULL) {
    return false;
  }

  while (*value != '\0' && isspace((unsigned char)*value) != 0) {
    value++;
  }

  if (*value == '\0') {
    return false;
  }

  if (strcmp(value, "0") == 0) {
    return false;
  }

  char lowered[8];
  size_t len = 0;
  while (value[len] != '\0' && len < sizeof(lowered) - 1) {
    lowered[len] = (char)tolower((unsigned char)value[len]);
    len++;
  }
  lowered[len] = '\0';

  if (strcmp(lowered, "false") == 0 || strcmp(lowered, "off") == 0 || strcmp(lowered, "no") == 0) {
    return false;
  }

  return true;
}

static bool ascii_instr_parse_positive_uint32(const char *value, uint32_t *out_value) {
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

#if ASCII_INSTR_HAVE_REGEX
static bool ascii_instr_compile_regex(regex_t *regex, const char *pattern) {
  if (regex == NULL || pattern == NULL) {
    return false;
  }
  int ret = regcomp(regex, pattern, REG_EXTENDED | REG_NOSUB);
  if (ret != 0) {
    return false;
  }
  return true;
}
#endif

static void ascii_instr_only_list_destroy(ascii_instr_only_list_t *list) {
  if (list == NULL) {
    return;
  }
  if (list->items != NULL) {
    for (size_t i = 0; i < list->count; ++i) {
      ascii_instr_only_selector_t *selector = &list->items[i];
      SAFE_FREE(selector->pattern);
      SAFE_FREE(selector->module);
    }
    SAFE_FREE(list->items);
  }
  list->count = 0;
  list->capacity = 0;
}

static bool ascii_instr_only_list_append(ascii_instr_only_list_t *list, ascii_instr_selector_type_t type,
                                         const char *module, const char *pattern) {
  if (list == NULL) {
    return false;
  }

  if (type == ASCII_INSTR_SELECTOR_FILE_SUBSTRING || type == ASCII_INSTR_SELECTOR_FILE_GLOB ||
      type == ASCII_INSTR_SELECTOR_FUNCTION_GLOB) {
    if (pattern == NULL || pattern[0] == '\0') {
      return false;
    }
  }

  if (type == ASCII_INSTR_SELECTOR_MODULE) {
    if (module == NULL || module[0] == '\0') {
      return false;
    }
  }

  if (list->count == list->capacity) {
    size_t new_capacity = (list->capacity == 0) ? 4U : list->capacity * 2U;
    ascii_instr_only_selector_t *new_items =
        SAFE_REALLOC(list->items, new_capacity * sizeof(*new_items), ascii_instr_only_selector_t *);
    list->items = new_items;
    list->capacity = new_capacity;
  }

  ascii_instr_only_selector_t *selector = &list->items[list->count];
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

static void ascii_instr_trim(char *value) {
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

static bool ascii_instr_parse_only_filters(ascii_instr_runtime_t *runtime, const char *value) {
  if (runtime == NULL) {
    return false;
  }

  ascii_instr_only_list_destroy(&runtime->only_selectors);

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

    ascii_instr_trim(token_start);
    if (*token_start == '\0') {
      continue;
    }

    char *equal_sign = strchr(token_start, '=');
    if (equal_sign != NULL) {
      *equal_sign = '\0';
      char *kind = token_start;
      char *spec = equal_sign + 1;
      ascii_instr_trim(kind);
      ascii_instr_trim(spec);
      if (*spec == '\0') {
        continue;
      }

      if (strcmp(kind, "file") == 0) {
        (void)ascii_instr_only_list_append(&runtime->only_selectors, ASCII_INSTR_SELECTOR_FILE_GLOB, NULL, spec);
      } else if (strcmp(kind, "func") == 0 || strcmp(kind, "function") == 0) {
        (void)ascii_instr_only_list_append(&runtime->only_selectors, ASCII_INSTR_SELECTOR_FUNCTION_GLOB, NULL, spec);
      } else if (strcmp(kind, "module") == 0) {
        char *module_value = spec;
        char *module_pattern = strchr(module_value, ':');
        if (module_pattern != NULL) {
          *module_pattern = '\0';
          module_pattern++;
          ascii_instr_trim(module_pattern);
        }
        ascii_instr_trim(module_value);
        if (*module_value == '\0') {
          continue;
        }
        const char *pattern_part = (module_pattern != NULL && *module_pattern != '\0') ? module_pattern : NULL;
        (void)ascii_instr_only_list_append(&runtime->only_selectors, ASCII_INSTR_SELECTOR_MODULE, module_value,
                                           pattern_part);
      } else {
        (void)ascii_instr_only_list_append(&runtime->only_selectors, ASCII_INSTR_SELECTOR_FILE_GLOB, NULL, spec);
      }
      continue;
    }

    char *colon = strchr(token_start, ':');
    if (colon != NULL) {
      *colon = '\0';
      char *module_name = token_start;
      char *pattern_part = colon + 1;
      ascii_instr_trim(module_name);
      ascii_instr_trim(pattern_part);
      if (*module_name == '\0') {
        continue;
      }
      const char *pattern_spec = (*pattern_part != '\0') ? pattern_part : NULL;
      (void)ascii_instr_only_list_append(&runtime->only_selectors, ASCII_INSTR_SELECTOR_MODULE, module_name,
                                         pattern_spec);
      continue;
    }

    (void)ascii_instr_only_list_append(&runtime->only_selectors, ASCII_INSTR_SELECTOR_FILE_SUBSTRING, NULL,
                                       token_start);
  }

  SAFE_FREE(mutable_value);
  return true;
}

static bool ascii_instr_match_glob(const char *pattern, const char *value) {
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

static const char *ascii_instr_basename(const char *path) {
  if (path == NULL) {
    return NULL;
  }

  const char *slash = strrchr(path, '/');
#ifdef _WIN32
  const char *backslash = strrchr(path, '\\');
  if (backslash != NULL && (slash == NULL || backslash > slash)) {
    slash = backslash;
  }
#endif
  if (slash != NULL && slash[1] != '\0') {
    return slash + 1;
  }
  return path;
}

static bool ascii_instr_path_contains_module(const char *file_path, const char *module_name) {
  if (file_path == NULL || module_name == NULL || module_name[0] == '\0') {
    return false;
  }

  const size_t module_len = strlen(module_name);
  const char *cursor = file_path;
  while ((cursor = strstr(cursor, module_name)) != NULL) {
    bool left_ok = (cursor == file_path);
    if (!left_ok) {
      const char prev = cursor[-1];
      left_ok = (prev == '/' || prev == '\\');
    }

    const char tail = cursor[module_len];
    bool right_ok = (tail == '\0' || tail == '/' || tail == '\\');

    if (left_ok && right_ok) {
      return true;
    }

    cursor = cursor + 1;
  }

  return false;
}

static bool ascii_instr_only_selectors_match(const ascii_instr_runtime_t *runtime, const char *file_path,
                                             const char *function_name) {
  const ascii_instr_only_list_t *list = &runtime->only_selectors;
  if (list->count == 0) {
    return true;
  }

  const char *base_name = ascii_instr_basename(file_path);
  for (size_t i = 0; i < list->count; ++i) {
    const ascii_instr_only_selector_t *selector = &list->items[i];
    switch (selector->type) {
    case ASCII_INSTR_SELECTOR_FILE_SUBSTRING:
      if (file_path != NULL && selector->pattern != NULL && strstr(file_path, selector->pattern) != NULL) {
        return true;
      }
      break;
    case ASCII_INSTR_SELECTOR_FILE_GLOB:
      if (file_path != NULL && selector->pattern != NULL && ascii_instr_match_glob(selector->pattern, file_path)) {
        return true;
      }
      break;
    case ASCII_INSTR_SELECTOR_FUNCTION_GLOB:
      if (function_name != NULL && selector->pattern != NULL &&
          ascii_instr_match_glob(selector->pattern, function_name)) {
        return true;
      }
      break;
    case ASCII_INSTR_SELECTOR_MODULE:
      if (file_path != NULL && selector->module != NULL &&
          ascii_instr_path_contains_module(file_path, selector->module)) {
        if (selector->pattern == NULL || (base_name != NULL && ascii_instr_match_glob(selector->pattern, base_name))) {
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

static bool ascii_instr_should_log(const ascii_instr_runtime_t *runtime, const char *file_path, uint32_t line_number,
                                   const char *function_name) {
  if (!runtime->filters_enabled) {
    return true;
  }

  if (runtime->filter_thread != NULL) {
    char tid_buf[32];
    snprintf(tid_buf, sizeof(tid_buf), "%llu", (unsigned long long)runtime->thread_id);
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

#if ASCII_INSTR_HAVE_REGEX
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

#if ASCII_INSTR_HAVE_REGEX
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
    if (!ascii_instr_only_selectors_match(runtime, file_path, function_name)) {
      return false;
    }
  }

  (void)line_number;
  return true;
}

static int ascii_instr_write_full(int fd, const char *buffer, size_t len) {
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
