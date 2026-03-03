/**
 * @file filesystem.c
 * @brief Cross-platform filesystem utilities and binary PATH cache
 */

#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/log/log.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

// Thread-local storage for error messages (avoids allocation)
static _Thread_local char error_msg_buffer[512];

const char *file_read_error_message(const char *path) {
  switch (errno) {
  case ENOENT:
    snprintf(error_msg_buffer, sizeof(error_msg_buffer), "File does not exist: %s", path);
    break;
  case EACCES:
    snprintf(error_msg_buffer, sizeof(error_msg_buffer), "Permission denied (cannot read): %s", path);
    break;
  case EISDIR:
    snprintf(error_msg_buffer, sizeof(error_msg_buffer), "Is a directory, not a file: %s", path);
    break;
  default:
    snprintf(error_msg_buffer, sizeof(error_msg_buffer), "Failed to open for reading: %s (%s)", path,
             platform_strerror(errno));
    break;
  }
  return error_msg_buffer;
}

const char *file_write_error_message(const char *path) {
  switch (errno) {
  case ENOENT:
    snprintf(error_msg_buffer, sizeof(error_msg_buffer), "Directory does not exist: %s", path);
    break;
  case EACCES:
    snprintf(error_msg_buffer, sizeof(error_msg_buffer), "Permission denied (cannot write): %s", path);
    break;
  case EROFS:
    snprintf(error_msg_buffer, sizeof(error_msg_buffer), "Read-only filesystem: %s", path);
    break;
  case ENOSPC:
    snprintf(error_msg_buffer, sizeof(error_msg_buffer), "No space left on device: %s", path);
    break;
  case EISDIR:
    snprintf(error_msg_buffer, sizeof(error_msg_buffer), "Is a directory, not a file: %s", path);
    break;
  default:
    snprintf(error_msg_buffer, sizeof(error_msg_buffer), "Failed to open for writing: %s (%s)", path,
             platform_strerror(errno));
    break;
  }
  return error_msg_buffer;
}

/* ============================================================================
 * Binary PATH Cache (moved from system.c)
 * ============================================================================ */

/**
 * @brief Cache entry for binary PATH lookup
 */
typedef struct {
  char *bin_name;
  bool in_path;
  UT_hash_handle hh;
} bin_cache_entry_t;

static bin_cache_entry_t *g_bin_path_cache = NULL;
static rwlock_t g_cache_rwlock;
static lifecycle_t g_cache_lc = LIFECYCLE_INIT;

/**
 * @brief Check if binary is in PATH (no caching)
 * Implemented in posix/filesystem.c and windows/filesystem.c
 */
extern bool check_binary_in_path_uncached(const char *bin_name);

/**
 * @brief Initialize the binary PATH cache
 */
static void init_cache_once(void) {
  if (!lifecycle_init(&g_cache_lc, "cache")) {
    return;
  }

  g_bin_path_cache = NULL;

  if (rwlock_init(&g_cache_rwlock, "binary_cache") != 0) {
    log_error("Failed to initialize binary PATH cache rwlock");
    lifecycle_init_abort(&g_cache_lc);
  }
}

/**
 * @brief Free a cache entry
 */
static void free_cache_entry(bin_cache_entry_t *entry) {
  if (entry) {
    if (entry->bin_name) {
      SAFE_FREE(entry->bin_name);
    }
    SAFE_FREE(entry);
  }
}

void platform_cleanup_binary_path_cache(void) {
  if (!lifecycle_shutdown(&g_cache_lc)) {
    return;
  }

  if (g_bin_path_cache) {
    rwlock_wrlock(&g_cache_rwlock);

    bin_cache_entry_t *entry, *tmp;
    HASH_ITER(hh, g_bin_path_cache, entry, tmp) {
      HASH_DELETE(hh, g_bin_path_cache, entry);
      free_cache_entry(entry);
    }

    rwlock_wrunlock(&g_cache_rwlock);
    rwlock_destroy(&g_cache_rwlock);
    g_bin_path_cache = NULL;
  }
}

bool platform_is_binary_in_path(const char *bin_name) {
  if (!bin_name || bin_name[0] == '\0') {
    return false;
  }

  init_cache_once();
  if (!lifecycle_is_initialized(&g_cache_lc)) {
    SET_ERRNO(ERROR_INVALID_STATE, "Binary PATH cache not initialized, checking directly (this should never happen)");
    return check_binary_in_path_uncached(bin_name);
  }

  rwlock_rdlock(&g_cache_rwlock);
  bin_cache_entry_t *entry = NULL;
  HASH_FIND_STR(g_bin_path_cache, bin_name, entry);
  rwlock_rdunlock(&g_cache_rwlock);

  if (entry) {
    log_dev("Binary '%s' %s in PATH (%s)", bin_name, colored_string(LOG_COLOR_INFO, "found"),
            colored_string(LOG_COLOR_WARN, "cached"));
    return entry->in_path;
  }

  bool found = check_binary_in_path_uncached(bin_name);

  entry = SAFE_MALLOC(sizeof(bin_cache_entry_t), bin_cache_entry_t *);
  if (!entry) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate cache entry");
    return found;
  }

  entry->bin_name = platform_strdup(bin_name);
  if (!entry->bin_name) {
    SET_ERRNO(ERROR_MEMORY, "Failed to duplicate binary name");
    SAFE_FREE(entry);
    return found;
  }

  entry->in_path = found;

  rwlock_wrlock(&g_cache_rwlock);
  HASH_ADD_KEYPTR(hh, g_bin_path_cache, entry->bin_name, strlen(entry->bin_name), entry);
  rwlock_wrunlock(&g_cache_rwlock);

  log_dev("Binary '%s' %s in PATH", bin_name,
          colored_string(found ? LOG_COLOR_INFO : LOG_COLOR_ERROR, found ? "found" : "NOT found"));

  return found;
}

