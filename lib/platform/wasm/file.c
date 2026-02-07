/**
 * @file platform/wasm/file.c
 * @brief File operation stubs for WASM
 */

#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/asciichat_errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>

// File operations - basic WASM stubs
FILE *platform_fopen(const char *filename, const char *mode) {
  return fopen(filename, mode); // Use standard fopen
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
  return write(fd, buf, count);
}

ssize_t platform_read(int fd, void *buf, size_t count) {
  return read(fd, buf, count);
}

int platform_open(const char *pathname, int flags, ...) {
  // Handle optional mode parameter for O_CREAT
  int mode = 0;
  if (flags & 0x0200) { // O_CREAT flag value
    va_list args;
    va_start(args, flags);
    mode = va_arg(args, int);
    va_end(args);
    return open(pathname, flags, mode);
  }
  return open(pathname, flags);
}

int platform_close(int fd) {
  return close(fd);
}
