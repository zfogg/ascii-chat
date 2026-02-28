/**
 * @file platform/wasm/stubs/filesystem.c
 * @brief File operation stubs for WASM
 * @ingroup platform
 */

#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/wasm_console.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/asciichat_errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>

// File operations - basic WASM stubs
FILE *platform_fopen(const char *name, const char *filename, const char *mode) {
  if (!name) {
    return NULL;
  }
  FILE *stream = fopen(filename, mode); // Use standard fopen
  if (stream) {
    int fd = fileno(stream);
    if (fd >= 0) {
      NAMED_REGISTER_FD(fd, name);
    }
  }
  return stream;
}

FILE *platform_tmpfile(void) {
  return tmpfile();
}

size_t platform_write_all(int fd, const void *buf, size_t count) {
  (void)fd;
  (void)buf;
  (void)count;
  return 0; // Not implemented for WASM
}

asciichat_error_t platform_mkdir_recursive(const char *path, int mode) {
  (void)path;
  (void)mode;
  return ERROR_PLATFORM_INIT; // Not implemented for WASM
}

asciichat_error_t platform_find_config_file(const char *filename, config_file_list_t *list_out) {
  (void)filename;
  if (list_out) {
    list_out->files = NULL;
    list_out->count = 0;
    list_out->capacity = 0;
  }
  return ASCIICHAT_OK; // No config files in WASM
}

void config_file_list_destroy(config_file_list_t *list) {
  (void)list;
  // No-op
}

// File descriptor operations
ssize_t platform_write(int fd, const void *buf, size_t count) {
  // For WASM, route stdout/stderr to browser console
  if ((fd == 1 || fd == 2) && buf && count > 0) {
    wasm_log_to_console(fd, (const uint8_t *)buf, count);
    return (ssize_t)count; // Report all bytes as written
  }

  // Fallback: use standard write for other fds
  return write(fd, buf, count);
}

ssize_t platform_read(int fd, void *buf, size_t count) {
  return read(fd, buf, count);
}

int platform_open(const char *name, const char *pathname, int flags, ...) {
  if (!name) {
    return -1;
  }

  // Handle optional mode parameter for O_CREAT
  int fd = -1;
  int mode = 0;
  if (flags & 0x0200) { // O_CREAT flag value
    va_list args;
    va_start(args, flags);
    mode = va_arg(args, int);
    va_end(args);
    fd = open(pathname, flags, mode);
  } else {
    fd = open(pathname, flags);
  }

  if (fd >= 0) {
    NAMED_REGISTER_FD(fd, name);
    log_dev("Opened file descriptor %d for %s at path %s", fd, name, pathname);
  }

  return fd;
}

int platform_close(int fd) {
  return close(fd);
}

int platform_access(const char *pathname, int mode) {
  (void)pathname;
  (void)mode;
  return -1; // Not accessible in WASM
}

char *platform_get_data_dir(void) {
  return NULL; // No data directory in WASM
}
