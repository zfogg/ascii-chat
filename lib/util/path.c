/**
 * @file util/path.c
 * @ingroup util
 * @brief 📂 Cross-platform path manipulation with normalization and Windows/Unix separator handling
 */

#include "path.h"
#include "common.h"
#include "common/error_codes.h"
#include "platform/system.h"
#include "platform/path.h"
#include "platform/filesystem.h"
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

/* Normalize a path by resolving .. and . components
 * Handles both Windows (\) and Unix (/) separators
 * Returns a pointer to a static buffer (not thread-safe, but sufficient for __FILE__ normalization)
 */
static const char *normalize_path(const char *path) {
  if (!path) {
    SET_ERRNO(ERROR_INVALID_PARAM, "path is null");
    return "unknown";
  }

  static char normalized[PLATFORM_MAX_PATH_LENGTH];
  static char components[PLATFORM_MAX_PATH_LENGTH][256];
  int component_count = 0;
  size_t path_len = strlen(path);

  if (path_len >= PLATFORM_MAX_PATH_LENGTH) {
    return path; /* Can't normalize, return as-is */
  }

  const char *pos = path;
  bool absolute = path_is_absolute(path);

  /* Skip past the absolute path prefix if present */
  if (absolute) {
#ifdef _WIN32
    if (path_len >= 3 && isalpha((unsigned char)path[0]) && path[1] == ':' && path[2] == PATH_DELIM) {
      pos += 3; /* Skip the drive letter and colon and separator (e.g., "C:\") */
    }
#else
    if (path_len >= 1 && path[0] == PATH_DELIM) {
      pos += 1; /* Skip the root separator */
    }
#endif
  }

  /* Parse path into components */
  while (*pos) {
    /* Skip leading separators (handle both / and \ on all platforms) */
    while (*pos == '/' || *pos == '\\') {
      pos++;
    }

    if (!*pos)
      break;

    const char *component_start = pos;
    while (*pos && *pos != '/' && *pos != '\\') {
      pos++;
    }

    size_t component_len = (size_t)(pos - component_start);
    if (component_len == 0)
      continue;

    if (component_len >= sizeof(components[0])) {
      component_len = sizeof(components[0]) - 1;
    }

    /* Check for . and .. components */
    if (component_len == 1 && component_start[0] == PATH_COMPONENT_DOT) {
      /* Skip . component */
      continue;
    }
    if (component_len == 2 && component_start[0] == PATH_COMPONENT_DOT && component_start[1] == PATH_COMPONENT_DOT) {
      /* Handle .. component - go up one level */
      if (component_count > 0) {
        component_count--;
        continue;
      }
      if (!absolute) {
        /* For relative paths, keep .. at the start */
        memcpy(components[component_count], component_start, component_len);
        components[component_count][component_len] = '\0';
        component_count++;
      }
      continue;
    }
    /* Normal component */
    memcpy(components[component_count], component_start, component_len);
    components[component_count][component_len] = '\0';
    component_count++;
  }

  /* Build normalized path */
  size_t out_pos = 0;
#ifdef _WIN32
  if (absolute && path_len >= 3 && isalpha((unsigned char)path[0]) && path[1] == ':') {
    normalized[out_pos++] = path[0];
    normalized[out_pos++] = ':';
    normalized[out_pos++] = PATH_DELIM;
  }
#else
  if (absolute) {
    normalized[out_pos++] = PATH_DELIM;
  }
#endif

  for (int i = 0; i < component_count; i++) {
    if (i > 0) {
      normalized[out_pos++] = PATH_DELIM;
    }
    size_t comp_len = strlen(components[i]);
    if (out_pos + comp_len >= PLATFORM_MAX_PATH_LENGTH) {
      break;
    }
    memcpy(normalized + out_pos, components[i], comp_len);
    out_pos += comp_len;
  }

  normalized[out_pos] = '\0';
  return normalized;
}

