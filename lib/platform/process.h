#pragma once

/**
 * @file platform/process.h
 * @brief Cross-platform process execution utilities
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * Provides platform-independent process execution functions for running
 * external programs (like ssh-keygen, gpg) and capturing their output.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#include <stdio.h>
#include "../asciichat_errno.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Execute a command and return a file stream for reading/writing
 *
 * Opens a process for communication, similar to POSIX popen().
 * Creates a unidirectional pipe to read from or write to the process.
 *
 * Platform-specific implementations:
 *   - POSIX: Uses popen()
 *   - Windows: Uses _popen()
 *
 * @param command Command line to execute (e.g., "ssh-keygen -l -f file.pub")
 * @param mode File stream mode: "r" for reading, "w" for writing
 * @param out_stream Pointer to receive the FILE* stream
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note The returned stream must be closed with platform_pclose().
 * @note On Windows, the mode parameters are the same as POSIX popen().
 *
 * @par Example:
 * @code{.c}
 * FILE *stream;
 * if (platform_popen("ssh-keygen -l -f key.pub", "r", &stream) == ASCIICHAT_OK) {
 *     char line[256];
 *     fgets(line, sizeof(line), stream);
 *     platform_pclose(&stream);
 * }
 * @endcode
 *
 * @ingroup platform
 */
asciichat_error_t platform_popen(const char *command, const char *mode, FILE **out_stream);

/**
 * @brief Close a process stream opened with platform_popen()
 *
 * Closes the stream and waits for the process to terminate.
 * Returns the process exit status.
 *
 * Platform-specific implementations:
 *   - POSIX: Uses pclose() and waits for process
 *   - Windows: Uses _pclose() and waits for process
 *
 * @param stream_ptr Pointer to FILE* stream to close
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note stream_ptr must be a pointer to a FILE* stream obtained from platform_popen().
 * @note The FILE* pointer is set to NULL after closing.
 * @note Always sets errno context on failure for debugging.
 *
 * @ingroup platform
 */
asciichat_error_t platform_pclose(FILE **stream_ptr);

#ifdef __cplusplus
}
#endif

/** @} */
