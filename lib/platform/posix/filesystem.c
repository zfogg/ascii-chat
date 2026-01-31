/**
 * @file platform/posix/filesystem.c
 * @brief POSIX filesystem operations
 * @ingroup platform
 */

#ifndef _WIN32

#include "../filesystem.h"
#include "../system.h"
#include "../util.h"
#include "../../common.h"
#include "../../log/logging.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>

// ============================================================================
// Directory Management
// ============================================================================

/**
 * @brief Create a directory (POSIX implementation)
 */
asciichat_error_t platform_mkdir(const char *path, int mode) {
  if (!path) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid path to platform_mkdir");
  }

  if (mkdir(path, mode) == -1) {
    // EEXIST is not an error - directory may already exist
    if (errno == EEXIST) {
      // Verify it's actually a directory (use stat() to follow symlinks)
      struct stat sb;
      if (stat(path, &sb) == 0 && S_ISDIR(sb.st_mode)) {
        return ASCIICHAT_OK;
      }
      // Path exists but is not a directory
      return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Path exists but is not a directory: %s", path);
    }
    return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to create directory: %s", path);
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Create directories recursively (POSIX implementation)
 */
asciichat_error_t platform_mkdir_recursive(const char *path, int mode) {
  if (!path || path[0] == '\0') {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid path to platform_mkdir_recursive");
  }

  char tmp[512];
  size_t len = strlen(path);
  if (len >= sizeof(tmp)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Path too long for platform_mkdir_recursive: %zu", len);
  }

  strncpy(tmp, path, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';

  // Create each directory in the path
  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/' || *p == '\\') {
      char orig = *p;
      *p = '\0';

      // Skip empty components
      if (tmp[0] != '\0' && strcmp(tmp, ".") != 0) {
        if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
          // Propagate error if not EEXIST
          return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to create directory: %s", tmp);
        }
      }

      *p = orig;
    }
  }

  // Create the final directory
  if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
    return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to create directory: %s", tmp);
  }

  return ASCIICHAT_OK;
}

// ============================================================================
// File Statistics
// ============================================================================

/**
 * @brief Get file statistics (POSIX implementation)
 */
asciichat_error_t platform_stat(const char *path, platform_stat_t *stat_out) {
  if (!path || !stat_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters to platform_stat");
  }

  struct stat sb;
  if (lstat(path, &sb) == -1) {
    log_dev("Failed to stat file: %s", path);
    return ERROR_FILE_NOT_FOUND;
  }

  stat_out->size = (size_t)sb.st_size;
  stat_out->mode = sb.st_mode;
  stat_out->is_regular_file = S_ISREG(sb.st_mode) ? 1 : 0;
  stat_out->is_directory = S_ISDIR(sb.st_mode) ? 1 : 0;
  stat_out->is_symlink = S_ISLNK(sb.st_mode) ? 1 : 0;

  return ASCIICHAT_OK;
}

/**
 * @brief Check if a path is a regular file (POSIX implementation)
 */
int platform_is_regular_file(const char *path) {
  if (!path) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: path=%p", path);
    return 0;
  }

  platform_stat_t stat_info;
  if (platform_stat(path, &stat_info) != ASCIICHAT_OK) {
    return 0;
  }

  return stat_info.is_regular_file;
}

/**
 * @brief Check if a path is a directory (POSIX implementation)
 */
int platform_is_directory(const char *path) {
  if (!path) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: path=%p", path);
    return 0;
  }

  platform_stat_t stat_info;
  if (platform_stat(path, &stat_info) != ASCIICHAT_OK) {
    return 0;
  }

  return stat_info.is_directory;
}

// ============================================================================
// Temporary Files and Directories
// ============================================================================

int platform_create_temp_file(char *path_out, size_t path_size, const char *prefix, int *fd) {
  /* Unix temp file creation with mkstemp() - include PID for concurrent process safety */
  int needed = snprintf(path_out, path_size, "/tmp/%s_%d_XXXXXX", prefix, getpid());
  if (needed < 0 || (size_t)needed >= path_size) {
    return -1;
  }

  int temp_fd = mkstemp(path_out);
  if (temp_fd < 0) {
    return -1;
  }

  *fd = temp_fd;
  return 0;
}

int platform_delete_temp_file(const char *path) {
  return unlink(path);
}

asciichat_error_t platform_mkdtemp(char *path_out, size_t path_size, const char *prefix) {
  if (!path_out || path_size < 32 || !prefix) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for platform_mkdtemp");
  }

  /* Create temp directory template with PID for concurrent process safety */
  int needed = snprintf(path_out, path_size, "/tmp/%s_%d_XXXXXX", prefix, getpid());
  if (needed < 0 || (size_t)needed >= path_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Path buffer too small for temporary directory");
  }

  /* mkdtemp modifies the template in-place and returns the path on success */
  if (mkdtemp(path_out) == NULL) {
    return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to create temporary directory");
  }

  return ASCIICHAT_OK;
}