const char *extract_project_relative_path(const char *file) {
  if (!file) {
    SET_ERRNO(ERROR_INVALID_PARAM, "file is null");
    return "unknown";
  }

  /* First normalize the path to resolve .. and . components */
  const char *normalized = normalize_path(file);

  /* Extract relative path by looking for common project directories */
  /* Look for lib/, src/, tests/, include/ in the path - make it relative from there */
  /* This avoids embedding absolute paths in the binary */
  /* We need to find the LAST occurrence to avoid matching parent directories */
  /* For example: C:\Users\user\src\ascii-chat\src\client\crypto.c */
  /* We want to match the LAST src\, not the first one */
  const char *dirs[] = {"lib/", "src/", "tests/", "include/", "lib\\", "src\\", "tests\\", "include\\"};
  const char *best_match = NULL;
  size_t best_match_pos = 0;

  for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
    const char *dir = dirs[i];
    const char *search_start = normalized;
    const char *last_found = NULL;

    /* Find the last occurrence of this directory */
    const char *found;
    while ((found = strstr(search_start, dir)) != NULL) {
      last_found = found;
      search_start = found + 1; /* Move past this match to find next one */
    }

    if (last_found) {
      size_t pos = (size_t)(last_found - normalized);
      /* Use the match that's closest to the end (most specific project directory) */
      /* Higher position = further into the path = more specific */
      if (best_match == NULL || pos > best_match_pos) {
        best_match = last_found;
        best_match_pos = pos;
      }
    }
  }

  if (best_match) {
    /* Found a project directory - return everything from here */
    return best_match;
  }

  /* If no common project directory found, try to find just the filename */
  const char *last_sep = strrchr(normalized, PATH_DELIM);

  if (last_sep) {
    return last_sep + 1;
  }

  /* Last resort: return the normalized path */
  return normalized;
}

char *expand_path(const char *path) {
  if (path[0] == PATH_TILDE) {
    const char *home = platform_get_home_dir();
    if (!home) {
      return NULL;
    }

    char *expanded;
    size_t total_len = strlen(home) + strlen(path) + 1; // path includes the tilde
    expanded = SAFE_MALLOC(total_len, char *);
    if (!expanded) {
      return NULL;
    }
    safe_snprintf(expanded, total_len, "%s%s", home, path + 1);

    platform_normalize_path_separators(expanded);

    return expanded;
  }
  return platform_strdup(path);
}

char *get_config_dir(void) {
  /* Delegate to platform abstraction layer */
  return platform_get_config_dir();
}

char *get_log_dir(void) {
#ifdef NDEBUG
  // Release builds: Use $TMPDIR/ascii-chat/
  // Get system temp directory
  char temp_dir[PLATFORM_MAX_PATH_LENGTH];
  if (!platform_get_temp_dir(temp_dir, sizeof(temp_dir))) {
    // Fallback: Use current working directory if temp dir unavailable
    char cwd_buf[PLATFORM_MAX_PATH_LENGTH];
    if (!platform_get_cwd(cwd_buf, sizeof(cwd_buf))) {
      return NULL;
    }
    char *result = SAFE_MALLOC(strlen(cwd_buf) + 1, char *);
    if (!result) {
      return NULL;
    }
    safe_snprintf(result, strlen(cwd_buf) + 1, "%s", cwd_buf);
    return result;
  }

  // Build path to ascii-chat subdirectory
  size_t log_dir_len = strlen(temp_dir) + strlen(PATH_SEPARATOR_STR) + strlen("ascii-chat") + 1;
  char *log_dir = SAFE_MALLOC(log_dir_len, char *);
  if (!log_dir) {
    return NULL;
  }
  safe_snprintf(log_dir, log_dir_len, "%s%sascii-chat", temp_dir, PATH_SEPARATOR_STR);

  // Create the directory if it doesn't exist (with owner-only permissions)
  asciichat_error_t mkdir_result = platform_mkdir(log_dir, DIR_PERM_PRIVATE);
  if (mkdir_result != ASCIICHAT_OK) {
    // Directory creation failed - fall back to temp_dir without subdirectory
    SAFE_FREE(log_dir);
    char *result = SAFE_MALLOC(strlen(temp_dir) + 1, char *);
    if (!result) {
      return NULL;
    }
    safe_snprintf(result, strlen(temp_dir) + 1, "%s", temp_dir);
    return result;
  }

  // Verify the directory is writable
  if (platform_access(log_dir, PLATFORM_ACCESS_WRITE) != 0) {
    // Directory not writable - fall back to temp_dir
    SAFE_FREE(log_dir);
    char *result = SAFE_MALLOC(strlen(temp_dir) + 1, char *);
    if (!result) {
      return NULL;
    }
    safe_snprintf(result, strlen(temp_dir) + 1, "%s", temp_dir);
    return result;
  }

  return log_dir;
#else
  // Debug builds: Use current working directory
  char cwd_buf[PLATFORM_MAX_PATH_LENGTH];
  if (!platform_get_cwd(cwd_buf, sizeof(cwd_buf))) {
    return NULL;
  }

  char *result = SAFE_MALLOC(strlen(cwd_buf) + 1, char *);

  safe_snprintf(result, strlen(cwd_buf) + 1, "%s", cwd_buf);
  return result;
#endif
}

