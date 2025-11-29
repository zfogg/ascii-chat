/**
 * @file util/path.c
 * @ingroup util
 * @brief 📂 Cross-platform path manipulation with normalization and Windows/Unix separator handling
 */

#include "path.h"
#include "common.h"
#include "platform/system.h"
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdlib.h>

/* Normalize a path by resolving .. and . components
 * Handles both Windows (\) and Unix (/) separators
 * Returns a pointer to a static buffer (not thread-safe, but sufficient for __FILE__ normalization)
 */
static const char *normalize_path(const char *path) {
  if (!path) {
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
  bool absolute = false;

  /* Check if path is absolute (Windows drive or Unix root) */
#ifdef _WIN32
  if (path_len >= 3 && isalpha((unsigned char)path[0]) && path[1] == ':' && path[2] == PATH_DELIM) {
    absolute = true;
    pos += 3; /* Skip the drive letter and colon and separator (e.g., "C:\") */
  }
#else
  if (path_len >= 1 && path[0] == PATH_DELIM) {
    absolute = true;
  }
#endif

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
  if (!file)
    return "unknown";

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
    const char *home = NULL;
#ifdef _WIN32
    // On Windows, try USERPROFILE first, then HOME as fallback
    if (!(home = platform_getenv("USERPROFILE"))) {
      if (!(home = platform_getenv("HOME"))) {
        return NULL; // Both USERPROFILE and HOME failed
      }
      // HOME found, continue to expansion below
    }
    // USERPROFILE found, continue to expansion below
#else
    if (!(home = platform_getenv("HOME"))) {
      return NULL;
    }
#endif

    char *expanded;
    size_t total_len = strlen(home) + strlen(path) + 1; // path includes the tilde
    expanded = SAFE_MALLOC(total_len, char *);
    if (!expanded) {
      return NULL;
    }
    safe_snprintf(expanded, total_len, "%s%s", home, path + 1);

#ifdef _WIN32
    // Convert Unix forward slashes to Windows backslashes
    for (char *p = expanded; *p; p++) {
      if (*p == '/') {
        *p = '\\';
      }
    }
#endif

    return expanded;
  }
  return platform_strdup(path);
}

char *get_config_dir(void) {
  const char *home = platform_getenv("HOME");

#ifndef _WIN32
  // Use XDG_CONFIG_HOME/ascii-chat
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

  // Fallback to ~/.config/ascii-chat
  if (home && home[0] != '\0') {
    size_t len = strlen(home) + strlen("/.config/ascii-chat/") + 1;
    char *dir = SAFE_MALLOC(len, char *);
    if (!dir) {
      return NULL;
    }
    safe_snprintf(dir, len, "%s/.config/ascii-chat/", home);
    return dir;
  }

  // Final fallback: return NULL if no config directory can be determined
  return NULL;

#elif _WIN32
  const char *appdata = platform_getenv("APPDATA");
  if (appdata && appdata[0] != '\0') {
    size_t len = strlen(appdata) + strlen("\\.ascii-chat\\") + 1;
    char *dir = SAFE_MALLOC(len, char *);
    if (!dir) {
      return NULL;
    }
    safe_snprintf(dir, len, "%s\\.ascii-chat\\", appdata);
    return dir;
  }
  // Fallback to the user's home directory
  size_t len = strlen(home) + strlen("\\.ascii-chat\\") + 1;
  char *dir = SAFE_MALLOC(len, char *);
  if (!dir) {
    return NULL;
  }
  safe_snprintf(dir, len, "%s\\.ascii-chat\\", home);
  return dir;
#else
  size_t len = strlen(home) + strlen("/.ascii-chat/") + 1;
  char *dir = SAFE_MALLOC(len, char *);
  if (!dir) {
    return NULL;
  }
  safe_snprintf(dir, len, "%s/.ascii-chat/", home);
  return dir;
#endif
}

bool path_normalize_copy(const char *path, char *out, size_t out_len) {
  if (!path || !out || out_len == 0) {
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

#ifdef _WIN32
  if (_strnicmp(normalized_path, normalized_base, base_len) != 0) {
#else
  if (strncmp(normalized_path, normalized_base, base_len) != 0) {
#endif
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

asciichat_error_t path_validate_user_path(const char *input, path_role_t role, char **normalized_out) {
  if (!normalized_out) {
    return SET_ERRNO(map_role_to_error(role), "path_validate_user_path requires output pointer");
  }
  *normalized_out = NULL;

  if (!input || *input == '\0') {
    return SET_ERRNO(map_role_to_error(role), "Path is empty for role %d", role);
  }

  // For log files, allow simple filenames (e.g., "trace.log") without path separators
  // They will be treated as relative to the current directory
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

  const char *home_env = platform_getenv("HOME");
#ifdef _WIN32
  if (!home_env) {
    home_env = platform_getenv("USERPROFILE");
  }
#endif
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
  append_base_if_valid("/var/log", bases, &base_count);
  append_base_if_valid("/var/tmp", bases, &base_count);
#endif

  // For log files, skip the "allowed directories" check - allow any path
  bool allowed;
  if (role == PATH_ROLE_LOG_FILE) {
    allowed = true;
  } else {
    allowed = base_count == 0 ? true : path_is_within_any_base(normalized_buf, bases, base_count);
  }

  if (!allowed) {
    SAFE_FREE(expanded);
    if (config_dir) {
      SAFE_FREE(config_dir);
    }
    return SET_ERRNO(map_role_to_error(role), "Path %s is outside allowed directories", normalized_buf);
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
