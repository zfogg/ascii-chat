#pragma once

/**
 * @file platform/pipe.h
 * @ingroup platform
 * @brief Cross-platform pipe/agent socket interface for ascii-chat
 *
 * This header provides a unified interface for agent communication (SSH agent,
 * GPG agent) that abstracts platform-specific implementations:
 * - Windows: Named pipes (HANDLE with ReadFile/WriteFile)
 * - POSIX: Unix domain sockets (file descriptor with read/write)
 *
 * The interface provides:
 * - Pipe/agent connection management
 * - Binary I/O operations (read/write)
 * - Connection lifecycle management
 *
 * @note On Windows, uses named pipes (CreateFileA, ReadFile, WriteFile).
 *       On POSIX systems, uses Unix domain sockets (socket, connect, read, write).
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h> // For ssize_t
#include "../common.h"

#ifdef _WIN32
#include <windows.h>
/** @brief Pipe handle type (Windows: HANDLE) */
typedef HANDLE pipe_t;
/** @brief Invalid pipe value (Windows: INVALID_HANDLE_VALUE) */
#define INVALID_PIPE_VALUE INVALID_HANDLE_VALUE
#else
#include <sys/types.h>
/** @brief Pipe handle type (POSIX: int file descriptor) */
typedef int pipe_t;
/** @brief Invalid pipe value (POSIX: -1) */
#define INVALID_PIPE_VALUE (-1)
#endif

// ============================================================================
// Pipe/Agent Connection Functions
// ============================================================================

/**
 * @brief Connect to an agent via named pipe (Windows) or Unix socket (POSIX)
 * @param path Path to agent (named pipe path on Windows, socket path on POSIX)
 * @return Pipe handle on success, INVALID_PIPE_VALUE on error
 *
 * Connects to an agent using the appropriate platform-specific mechanism:
 * - Windows: Opens named pipe via CreateFileA
 * - POSIX: Connects to Unix domain socket via socket() + connect()
 *
 * @note The path format differs by platform:
 *       - Windows: Named pipe path (e.g., "\\\\.\\pipe\\openssh-ssh-agent")
 *       - POSIX: Unix socket path (e.g., "/tmp/ssh-XXXXXX/agent.XXXXXX")
 *
 * @ingroup platform
 */
pipe_t platform_pipe_connect(const char *path);

/**
 * @brief Close a pipe connection
 * @param pipe Pipe handle to close
 * @return 0 on success, non-zero on error
 *
 * Closes the pipe connection using the appropriate platform-specific function:
 * - Windows: CloseHandle()
 * - POSIX: close()
 *
 * @ingroup platform
 */
int platform_pipe_close(pipe_t pipe);

/**
 * @brief Read data from a pipe
 * @param pipe Pipe handle to read from
 * @param buf Buffer to store read data
 * @param len Maximum number of bytes to read
 * @return Number of bytes read on success, -1 on error, 0 on connection closed
 *
 * Reads data from the pipe using the appropriate platform-specific function:
 * - Windows: ReadFile()
 * - POSIX: read()
 *
 * @note This function may read fewer bytes than requested (short read).
 *       Caller should handle partial reads if needed.
 *
 * @ingroup platform
 */
ssize_t platform_pipe_read(pipe_t pipe, void *buf, size_t len);

/**
 * @brief Write data to a pipe
 * @param pipe Pipe handle to write to
 * @param buf Data buffer to write
 * @param len Number of bytes to write
 * @return Number of bytes written on success, -1 on error
 *
 * Writes data to the pipe using the appropriate platform-specific function:
 * - Windows: WriteFile()
 * - POSIX: write()
 *
 * @note This function may write fewer bytes than requested (short write).
 *       Caller should handle partial writes if needed.
 *
 * @ingroup platform
 */
ssize_t platform_pipe_write(pipe_t pipe, const void *buf, size_t len);

/**
 * @brief Check if a pipe handle is valid
 * @param pipe Pipe handle to check
 * @return true if pipe is valid, false otherwise
 *
 * @ingroup platform
 */
bool platform_pipe_is_valid(pipe_t pipe);
