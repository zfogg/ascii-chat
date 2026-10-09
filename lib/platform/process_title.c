/**
 * @file platform/process_title.c
 * @brief Process titles with preserved application arguments and environment.
 */

#include <ascii-chat/platform/process.h>
#include <ascii-chat/platform/terminal.h>

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <ascii-chat/common.h>
#include <stdint.h>
#include <string.h>
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