asciichat_error_t platform_rmdir_recursive(const char *path) {
  if (!path) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "path is NULL");
  }

  DIR *dir = opendir(path);
  if (!dir) {
    /* Path doesn't exist or isn't a directory - treat as success (no-op) */
    return ASCIICHAT_OK;
  }

  struct dirent *entry;
  asciichat_error_t result = ASCIICHAT_OK;

  while ((entry = readdir(dir)) != NULL) {
    /* Skip . and .. */
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    /* Build full path to entry */
    char full_path[PLATFORM_MAX_PATH_LENGTH];
    int len = snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
    if (len < 0 || len >= (int)sizeof(full_path)) {
      log_warn("Path too long during directory cleanup: %s/%s", path, entry->d_name);
      continue;
    }

    /* Check if it's a directory using stat */
    struct stat st;
    if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
      /* Recursively delete subdirectory */
      asciichat_error_t subdir_result = platform_rmdir_recursive(full_path);
      if (subdir_result != ASCIICHAT_OK) {
        result = subdir_result;
      }
    } else {
      /* Delete file */
      if (unlink(full_path) != 0) {
        log_warn("Failed to delete file during cleanup: %s", full_path);
        result = ERROR_FILE_OPERATION;
      }
    }
  }

  closedir(dir);

  /* Delete the directory itself */
  if (rmdir(path) != 0) {
    return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Failed to delete directory: %s", path);
  }

  return result;
}

// ============================================================================
// Key File Security
// ============================================================================

// Group and other permissions mask - keys should only be readable by owner
#define SSH_KEY_PERMISSIONS_MASK (S_IRWXG | S_IRWXO)

asciichat_error_t platform_validate_key_file_permissions(const char *key_path) {
  if (!key_path) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: key_path=%p", key_path);
  }

  struct stat st;
  if (stat(key_path, &st) != 0) {
    return SET_ERRNO_SYS(ERROR_CRYPTO_KEY, "Cannot stat key file: %s", key_path);
  }

  // Check for overly permissive permissions
  // SSH keys should only be readable by owner (permissions 0600 or 0400)
  if ((st.st_mode & SSH_KEY_PERMISSIONS_MASK) != 0) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Key file has overly permissive permissions: %o (recommended: 600 or 400)",
                     st.st_mode & 0777);
  }

  return ASCIICHAT_OK;
}

// ============================================================================
// Config File Search
// ============================================================================

/**
 * @brief Get XDG_CONFIG_HOME directory with fallback to ~/.config
 * @return Allocated string (must be freed by caller)
 */
static char *get_xdg_config_home(void) {
  const char *xdg_config_home = getenv("XDG_CONFIG_HOME");

  if (xdg_config_home && xdg_config_home[0] != '\0') {
    return platform_strdup(xdg_config_home);
  }

  // Fallback: ~/.config
  const char *home = getenv("HOME");
  if (!home) {
    home = "/root";
  }

  char path[PLATFORM_MAX_PATH_LENGTH];
  int ret = snprintf(path, sizeof(path), "%s/.config", home);
  if (ret < 0 || (size_t)ret >= sizeof(path)) {
    return NULL; // Path too long
  }

  return platform_strdup(path);
}

/**
 * @brief Parse XDG_CONFIG_DIRS (colon-separated) into array of directories
 * @param dirs_out Pointer to array (allocated by this function)
 * @param count_out Pointer to count of directories
 * @return ASCIICHAT_OK on success
 *
 * Default if XDG_CONFIG_DIRS not set: /etc/xdg
 * Caller must free dirs_out array with SAFE_FREE
 */
