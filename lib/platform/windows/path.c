/**
 * @file platform/windows/path.c
 * @brief Windows path manipulation implementation
 */

#include "../path.h"
#include "../../common.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

void platform_normalize_path_separators(char *path) {
  /* Windows: Convert forward slashes to backslashes */
  for (char *p = path; *p; p++) {
    if (*p == '/') {
      *p = '\\';
    }
  }
}

int platform_path_strcasecmp(const char *a, const char *b, size_t n) {
  /* Windows: Case-insensitive comparison */
  return _strnicmp(a, b, n);
}

const char *platform_get_home_dir(void) {
  /* Windows: Try USERPROFILE first, fallback to HOME */
  const char *home = platform_getenv("USERPROFILE");
  if (!home) {
    home = platform_getenv("HOME");
  }
  return home;
}

char *platform_get_config_dir(void) {
  /* Windows: Use %APPDATA%/ascii-chat/ */
  const char *appdata = platform_getenv("APPDATA");
  if (appdata && appdata[0] != '\0') {
    size_t len = strlen(appdata) + strlen("\\ascii-chat\\") + 1;
    char *dir = SAFE_MALLOC(len, char *);
    if (!dir) {
      return NULL;
    }
    safe_snprintf(dir, len, "%s\\ascii-chat\\", appdata);
    return dir;
  }

  /* Fallback to %USERPROFILE%/.ascii-chat/ */
  const char *userprofile = platform_getenv("USERPROFILE");
  if (userprofile && userprofile[0] != '\0') {
    size_t len = strlen(userprofile) + strlen("\\.ascii-chat\\") + 1;
    char *dir = SAFE_MALLOC(len, char *);
    if (!dir) {
      return NULL;
    }
    safe_snprintf(dir, len, "%s\\.ascii-chat\\", userprofile);
    return dir;
  }

  return NULL;
}
