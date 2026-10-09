/**
 * @file platform/process_title.c
 * @brief Process titles with preserved application arguments and environment.
 */

#include <ascii-chat/platform/process.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/common.h>
#include <stdbool.h>
#include <string.h>

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <stdint.h>
#ifdef __APPLE__
#include <crt_externs.h>
#define TITLE_ENVIRON (*_NSGetEnviron())
#else
#include <unistd.h>
#define TITLE_ENVIRON environ
extern char **environ;
#endif

// Reserve the original contiguous argv/environment area, as in PostgreSQL's
// process-title implementation. Application pointers use a separate copy.
static char *title_area;
static size_t title_capacity;
static char *saved_area;
static char **saved_argv;
static char **original_argv;
static int original_argc;

asciichat_error_t platform_process_title_init(int argc, char ***argv) {
  if (argc < 1 || !argv || !*argv || !(*argv)[0]) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid process title arguments");
  }
  if (title_area) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Process title already initialized");
  }

  char *start = (*argv)[0];
  char *end = start;
  for (int i = 0; i < argc; ++i) {
    if ((*argv)[i] != end) {
      return SET_ERRNO(ERROR_INVALID_STATE, "Process arguments are not contiguous");
    }
    end += strlen(end) + 1;
  }
  for (char **env = TITLE_ENVIRON; env && *env; ++env) {
    if (*env == end) {
      end += strlen(end) + 1;
    }
  }

  title_capacity = (size_t)(end - start);
  saved_area = SAFE_MALLOC(title_capacity, char *);
  memcpy(saved_area, start, title_capacity);
  saved_argv = SAFE_CALLOC((size_t)argc + 1, sizeof(char *), char **);
  for (int i = 0; i < argc; ++i) {
    saved_argv[i] = saved_area + ((*argv)[i] - start);
  }
  // Keep libc's environment vector: setenv() may own/reallocate that vector.
  // Only relocate strings that will be overwritten by the process title.
  for (char **env = TITLE_ENVIRON; env && *env; ++env) {
    uintptr_t address = (uintptr_t)*env;
    if (address >= (uintptr_t)start && address < (uintptr_t)end) {
      *env = saved_area + (address - (uintptr_t)start);
    }
  }
  original_argc = argc;
  original_argv = *argv;
  title_area = start;
  *argv = saved_argv;
#ifdef __APPLE__
  *_NSGetArgv() = saved_argv;
#endif
  return ASCIICHAT_OK;
}

asciichat_error_t platform_process_title_set(const char *title) {
  if (!title) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Process title must not be NULL");
  }
  if (!title_area) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Process title is not initialized");
  }
  size_t length = strlen(title);
  if (length >= title_capacity) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Process title exceeds available argument storage");
  }
  memcpy(title_area, title, length);
  memset(title_area + length, 0, title_capacity - length);
  for (int i = 1; i < original_argc; ++i) {
    original_argv[i] = title_area + title_capacity - 1;
  }
  return ASCIICHAT_OK;
}

void platform_process_title_destroy(void) {
  if (!title_area) {
    return;
  }
  memcpy(title_area, saved_area, title_capacity);
  for (char **env = TITLE_ENVIRON; env && *env; ++env) {
    uintptr_t address = (uintptr_t)*env;
    if (address >= (uintptr_t)saved_area && address < (uintptr_t)saved_area + title_capacity) {
      *env = title_area + (address - (uintptr_t)saved_area);
    }
  }
  for (int i = 0; i < original_argc; ++i) {
    original_argv[i] = title_area + (saved_argv[i] - saved_area);
  }
  if (g_argv == saved_argv) {
    g_argv = original_argv;
  }
#ifdef __APPLE__
  *_NSGetArgv() = original_argv;
#endif
  SAFE_FREE(saved_argv);
  SAFE_FREE(saved_area);
  saved_argv = NULL;
  saved_area = NULL;
  title_area = NULL;
  title_capacity = 0;
  original_argv = NULL;
  original_argc = 0;
}
#else
asciichat_error_t platform_process_title_init(int argc, char ***argv) {
  if (argc < 1 || !argv || !*argv || !(*argv)[0]) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid process title arguments");
  }
  return ASCIICHAT_OK;
}

