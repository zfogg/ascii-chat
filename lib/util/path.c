/**
 * @file util/path.c
 * @ingroup util
 * @brief 📂 Cross-platform path manipulation with normalization and Windows/Unix separator handling
 */

#include "path.h"
#include "common.h"
#include <string.h>
#include <stdbool.h>

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
  if (path_len >= 3 && isalpha((unsigned char)path[0]) && path[1] == ':' && (path[2] == '\\' || path[2] == '/')) {
    absolute = true;
  }
#else
  if (path_len >= 1 && path[0] == '/') {
    absolute = true;
  }
#endif

  /* Parse path into components */
  while (*pos) {
    /* Skip leading separators */
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
    if (component_len == 1 && component_start[0] == '.') {
      /* Skip . component */
      continue;
    }
    if (component_len == 2 && component_start[0] == '.' && component_start[1] == '.') {
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
    normalized[out_pos++] = '\\';
  }
#else
  if (absolute) {
    normalized[out_pos++] = '/';
  }
#endif

  for (int i = 0; i < component_count; i++) {
#ifdef _WIN32
    if (i > 0 || absolute) {
      normalized[out_pos++] = '\\';
    }
#else
    if (i > 0 || absolute) {
      normalized[out_pos++] = '/';
    }
#endif
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
  const char *last_slash = strrchr(normalized, '/');
  const char *last_backslash = strrchr(normalized, '\\');
  const char *last_sep = (last_slash > last_backslash) ? last_slash : last_backslash;

  if (last_sep) {
    return last_sep + 1;
  }

  /* Last resort: return the normalized path */
  return normalized;
}

char *expand_path(const char *path) {
  if (path[0] == '~') {
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
    size_t total_len = strlen(home) + strlen(path) + 1;
    expanded = SAFE_MALLOC(total_len, char *);
    if (!expanded) {
      return NULL;
    }
    safe_snprintf(expanded, total_len, "%s%s", home, path + 1);
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