bool path_normalize_copy(const char *path, char *out, size_t out_len) {
  if (!path || !out || out_len == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "null path or out or out_len is 0");
    return false;
  }

  const char *normalized = normalize_path(path);
  if (!normalized) {
    return false;
  }

  size_t len = strlen(normalized);
  if (len + 1 > out_len) {
    return false;
  }

  memcpy(out, normalized, len + 1);
  return true;
}

bool path_is_absolute(const char *path) {
  if (!path || !*path) {
    return false;
  }

#ifdef _WIN32
  if ((path[0] == '\\' && path[1] == '\\')) {
    return true; // UNC path
  }
  if (isalpha((unsigned char)path[0]) && path[1] == PATH_DRIVE_SEPARATOR && path[2] == PATH_DELIM) {
    return true;
  }
  return false;
#else
  return path[0] == PATH_DELIM;
#endif
}

bool path_is_within_base(const char *path, const char *base) {
  if (!path || !base) {
    return false;
  }

  if (!path_is_absolute(path) || !path_is_absolute(base)) {
    return false;
  }

  char normalized_path[PLATFORM_MAX_PATH_LENGTH];
  char normalized_base[PLATFORM_MAX_PATH_LENGTH];

  if (!path_normalize_copy(path, normalized_path, sizeof(normalized_path))) {
    return false;
  }
  if (!path_normalize_copy(base, normalized_base, sizeof(normalized_base))) {
    return false;
  }

  size_t base_len = strlen(normalized_base);
  if (base_len == 0) {
    return false;
  }

  if (platform_path_strcasecmp(normalized_path, normalized_base, base_len) != 0) {
    return false;
  }
  char next = normalized_path[base_len];
  if (next == '\0') {
    return true;
  }
  return next == PATH_DELIM;
}

bool path_is_within_any_base(const char *path, const char *const *bases, size_t base_count) {
  if (!path || !bases || base_count == 0) {
    return false;
  }

  for (size_t i = 0; i < base_count; ++i) {
    const char *base = bases[i];
    if (!base) {
      continue;
    }
    if (path_is_within_base(path, base)) {
      return true;
    }
  }

  return false;
}

bool path_looks_like_path(const char *value) {
  if (!value || *value == '\0') {
    return false;
  }

  if (value[0] == PATH_DELIM || value[0] == PATH_COMPONENT_DOT || value[0] == PATH_TILDE) {
    return true;
  }

  if (strchr(value, PATH_DELIM)) {
    return true;
  }

#ifdef _WIN32
  if (isalpha((unsigned char)value[0]) && value[1] == ':' && value[2] == PATH_DELIM) {
    return true;
  }
#endif

  return false;
}

static asciichat_error_t map_role_to_error(path_role_t role) {
  switch (role) {
  case PATH_ROLE_CONFIG_FILE:
    return ERROR_CONFIG;
  case PATH_ROLE_LOG_FILE:
    return ERROR_LOGGING_INIT;
  case PATH_ROLE_KEY_PRIVATE:
  case PATH_ROLE_KEY_PUBLIC:
  case PATH_ROLE_CLIENT_KEYS:
    return ERROR_CRYPTO_KEY;
  }
  return ERROR_GENERAL;
}