asciichat_error_t platform_process_title_set(const char *title) {
  if (!title) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Process title must not be NULL");
  }
#ifdef _WIN32
  return terminal_set_title(title);
#else
  return ASCIICHAT_OK;
#endif
}

void platform_process_title_destroy(void) {}
#endif

// Bound the Windows title and avoid allocating in proportion to untrusted argv.
#define PROCESS_TITLE_MAX 32768

typedef struct {
  char *data;
  size_t length;
  size_t capacity;
} title_writer_t;

static void title_append(title_writer_t *writer, const char *text) {
  size_t length = strlen(text);
  size_t available = writer->capacity - writer->length - 1;
  if (length > available) {
    length = available;
  }
  memcpy(writer->data + writer->length, text, length);
  writer->length += length;
  writer->data[writer->length] = '\0';
}

static void title_append_argument(title_writer_t *writer, const char *argument) {
  // Private key text is not accepted by the native CLI, but never display it
  // if a caller nevertheless supplies an armored key block.
  if (strstr(argument, "PRIVATE KEY")) {
    title_append(writer, "[redacted]");
    return;
  }
  bool quote = !*argument || strpbrk(argument, " \t\r\n\"\\") != NULL;
  if (quote) {
    title_append(writer, "\"");
  }
  const char hex[] = "0123456789abcdef";
  for (const unsigned char *p = (const unsigned char *)argument; *p; ++p) {
    if (*p < 32 || *p == 127) {
      char escaped[] = {'\\', 'x', hex[*p >> 4], hex[*p & 15], '\0'};
      title_append(writer, escaped);
    } else {
      if (*p == '"' || *p == '\\') {
        title_append(writer, "\\");
      }
      char character[] = {(char)*p, '\0'};
      title_append(writer, character);
    }
  }
  if (quote) {
    title_append(writer, "\"");
  }
}

static size_t title_secret_option_length(const char *argument) {
  static const char *const secret_options[] = {"--password", "--websocket-tls-key-password", "--turn-credential",
                                               "--turn-secret"};
  for (size_t i = 0; i < sizeof(secret_options) / sizeof(secret_options[0]); ++i) {
    size_t length = strlen(secret_options[i]);
    if (strncmp(argument, secret_options[i], length) == 0 && (argument[length] == '\0' || argument[length] == '=')) {
      return length;
    }
  }
  return 0;
}

asciichat_error_t platform_process_title_set_args(const char *mode, int argc, char **argv, int mode_arg_index) {
  if (!mode || argc < 1 || !argv || !argv[0] || mode_arg_index < -1 || mode_arg_index >= argc) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid process title arguments");
  }
  for (int i = 1; i < argc; ++i) {
    if (!argv[i]) {
      return SET_ERRNO(ERROR_INVALID_PARAM, "NULL process title argument");
    }
  }
  size_t capacity = PROCESS_TITLE_MAX;
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
  if (!title_area) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Process title is not initialized");
  }
  if (title_capacity < capacity) {
    capacity = title_capacity;
  }
#endif
  char *title = SAFE_MALLOC(capacity, char *);
  title_writer_t writer = {.data = title, .length = 0, .capacity = capacity};
  title[0] = '\0';
  title_append(&writer, "ascii-chat: ");
  title_append(&writer, mode);
  title_append(&writer, " mode");
  bool first = true;
  bool redact_next = false;
  for (int i = 1; i < argc; ++i) {
    if (i == mode_arg_index) {
      continue;
    }
    title_append(&writer, first ? " - " : " ");
    first = false;
    if (redact_next) {
      title_append(&writer, "[redacted]");
      redact_next = false;
      continue;
    }
    size_t secret_length = title_secret_option_length(argv[i]);
    if (secret_length) {
      // Render the known option name without ever copying its secret value.
      for (size_t j = 0; j < secret_length; ++j) {
        char character[] = {argv[i][j], '\0'};
        title_append(&writer, character);
      }
      if (argv[i][secret_length] == '=') {
        title_append(&writer, "=[redacted]");
      } else {
        redact_next = true;
      }
    } else {
      title_append_argument(&writer, argv[i]);
    }
  }
  asciichat_error_t result = platform_process_title_set(title);
  SAFE_FREE(title);
  return result;
}
