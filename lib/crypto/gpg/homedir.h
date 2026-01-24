/**
 * @file crypto/gpg/homedir.h
 * @ingroup crypto
 * @brief Temporary GPG homedir management for isolated key operations
 *
 * Provides utilities for creating and managing temporary GPG homedirs
 * to isolate key operations (import, decrypt, sign) without polluting
 * the user's main GPG keyring.
 *
 * Using a temporary homedir provides:
 * - Isolation from user's keys (no risk of deleting wrong keys)
 * - Automatic cleanup (just delete the directory)
 * - Better error handling and race condition avoidance
 * - Cleaner, more maintainable code
 *
 * Example usage:
 * @code
 * gpg_homedir_t homedir = gpg_homedir_create();
 * if (homedir == NULL) {
 *   return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to create GPG homedir");
 * }
 *
 * // Use homedir.path in gpg commands
 * char cmd[1024];
 * snprintf(cmd, sizeof(cmd), "gpg --homedir '%s' --batch --import '%s'",
 *          homedir->path, key_file);
 *
 * int status = system(cmd);
 * gpg_homedir_destroy(homedir);
 * @endcode
 */

#pragma once

#include "asciichat_errno.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct gpg_homedir_t
 * @brief Opaque handle to a temporary GPG homedir
 */
typedef struct gpg_homedir gpg_homedir_t;

/**
 * @brief Create a temporary GPG homedir for isolated key operations
 *
 * Creates a new temporary directory configured for GPG use.
 * The directory is isolated from the user's main GPG keyring.
 *
 * @return Pointer to homedir handle on success, NULL on failure
 *         Caller must destroy with gpg_homedir_destroy()
 *
 * @note The directory is created in the system temp location (TMPDIR, /tmp, etc.)
 */
gpg_homedir_t *gpg_homedir_create(void);

/**
 * @brief Get the homedir path for use in GPG commands
 *
 * Returns the filesystem path of the temporary homedir.
 * This path should be used with gpg's --homedir flag.
 *
 * @param homedir Handle from gpg_homedir_create()
 * @return Pointer to path string (valid until gpg_homedir_destroy() is called)
 *         Returns NULL if homedir is invalid
 */
const char *gpg_homedir_path(const gpg_homedir_t *homedir);

/**
 * @brief Destroy a temporary GPG homedir and clean up all files
 *
 * Recursively deletes the temporary directory and all its contents.
 * Safe to call on NULL pointer (no-op).
 *
 * @param homedir Handle from gpg_homedir_create() (can be NULL)
 */
void gpg_homedir_destroy(gpg_homedir_t *homedir);

#ifdef __cplusplus
}
#endif
