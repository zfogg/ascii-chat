/**
 * @file terminal/fd/reader.h
 * @ingroup terminal_fd
 * @brief Read ANSI ASCII frames from file descriptor, chunked by height
 *
 * Reads ASCII frames from any file descriptor (stdin, file, pipe) and chunks
 * them by height (number of lines) as specified. Supports reading ANSI-colored
 * ASCII art frames in batches.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <ascii-chat/asciichat_errno.h>

typedef struct terminal_fd_reader_s terminal_fd_reader_t;

/**
 * @brief Create a terminal FD reader from a file descriptor
 *
 * @param fd File descriptor to read from (e.g., STDIN_FILENO, open file)
 * @param frame_height Number of lines per frame (required - used for frame boundaries)
 * @param out Pointer to store reader context
 * @return ASCIICHAT_OK on success
 *
 * Width is auto-detected from the first frame's line length.
 */
asciichat_error_t terminal_fd_reader_create(int fd, int frame_height,
                                            terminal_fd_reader_t **out);

/**
 * @brief Read next frame from file descriptor
 *
 * Reads `frame_height` lines from the FD and returns them as a single frame string.
 * Returns NULL in out_frame when EOF is reached.
 *
 * @param reader Reader context
 * @param out_frame Pointer to store frame data (NULL when EOF, must free with SAFE_FREE)
 * @return ASCIICHAT_OK on success (including EOF)
 */
asciichat_error_t terminal_fd_reader_next(terminal_fd_reader_t *reader, char **out_frame);

/**
 * @brief Destroy terminal FD reader
 *
 * @param reader Reader context to destroy
 */
void terminal_fd_reader_destroy(terminal_fd_reader_t *reader);