static asciichat_error_t get_xdg_config_dirs(char ***dirs_out, size_t *count_out) {
  if (!dirs_out || !count_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: dirs_out=%p, count_out=%p", dirs_out, count_out);
  }

  *dirs_out = NULL;
  *count_out = 0;

  const char *xdg_config_dirs = getenv("XDG_CONFIG_DIRS");
  if (!xdg_config_dirs || xdg_config_dirs[0] == '\0') {
    xdg_config_dirs = "/etc/xdg"; // Default
  }

  // Count colons to determine number of directories
  size_t num_dirs = 1;
  for (const char *p = xdg_config_dirs; *p; p++) {
    if (*p == ':') {
      num_dirs++;
    }
  }

  // Allocate array of directory strings
  char **dirs = SAFE_MALLOC(sizeof(char *) * num_dirs, char **);
  if (!dirs) {
    return ERROR_MEMORY;
  }

  // Parse the colon-separated list
  char *dirs_copy = platform_strdup(xdg_config_dirs);
  if (!dirs_copy) {
    SAFE_FREE(dirs);
    return ERROR_MEMORY;
  }

  size_t idx = 0;
  char *saveptr = NULL;
  char *token = strtok_r(dirs_copy, ":", &saveptr);

  while (token && idx < num_dirs) {
    // Skip empty tokens
    if (token[0] != '\0') {
      dirs[idx] = platform_strdup(token);
      if (!dirs[idx]) {
        // Free previously allocated directories on error
        for (size_t i = 0; i < idx; i++) {
          SAFE_FREE(dirs[i]);
        }
        SAFE_FREE(dirs);
        SAFE_FREE(dirs_copy);
        return ERROR_MEMORY;
      }
      idx++;
    }
    token = strtok_r(NULL, ":", &saveptr);
  }

  SAFE_FREE(dirs_copy);

  *dirs_out = dirs;
  *count_out = idx;
  return ASCIICHAT_OK;
}

/**
 * @brief Find config file across multiple standard locations (POSIX implementation)
 *
 * Search priority (highest to lowest):
 * 1. XDG_CONFIG_HOME/ascii-chat (default: ~/.config/ascii-chat)
 * 2. Each directory in XDG_CONFIG_DIRS/ascii-chat (default: /etc/xdg/ascii-chat)
 * 3. Backward compatibility paths:
 *    - /opt/homebrew/etc/ascii-chat (macOS Homebrew ARM)
 *    - /usr/local/etc/ascii-chat (Unix/Linux local)
 *    - /etc/ascii-chat (system-wide)
 */
asciichat_error_t platform_find_config_file(const char *filename, config_file_list_t *list_out) {
  if (!filename || !list_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters to platform_find_config_file");
  }

  // Initialize output list
  list_out->files = NULL;
  list_out->count = 0;
  list_out->capacity = 0;

  // Get XDG directories
  char *xdg_config_home = get_xdg_config_home();
  if (!xdg_config_home) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to get XDG_CONFIG_HOME");
  }

  char **xdg_config_dirs = NULL;
  size_t xdg_config_dirs_count = 0;
  asciichat_error_t xdg_result = get_xdg_config_dirs(&xdg_config_dirs, &xdg_config_dirs_count);
  if (xdg_result != ASCIICHAT_OK) {
    SAFE_FREE(xdg_config_home);
    return SET_ERRNO(ERROR_MEMORY, "Failed to parse XDG_CONFIG_DIRS");
  }

  // Calculate total number of search directories
  // 1 (XDG_CONFIG_HOME) + xdg_config_dirs_count + 3 (legacy paths)
  const size_t total_dirs = 1 + xdg_config_dirs_count + 3;

  // Pre-allocate capacity for all possible results
  list_out->capacity = total_dirs;
  list_out->files = SAFE_MALLOC(sizeof(config_file_result_t) * total_dirs, config_file_result_t *);
  if (!list_out->files) {
    SAFE_FREE(xdg_config_home);
    for (size_t i = 0; i < xdg_config_dirs_count; i++) {
      SAFE_FREE(xdg_config_dirs[i]);
    }
    SAFE_FREE(xdg_config_dirs);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate config file list");
  }

  uint8_t priority = 0;
  char full_path[PLATFORM_MAX_PATH_LENGTH];

  // Priority 0: XDG_CONFIG_HOME/ascii-chat (highest priority, user config)
  int ret = snprintf(full_path, sizeof(full_path), "%s/ascii-chat/%s", xdg_config_home, filename);
  if (ret >= 0 && (size_t)ret < sizeof(full_path)) {
    if (platform_is_regular_file(full_path)) {
      config_file_result_t *result = &list_out->files[list_out->count];
      result->path = platform_strdup(full_path);
      if (result->path) {
        result->priority = priority++;
        result->exists = true;
        result->is_system_config = false; // User config
        list_out->count++;
      }
    }
  }

  // Priorities 1+: XDG_CONFIG_DIRS/ascii-chat
  for (size_t i = 0; i < xdg_config_dirs_count; i++) {
    ret = snprintf(full_path, sizeof(full_path), "%s/ascii-chat/%s", xdg_config_dirs[i], filename);
    if (ret >= 0 && (size_t)ret < sizeof(full_path)) {
      if (platform_is_regular_file(full_path)) {
        config_file_result_t *result = &list_out->files[list_out->count];
        result->path = platform_strdup(full_path);
        if (result->path) {
          result->priority = priority++;
          result->exists = true;
          result->is_system_config = true; // System config
          list_out->count++;
        }
      }
    }
  }

  // Backward compatibility: Legacy paths
  const char *legacy_dirs[] = {
      "/opt/homebrew/etc/ascii-chat", // macOS Homebrew ARM
      "/usr/local/etc/ascii-chat",    // Unix/Linux local
      "/etc/ascii-chat",              // System-wide
  };
  const size_t num_legacy = sizeof(legacy_dirs) / sizeof(legacy_dirs[0]);

  for (size_t i = 0; i < num_legacy; i++) {
    ret = snprintf(full_path, sizeof(full_path), "%s/%s", legacy_dirs[i], filename);
    if (ret >= 0 && (size_t)ret < sizeof(full_path)) {
      if (platform_is_regular_file(full_path)) {
        config_file_result_t *result = &list_out->files[list_out->count];
        result->path = platform_strdup(full_path);
        if (result->path) {
          result->priority = priority++;
          result->exists = true;
          result->is_system_config = true; // System config
          list_out->count++;
        }
      }
    }
  }

  // Cleanup
  SAFE_FREE(xdg_config_home);
  for (size_t i = 0; i < xdg_config_dirs_count; i++) {
    SAFE_FREE(xdg_config_dirs[i]);
  }
  SAFE_FREE(xdg_config_dirs);

  return ASCIICHAT_OK;
}

