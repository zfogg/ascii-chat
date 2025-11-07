// SPDX-License-Identifier: MIT
// Debug instrumentation logging runtime for ascii-chat line tracing

#include "debug/instrument_log.h"

#include "common.h"
#include "platform/internal.h"
#include "platform/system.h"
#include "platform/thread.h"
#include "util/time.h"

#include <errno.h>
#include <fcntl.h>
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

typedef struct ascii_instr_runtime {
  int fd;
  int pid;
  uint64_t thread_id;
  uint64_t sequence;
  char log_path[PATH_MAX];
  bool filters_enabled;
  const char *filter_include;
  const char *filter_exclude;
  const char *filter_thread;
  bool stderr_fallback;
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

static void ascii_instr_runtime_init_once(void);
static void ascii_instr_runtime_tls_destructor(void *ptr);
static int ascii_instr_open_log_file(ascii_instr_runtime_t *runtime);
static void ascii_instr_runtime_configure(ascii_instr_runtime_t *runtime);
static bool ascii_instr_should_log(const ascii_instr_runtime_t *runtime, const char *file_path, uint32_t line_number,
                                   const char *function_name);
static int ascii_instr_write_full(int fd, const char *buffer, size_t len);

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
  runtime->fd = -1;
  runtime->filter_include = SAFE_GETENV("ASCII_INSTR_INCLUDE");
  runtime->filter_exclude = SAFE_GETENV("ASCII_INSTR_EXCLUDE");
  runtime->filter_thread = SAFE_GETENV("ASCII_INSTR_THREAD");
  runtime->filters_enabled =
      (runtime->filter_include != NULL) || (runtime->filter_exclude != NULL) || (runtime->filter_thread != NULL);

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

  pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                  "pid=%d tid=%llu seq=%llu ts=%.*s.%09ldZ elapsed=%s file=%s line=%u func=%s macro=%u snippet=",
                  runtime->pid, (unsigned long long)runtime->thread_id, (unsigned long long)runtime->sequence,
                  (int)ts_len, timestamp, (long)ts.tv_nsec, elapsed_field, file_path, line_number,
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
  (void)runtime;
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

  (void)line_number;
  (void)function_name;
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