static void append_base_if_valid(const char *candidate, const char **bases, size_t *count) {
  if (!candidate || *candidate == '\0' || *count >= MAX_PATH_BASES) {
    return;
  }
  if (!path_is_absolute(candidate)) {
    return;
  }
  bases[*count] = candidate;
  (*count)++;
}

static void build_ascii_chat_path(const char *base, const char *suffix, char *out, size_t out_len) {
  if (!base || !suffix || out_len == 0) {
    out[0] = '\0';
    return;
  }

  size_t base_len = strlen(base);
  bool needs_sep = base_len > 0 && base[base_len - 1] != PATH_DELIM;

  safe_snprintf(out, out_len, "%s%s%s", base, needs_sep ? PATH_SEPARATOR_STR : "", suffix);
}

/**
 * Check if a path points to a sensitive system file that should not be overwritten.
 * This prevents accidental or malicious overwriting of critical OS files.
 *
 * @param path The normalized absolute path to check
 * @return true if the path is a sensitive system file
 */
static bool is_sensitive_system_path(const char *path) {
  if (!path) {
    return false;
  }

#ifdef _WIN32
  // Windows system directories
  const char *sensitive_paths[] = {"C:\\Windows",                   // System directory
                                   "C:\\Program Files",             // Program files
                                   "C:\\Program Files (x86)",       // 32-bit programs
                                   "C:\\ProgramData",               // All users data
                                   "C:\\System Volume Information", // System recovery
                                   "C:\\PerfLogs",                  // Performance logs
                                   NULL};
#else
  // Unix/Linux/macOS system directories
  const char *sensitive_paths[] = {"/etc",       // System configuration
                                   "/bin",       // Essential binaries
                                   "/sbin",      // System binaries
                                   "/usr/bin",   // User binaries
                                   "/usr/sbin",  // User system binaries
                                   "/usr/lib",   // System libraries
                                   "/lib",       // Libraries
                                   "/lib64",     // 64-bit libraries
                                   "/boot",      // Boot files
                                   "/sys",       // System interface
                                   "/proc",      // Process interface
                                   "/dev",       // Devices
                                   "/root",      // Root home (should not write to)
                                   "/var/lib",   // Variable library data
                                   "/var/cache", // Cache data
                                   "/var/spool", // Spool data
                                   NULL};

#ifdef __APPLE__
  // macOS-specific system paths
  const char *macos_paths[] = {"/System",       // Core system
                               "/Library",      // System library
                               "/Applications", // Bundled apps
                               "/Developer",    // Developer tools
                               "/Volumes",      // Mounted volumes
                               NULL};
#endif
#endif

  // Check each sensitive path
  for (int i = 0; sensitive_paths[i] != NULL; i++) {
    const char *base = sensitive_paths[i];
    size_t base_len = strlen(base);

    // Match if path equals base or starts with base + separator
    if (strcmp(path, base) == 0) {
      return true; // Exact match is sensitive
    }
    if (strncmp(path, base, base_len) == 0) {
      // Make sure it's followed by a path separator, not a partial match
      if (path[base_len] == PATH_DELIM || path[base_len] == '/' || path[base_len] == '\\') {
        return true;
      }
    }
  }

#ifdef __APPLE__
  // Check macOS paths
  for (int i = 0; macos_paths[i] != NULL; i++) {
    const char *base = macos_paths[i];
    size_t base_len = strlen(base);

    if (strcmp(path, base) == 0) {
      return true;
    }
    if (strncmp(path, base, base_len) == 0) {
      if (path[base_len] == PATH_DELIM || path[base_len] == '/' || path[base_len] == '\\') {
        return true;
      }
    }
  }
#endif

  return false;
}

/**
 * Check if an existing file is an ascii-chat log file by reading its header.
 * ascii-chat logs have a distinctive format with timestamps and log levels.
 *
 * @param path The path to the file to check
 * @return true if the file appears to be an ascii-chat log file (false for directories or non-files)
 */
