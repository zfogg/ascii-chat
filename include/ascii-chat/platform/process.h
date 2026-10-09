#pragma once

/**
 * @file platform/process.h
 * @brief Cross-platform process types and execution utilities
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * Provides platform-independent process types and execution functions for running
 * external programs (like ssh-keygen, gpg) and capturing their output.
 *
 * Windows does not provide pid_t natively, so we typedef it here.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#include <stdio.h>
#include "api.h"
#include "../asciichat_errno.h"

// ============================================================================
// Process ID Type (pid_t)
// ============================================================================

#ifdef _WIN32
/**
 * @brief Process ID type (Windows)
 *
 * Windows _getpid() returns int, so we typedef pid_t to int for compatibility
 * with POSIX code that uses pid_t.
 */
#ifndef _PID_T_DEFINED
typedef int pid_t;
#define _PID_T_DEFINED
#endif
#else
/* POSIX platforms - pid_t is defined in <sys/types.h> */
#include <sys/types.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the current process ID
 * @return Process ID of the calling process
 *
 * Platform-specific implementations:
 *   - POSIX: Uses getpid()
 *   - Windows: Uses _getpid()
 *
 * @ingroup platform
 */
pid_t platform_get_pid(void);

/**
 * @brief Reserve process-title storage before option parsing or environment use.
 * @param argc Original main argument count.
 * @param argv Original main argument vector, replaced with a preserved copy on Unix.
 * @note Call once on the main thread, after allocation tracking is initialized
 * but before starting workers. Keep the returned argv alive until destroy.
 * Windows and WASM require no storage. Failure leaves argv unchanged.
 */
ASCIICHAT_API asciichat_error_t platform_process_title_init(int argc, char ***argv);

/**
 * @brief Update the OS process naming surfaces through one platform API.
 * @note Main-thread only. Never writes to stdout. WASM is a no-op.
 * Unix titles exceeding the reserved storage fail without changing the title.
 * Linux updates argv and the main thread's comm (15 visible bytes). macOS
 * updates argv, the main-thread name, and the Launch Services display name
 * used by Activity Monitor. Windows updates the console title.
 * Does not rename the executable file or Windows Task Manager image name.
 * A native-name failure returns an error after the Unix argv title is updated;
 * the command-line update is retained, including when macOS Launch Services
 * is unavailable. The caller may treat this as a nonfatal cosmetic failure.
 */
ASCIICHAT_API asciichat_error_t platform_process_title_set(const char *title);

/**
 * @brief Set "ascii-chat: <mode> mode - <arguments>" with secret values redacted.
 * @param mode Canonical mode name.
 * @param argc Original argument count.
 * @param argv Preserved application arguments (not modified).
 * @param mode_arg_index Explicit mode token to omit, or -1 for implicit mode.
 * @note Password, TLS password, TURN credential/secret values and inline private
 * key blocks are redacted. Arguments are quoted/escaped for display, not execution.
 * Only supplied CLI arguments are shown, never environment/config values.
 * Sanitized output is truncated to available storage; raw arguments are never
 * used as a fallback. No separator is added if there are no remaining arguments.
 * Also sets a compact main-thread/comm name: ascii:server, ascii:client,
 * ascii:mirror, ascii:discovery, or ascii:acds for discovery-service.
 */
ASCIICHAT_API asciichat_error_t platform_process_title_set_args(const char *mode, int argc, char **argv,
                                                              int mode_arg_index);

/**
 * @brief Restore Unix argument storage and release title allocations.
 * @note Call after workers stop, before the final memory report. Idempotent.
 * The copied argv becomes invalid; g_argv is restored automatically.
 */
ASCIICHAT_API void platform_process_title_destroy(void);

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
asciichat_error_t platform_popen(const char *name, const char *command, const char *mode, FILE **out_stream);

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

/**
 * @name Platform-Specific popen/pclose Macros
 * @{
 *
 * Convenience helpers: Use PLATFORM_POPEN for simple cases where you don't need logging integration.
 * For more control, call platform_popen/platform_pclose directly with a descriptive name.
 *
 * @ingroup platform
 */

/** @} */

// ============================================================================
// Process Spawning
// ============================================================================

/**
 * @brief Opaque process handle
 *
 * Platform-specific process representation:
 *   - Windows: PROCESS_INFORMATION-based handle
 *   - POSIX: PID and status tracking
 *
 * @ingroup platform
 */
typedef struct platform_process platform_process_t;

/**
 * @brief Spawn a child process
 *
 * Creates and starts a new process with the specified command and arguments.
 *
 * Platform-specific behavior:
 *   - Windows: Uses CreateProcessA with PROCESS_INFORMATION
 *   - POSIX: Uses fork() and exec()
 *
 * @param process_out Output parameter: process handle (caller must free with platform_process_destroy)
 * @param path Path to executable (can be relative or absolute)
 * @param argv Argument array, NULL-terminated (argv[0] should be program name)
 * @param stdin_fd File descriptor for stdin, or -1 to use parent's stdin
 * @param stdout_fd File descriptor for stdout, or -1 to use parent's stdout
 * @param stderr_fd File descriptor for stderr, or -1 to use parent's stderr
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note argv must be NULL-terminated
 * @note File descriptors are duplicated, not closed
 *
 * @ingroup platform
 */
asciichat_error_t platform_process_spawn(platform_process_t **process_out, const char *path, const char *const *argv,
                                         int stdin_fd, int stdout_fd, int stderr_fd);

/**
 * @brief Wait for process to terminate with timeout
 *
 * Waits for a spawned process to complete execution.
 *
 * @param process Process handle from platform_process_spawn
 * @param timeout_ms Timeout in milliseconds, or -1 for infinite wait
 * @param exit_code_out Optional output: process exit code
 * @return ASCIICHAT_OK if process exited, ERROR_TIMEOUT if timeout
 *
 * @note On Windows, exit code is from GetExitCodeProcess
 * @note On POSIX, exit code is from waitpid
 *
 * @ingroup platform
 */
asciichat_error_t platform_process_wait(platform_process_t *process, int timeout_ms, int *exit_code_out);

/**
 * @brief Check if process is still running
 *
 * @param process Process handle
 * @return true if process is still running, false if terminated
 *
 * @note Non-blocking check
 *
 * @ingroup platform
 */
bool platform_process_is_alive(platform_process_t *process);

/**
 * @brief Terminate a process
 *
 * Forcefully terminates a running process.
 *
 * @param process Process handle
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note Does not wait for termination; use platform_process_wait to wait
 *
 * @ingroup platform
 */
asciichat_error_t platform_process_kill(platform_process_t *process);

/**
 * @brief Free process handle
 *
 * Releases resources associated with a process handle.
 * Must be called for every process created with platform_process_spawn.
 *
 * @param process Process handle to free
 *
 * @note Safe to call with NULL
 *
 * @ingroup platform
 */
void platform_process_destroy(platform_process_t *process);

#ifdef __cplusplus
}
#endif

/** @} */