/**
 * @brief Free config file list resources (POSIX implementation)
 */
void config_file_list_free(config_file_list_t *list) {
  if (!list) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: list=%p", list);
    return;
  }

  if (list->files) {
    for (size_t i = 0; i < list->count; i++) {
      SAFE_FREE(list->files[i].path);
    }
    SAFE_FREE(list->files);
  }

  list->count = 0;
  list->capacity = 0;
}

// ============================================================================
// Home and Config Directory Discovery (POSIX Implementation)
// ============================================================================

const char *platform_get_home_dir(void) {
  /* Unix: Use HOME environment variable */
  return platform_getenv("HOME");
}

char *platform_get_config_dir(void) {
  /* Unix: Use $XDG_CONFIG_HOME/ascii-chat/ if set */
  const char *xdg_config_home = platform_getenv("XDG_CONFIG_HOME");
  if (xdg_config_home && xdg_config_home[0] != '\0') {
    size_t len = strlen(xdg_config_home) + strlen("/ascii-chat/") + 1;
    char *dir = SAFE_MALLOC(len, char *);
    if (!dir) {
      return NULL;
    }
    safe_snprintf(dir, len, "%s/ascii-chat/", xdg_config_home);
    return dir;
  }

  /* Fallback: ~/.config/ascii-chat/ (XDG Base Directory standard) */
  const char *home = platform_getenv("HOME");
  if (home && home[0] != '\0') {
    size_t len = strlen(home) + strlen("/.config/ascii-chat/") + 1;
    char *dir = SAFE_MALLOC(len, char *);
    if (!dir) {
      return NULL;
    }
    safe_snprintf(dir, len, "%s/.config/ascii-chat/", home);
    return dir;
  }

  return NULL;
}

// ============================================================================
// Platform Path Utilities
// ============================================================================

/**
 * @brief Open temporary file (POSIX)
 *
 * On POSIX, platform_create_temp_file already returns a valid fd, so just return it.
 */
asciichat_error_t platform_temp_file_open(const char *path, int *fd_out) {
  (void)path; // Unused on POSIX - fd already obtained from platform_create_temp_file
  if (!fd_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "fd_out cannot be NULL");
  }
  /* On POSIX, the caller already has the fd from platform_create_temp_file */
  /* This is a no-op wrapper for API consistency */
  return ASCIICHAT_OK;
}

/**
 * @brief Skip absolute path prefix (POSIX)
 *
 * On Unix, absolute paths don't have a drive letter prefix, so return the original path.
 */
const char *platform_path_skip_absolute_prefix(const char *path) {
  /* Unix: No drive letter to skip */
  return path;
}

/**
 * @brief Normalize path separators for the current platform (POSIX)
 *
 * On Unix, forward slashes are already the standard, so this is a no-op.
 */
void platform_normalize_path_separators(char *path) {
  /* Unix: No-op - already uses forward slashes */
  (void)path;
}

/**
 * @brief Platform-aware path string comparison (POSIX)
 *
 * On Unix, paths are case-sensitive, so use standard strncmp.
 */
int platform_path_strcasecmp(const char *a, const char *b, size_t n) {
  /* Unix: Case-sensitive comparison */
  return strncmp(a, b, n);
}

#endif
