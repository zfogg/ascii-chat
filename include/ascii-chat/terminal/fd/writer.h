/**
 * @file terminal/fd/writer.h
 * @ingroup terminal_fd
 * @brief Write ANSI ASCII frames to file descriptor
 *
 * Writes ASCII frames with ANSI color codes to any file descriptor (stdout,
 * file, pipe). Supports batching frames with proper newline handling.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date March 2026
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <ascii-chat/asciichat_errno.h>

typedef struct terminal_fd_writer_s terminal_fd_writer_t;

/**
 * @brief Create a terminal FD writer to a file descriptor
 *
 * @param fd File descriptor to write to (e.g., STDOUT_FILENO, open file)
 * @param out Pointer to store writer context
 * @return ASCIICHAT_OK on success
 */
asciichat_error_t terminal_fd_writer_create(int fd, terminal_fd_writer_t **out);

/**
 * @brief Write a frame to the file descriptor
 *
 * Writes the frame data followed by a newline. Frame should contain
 * ANSI-colored ASCII art, typically from rendering output.
 *
 * @param writer Writer context
 * @param frame Frame data to write (NULL is no-op)
 * @return ASCIICHAT_OK on success
 */
asciichat_error_t terminal_fd_writer_write(terminal_fd_writer_t *writer, const char *frame);

/**
 * @brief Destroy terminal FD writer
 *
 * @param writer Writer context to destroy
 */
void terminal_fd_writer_destroy(terminal_fd_writer_t *writer);
