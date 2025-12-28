/**
 * @file platform/posix/mmap.c
 * @brief POSIX implementation of memory-mapped files (Linux/macOS)
 * @ingroup platform
 */

#include "platform/mmap.h"
#include "log/logging.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

void platform_mmap_init(platform_mmap_t *mapping) {
  if (!mapping) {
    return;
  }
  mapping->addr = NULL;
  mapping->size = 0;
  mapping->fd = -1;
}

asciichat_error_t platform_mmap_open(const char *path, size_t size, platform_mmap_t *out) {
  if (!path || !out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "mmap: NULL path or output pointer");
  }

  if (size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "mmap: size cannot be zero");
  }

  // Open or create the file
  int fd = open(path, O_RDWR | O_CREAT, 0600);
  if (fd < 0) {
    return SET_ERRNO_SYS(ERROR_CONFIG, "mmap: failed to open file: %s", path);
  }

  // Check current file size
  struct stat st;
  if (fstat(fd, &st) < 0) {
    int saved_errno = errno;
    close(fd);
    errno = saved_errno;
    return SET_ERRNO_SYS(ERROR_CONFIG, "mmap: failed to stat file: %s", path);
  }

  // Resize file if needed
  if ((size_t)st.st_size < size) {
    if (ftruncate(fd, (off_t)size) < 0) {
      int saved_errno = errno;
      close(fd);
      errno = saved_errno;
      return SET_ERRNO_SYS(ERROR_CONFIG, "mmap: failed to resize file to %zu bytes: %s", size, path);
    }
    log_debug("mmap: created/resized file %s to %zu bytes", path, size);
  } else if ((size_t)st.st_size > size) {
    // File is larger than requested - use existing size
    size = (size_t)st.st_size;
    log_debug("mmap: using existing file size %zu bytes for %s", size, path);
  }

  // Map the file into memory
  void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (addr == MAP_FAILED) {
    int saved_errno = errno;
    close(fd);
    errno = saved_errno;
    return SET_ERRNO_SYS(ERROR_MEMORY, "mmap: failed to map file: %s", path);
  }

  out->addr = addr;
  out->size = size;
  out->fd = fd;

  log_debug("mmap: mapped %s at %p (%zu bytes)", path, addr, size);
  return ASCIICHAT_OK;
}

void platform_mmap_close(platform_mmap_t *mapping) {
  if (!mapping) {
    return;
  }

  if (mapping->addr && mapping->addr != MAP_FAILED) {
    if (munmap(mapping->addr, mapping->size) < 0) {
      log_warn("mmap: munmap failed: %s", SAFE_STRERROR(errno));
    }
    mapping->addr = NULL;
  }

  if (mapping->fd >= 0) {
    close(mapping->fd);
    mapping->fd = -1;
  }

  mapping->size = 0;
}

void platform_mmap_sync(platform_mmap_t *mapping, bool async) {
  if (!mapping || !mapping->addr || mapping->addr == MAP_FAILED) {
    return;
  }

  int flags = async ? MS_ASYNC : MS_SYNC;
  if (msync(mapping->addr, mapping->size, flags) < 0) {
    log_warn("mmap: msync failed: %s", SAFE_STRERROR(errno));
  }
}

bool platform_mmap_is_valid(const platform_mmap_t *mapping) {
  return mapping && mapping->addr && mapping->addr != MAP_FAILED && mapping->fd >= 0;
}
