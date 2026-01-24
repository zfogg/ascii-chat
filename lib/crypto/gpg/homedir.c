/**
 * @file crypto/gpg/homedir.c
 * @ingroup crypto
 * @brief Temporary GPG homedir management implementation
 */

#include "homedir.h"
#include "../../common.h"
#include "../../log/logging.h"
#include "../../platform/util.h"
#include "../../platform/filesystem.h"
#include <string.h>
#include <stdlib.h>

/**
 * @struct gpg_homedir
 * @brief Internal structure for temporary GPG homedir
 */
struct gpg_homedir {
  char path[PLATFORM_MAX_PATH_LENGTH];
};

gpg_homedir_t *gpg_homedir_create(void) {
  gpg_homedir_t *homedir = SAFE_MALLOC(sizeof(gpg_homedir_t), gpg_homedir_t *);
  if (!homedir) {
    log_error("Failed to allocate memory for GPG homedir handle");
    return NULL;
  }

  /* Create temporary directory using platform abstraction */
  if (platform_mkdtemp(homedir->path, sizeof(homedir->path), "ascii-chat-gpg") != 0) {
    log_error("Failed to create temporary GPG homedir");
    SAFE_FREE(homedir);
    return NULL;
  }

  /* Restrict permissions to owner only (mode 0700) using platform abstraction */
  if (platform_chmod(homedir->path, 0700) != 0) {
    log_warn("Failed to set permissions on GPG homedir, attempting cleanup");
    platform_rmdir_recursive(homedir->path);
    SAFE_FREE(homedir);
    return NULL;
  }

  log_debug("Created temporary GPG homedir: %s", homedir->path);
  return homedir;
}

const char *gpg_homedir_path(const gpg_homedir_t *homedir) {
  if (!homedir) {
    return NULL;
  }
  return homedir->path;
}

void gpg_homedir_destroy(gpg_homedir_t *homedir) {
  if (!homedir) {
    return;
  }

  /* Recursively delete the entire directory and all contents using platform abstraction */
  if (platform_rmdir_recursive(homedir->path) != 0) {
    log_warn("Failed to completely clean up GPG homedir: %s", homedir->path);
  } else {
    log_debug("Cleaned up temporary GPG homedir: %s", homedir->path);
  }

  SAFE_FREE(homedir);
}
