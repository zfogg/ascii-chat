/**
 * @file platform/posix/path.c
 * @brief POSIX path manipulation implementation (Linux, macOS, BSD)
 */

#include "../path.h"
#include "../../common.h"
#include <string.h>
#include <stdlib.h>

void platform_normalize_path_separators(char *path) {
  /* No-op on POSIX - already uses forward slashes */
  (void)path;
}

int platform_path_strcasecmp(const char *a, const char *b, size_t n) {
  /* Case-sensitive on Unix */
  return strncmp(a, b, n);
}

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

  /* Fallback: ~/.ascii-chat/ */
  const char *home = platform_getenv("HOME");
  if (home && home[0] != '\0') {
    size_t len = strlen(home) + strlen("/.ascii-chat/") + 1;
    char *dir = SAFE_MALLOC(len, char *);
    if (!dir) {
      return NULL;
    }
    safe_snprintf(dir, len, "%s/.ascii-chat/", home);
    return dir;
  }

  return NULL;
}