static bool is_existing_ascii_chat_log(const char *path) {
  if (!path) {
    return false;
  }

  // Check if path is a regular file (not a directory) using platform abstraction
  if (!platform_is_regular_file(path)) {
    return false; // Not a regular file (could be directory, doesn't exist, symlink, etc.)
  }

  // Try to open and read the first line
  FILE *f = fopen(path, "r");
  if (!f) {
    return false; // Can't read file
  }

  char buffer[256];
  bool is_ascii_chat_log = false;

  // Read first line and check for ascii-chat log signature
  if (fgets(buffer, sizeof(buffer), f) != NULL) {
    // ascii-chat logs start with timestamps like: [HH:MM:SS.microseconds] [LEVEL]
    // Pattern: [digit][digit]:[digit][digit]:[digit][digit].[digits]
    if (buffer[0] == '[' && isdigit((unsigned char)buffer[1]) && isdigit((unsigned char)buffer[2]) &&
        buffer[3] == ':') {
      is_ascii_chat_log = true;
    }
  }

  fclose(f);
  return is_ascii_chat_log;
}

asciichat_error_t path_validate_user_path(const char *input, path_role_t role, char **normalized_out) {
  if (!normalized_out) {
    return SET_ERRNO(map_role_to_error(role), "path_validate_user_path requires output pointer");
  }
  *normalized_out = NULL;

  if (!input || *input == '\0') {
    return SET_ERRNO(map_role_to_error(role), "Path is empty for role %d", role);
  }

  // SECURITY: For log files, if input is a simple filename (no separators or ..), constrain it to a safe directory
  if (role == PATH_ROLE_LOG_FILE) {
    // Check if input contains path separators or parent directory references
    bool is_simple_filename = true;
    for (const char *p = input; *p; p++) {
      if (*p == PATH_DELIM || *p == '/' || *p == '\\') {
        is_simple_filename = false;
        break;
      }
    }
    // Also reject ".." components (even without separators like "..something")
    if (strstr(input, "..") != NULL) {
      is_simple_filename = false;
    }

    // If it's a simple filename, resolve it to a safe base directory
    if (is_simple_filename) {
      // Always prefer current working directory for simple log filenames
      // This ensures logs go to where the user is running the command from
      char safe_base[PLATFORM_MAX_PATH_LENGTH];

      if (!platform_get_cwd(safe_base, sizeof(safe_base))) {
        // If cwd fails, try config dir as fallback
        char *config_dir = get_config_dir();
        if (config_dir) {
          SAFE_STRNCPY(safe_base, config_dir, sizeof(safe_base));
          SAFE_FREE(config_dir);
        } else {
          return SET_ERRNO(ERROR_LOGGING_INIT, "Failed to determine safe directory for log file");
        }
      }

      // Build the full path: safe_base + separator + input
      size_t base_len = strlen(safe_base);
      size_t input_len = strlen(input);
      bool needs_sep = base_len > 0 && safe_base[base_len - 1] != PATH_DELIM;
      size_t total_len = base_len + (needs_sep ? 1 : 0) + input_len + 1;

      if (total_len > PLATFORM_MAX_PATH_LENGTH) {
        return SET_ERRNO(ERROR_LOGGING_INIT, "Log file path too long: %s/%s", safe_base, input);
      }

      char resolved_buf[PLATFORM_MAX_PATH_LENGTH];
      safe_snprintf(resolved_buf, sizeof(resolved_buf), "%s%s%s", safe_base, needs_sep ? PATH_SEPARATOR_STR : "",
                    input);

      // Normalize the resolved path
      char normalized_buf[PLATFORM_MAX_PATH_LENGTH];
      if (!path_normalize_copy(resolved_buf, normalized_buf, sizeof(normalized_buf))) {
        return SET_ERRNO(ERROR_LOGGING_INIT, "Failed to normalize log file path: %s", resolved_buf);
      }

      // Allocate and return the result
      char *result = SAFE_MALLOC(strlen(normalized_buf) + 1, char *);
      if (!result) {
        return SET_ERRNO(ERROR_MEMORY, "Failed to allocate normalized path");
      }
      safe_snprintf(result, strlen(normalized_buf) + 1, "%s", normalized_buf);
      *normalized_out = result;
      return ASCIICHAT_OK;
    }
    // If not a simple filename (contains separators), continue with normal validation below
  }

  // For non-log-files or log files with path separators, validate as usual
  if (role != PATH_ROLE_LOG_FILE && !path_looks_like_path(input)) {
    return SET_ERRNO(map_role_to_error(role), "Value does not look like a filesystem path: %s", input);
  }

  char *expanded = expand_path(input);
  if (!expanded) {
    return SET_ERRNO(map_role_to_error(role), "Failed to expand path: %s", input);
  }

  char candidate_buf[PLATFORM_MAX_PATH_LENGTH];
  const char *candidate_path = expanded;

  if (!path_is_absolute(candidate_path)) {
    char cwd_buf[PLATFORM_MAX_PATH_LENGTH];
    if (!platform_get_cwd(cwd_buf, sizeof(cwd_buf))) {
      SAFE_FREE(expanded);
      return SET_ERRNO(map_role_to_error(role), "Failed to determine current working directory");
    }

    size_t total_len = strlen(cwd_buf) + 1 + strlen(candidate_path) + 1;
    if (total_len >= sizeof(candidate_buf)) {
      SAFE_FREE(expanded);
      return SET_ERRNO(map_role_to_error(role), "Resolved path is too long: %s/%s", cwd_buf, candidate_path);
    }
    if (strlen(candidate_path) > 0 && candidate_path[0] == PATH_DELIM) {
      safe_snprintf(candidate_buf, sizeof(candidate_buf), "%s%s", cwd_buf, candidate_path);
    } else {
      safe_snprintf(candidate_buf, sizeof(candidate_buf), "%s%c%s", cwd_buf, PATH_DELIM, candidate_path);
    }
    candidate_path = candidate_buf;
  }

  char normalized_buf[PLATFORM_MAX_PATH_LENGTH];
  if (!path_normalize_copy(candidate_path, normalized_buf, sizeof(normalized_buf))) {
    SAFE_FREE(expanded);
    return SET_ERRNO(map_role_to_error(role), "Failed to normalize path: %s", candidate_path);
  }

  if (!path_is_absolute(normalized_buf)) {
    SAFE_FREE(expanded);
    return SET_ERRNO(map_role_to_error(role), "Normalized path is not absolute: %s", normalized_buf);
  }

  const char *bases[MAX_PATH_BASES] = {0};
  size_t base_count = 0;

  // Always add current working directory as an allowed base
  // This is critical for log files and other paths relative to where the user runs the command
  char cwd_base[PLATFORM_MAX_PATH_LENGTH];
  if (platform_get_cwd(cwd_base, sizeof(cwd_base))) {
    append_base_if_valid(cwd_base, bases, &base_count);
  }

  char temp_base[PLATFORM_MAX_PATH_LENGTH];
  if (platform_get_temp_dir(temp_base, sizeof(temp_base))) {
    append_base_if_valid(temp_base, bases, &base_count);
  }

  char *config_dir = get_config_dir();
  if (config_dir) {
    append_base_if_valid(config_dir, bases, &base_count);
  }

  const char *home_env = platform_get_home_dir();
  if (home_env) {
    append_base_if_valid(home_env, bases, &base_count);
  }

  char ascii_chat_home[PLATFORM_MAX_PATH_LENGTH];
  if (home_env) {
    build_ascii_chat_path(home_env, ".ascii-chat", ascii_chat_home, sizeof(ascii_chat_home));
    append_base_if_valid(ascii_chat_home, bases, &base_count);
  }

#ifndef _WIN32
  char ascii_chat_home_tmp[PLATFORM_MAX_PATH_LENGTH];
  build_ascii_chat_path("/tmp", ".ascii-chat", ascii_chat_home_tmp, sizeof(ascii_chat_home_tmp));
  append_base_if_valid(ascii_chat_home_tmp, bases, &base_count);
#endif

  char ssh_home[PLATFORM_MAX_PATH_LENGTH];
  if (home_env) {
    build_ascii_chat_path(home_env, ".ssh", ssh_home, sizeof(ssh_home));
    append_base_if_valid(ssh_home, bases, &base_count);
  }

#ifdef _WIN32
  char program_data_logs[PLATFORM_MAX_PATH_LENGTH];
  const char *program_data = platform_getenv("PROGRAMDATA");
  if (program_data) {
    build_ascii_chat_path(program_data, "ascii-chat", program_data_logs, sizeof(program_data_logs));
    append_base_if_valid(program_data_logs, bases, &base_count);
  }
#else
  // System-wide config directories (for server deployments)
  append_base_if_valid("/etc/ascii-chat", bases, &base_count);
  append_base_if_valid("/usr/local/etc/ascii-chat", bases, &base_count);
  append_base_if_valid("/var/log", bases, &base_count);
  append_base_if_valid("/var/tmp", bases, &base_count);
  append_base_if_valid("/tmp", bases, &base_count);
#ifdef __APPLE__
  // On macOS, /tmp is a symlink to /private/tmp
  append_base_if_valid("/private/tmp", bases, &base_count);
  // On macOS, all user home directories are under /Users
  append_base_if_valid("/Users", bases, &base_count);
#endif
#endif

  // Security check: Reject paths that point to sensitive system files
  // This applies to all path roles, not just logs
  if (is_sensitive_system_path(normalized_buf)) {
    SAFE_FREE(expanded);
    if (config_dir) {
      SAFE_FREE(config_dir);
    }
    return SET_ERRNO(map_role_to_error(role), "Cannot write to protected system path: %s", normalized_buf);
  }

  // For log files, apply special validation rules
  if (role == PATH_ROLE_LOG_FILE) {
    // Check if file already exists
    FILE *existing_file = fopen(normalized_buf, "r");
    bool file_exists = (existing_file != NULL);
    if (existing_file) {
      fclose(existing_file);
    }

    // If file exists, it MUST be an ascii-chat log to be overwritten
    if (file_exists && !is_existing_ascii_chat_log(normalized_buf)) {
      SAFE_FREE(expanded);
      if (config_dir) {
        SAFE_FREE(config_dir);
      }
      return SET_ERRNO(ERROR_LOGGING_INIT,
                       "Cannot overwrite existing non-ascii-chat file: %s\n"
                       "For safety, ascii-chat will only overwrite its own log files",
                       normalized_buf);
    }

    // If file doesn't exist, check that path is in safe locations
    if (!file_exists) {
      bool allowed = base_count == 0 ? true : path_is_within_any_base(normalized_buf, bases, base_count);
      if (!allowed) {
        SAFE_FREE(expanded);
        if (config_dir) {
          SAFE_FREE(config_dir);
        }
        return SET_ERRNO(ERROR_LOGGING_INIT,
                         "Log path %s is outside allowed directories (use -L /tmp/file.log, ~/file.log, or "
                         "relative/absolute paths in safe locations)",
                         normalized_buf);
      }
    }
  } else {
    // For non-log-file paths, apply standard whitelist validation
    bool allowed = base_count == 0 ? true : path_is_within_any_base(normalized_buf, bases, base_count);
    if (!allowed) {
      SAFE_FREE(expanded);
      if (config_dir) {
        SAFE_FREE(config_dir);
      }
      return SET_ERRNO(map_role_to_error(role), "Path %s is outside allowed directories", normalized_buf);
    }
  }

  char *result = SAFE_MALLOC(strlen(normalized_buf) + 1, char *);
  if (!result) {
    SAFE_FREE(expanded);
    if (config_dir) {
      SAFE_FREE(config_dir);
    }
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate normalized path");
  }
  safe_snprintf(result, strlen(normalized_buf) + 1, "%s", normalized_buf);
  *normalized_out = result;

  SAFE_FREE(expanded);
  if (config_dir) {
    SAFE_FREE(config_dir);
  }
  return ASCIICHAT_OK;
}
