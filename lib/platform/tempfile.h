#pragma once

/**
 * @file platform/tempfile.h
 * @brief Cross-platform temporary file creation and cleanup
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * Provides platform-independent temporary file creation that handles:
 * - Windows: GetTempPath/GetTempFileName with process ID safety
 * - Unix: mkstemp with process ID safety
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <stddef.h>

/**
 * Create a temporary file with a given prefix.
 *
 * Windows: Creates file in temp dir via GetTempFileName with process-specific prefix
 * Unix: Creates file via mkstemp with prefix in /tmp
 *
 * @param path_out Buffer to store the created temp file path (must be at least 256 bytes)
 * @param path_size Size of path_out buffer
 * @param prefix Prefix for the temp file name (e.g., "asc_sig")
 * @param fd Output parameter: on Unix, receives the open file descriptor; on Windows, -1
 * @return 0 on success, -1 on error
 *
 * Note: Caller must close fd on Unix and delete the file on both platforms when done
 */
int platform_create_temp_file(char *path_out, size_t path_size, const char *prefix, int *fd);

/**
 * Delete a temporary file.
 *
 * @param path Path to the file to delete
 * @return 0 on success, -1 on error
 */
int platform_delete_temp_file(const char *path);

/** @} */
