/**
 * @file session/stdin_reader.h
 * @ingroup session
 * @brief Read ANSI ASCII frames from stdin and chunk them by height
 *
 * When stdin is piped and --render-file is specified, read ASCII frames
 * from stdin instead of capturing from webcam/media. Frames are chunked
 * by height (number of lines) as specified via --height.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <ascii-chat/asciichat_errno.h>

typedef struct stdin_frame_reader_s stdin_frame_reader_t;

/**
 * @brief Create a stdin frame reader
 *
 * @param frame_height Number of lines per frame
 * @param frame_width Expected characters per line (for validation)
 * @param out Pointer to store reader context
 * @return ASCIICHAT_OK on success
 */
asciichat_error_t stdin_frame_reader_create(int frame_height, int frame_width,
                                            stdin_frame_reader_t **out);

/**
 * @brief Read next frame from stdin
 *
 * Reads `frame_height` lines from stdin and returns them as a single frame string.
 * Returns NULL in out_frame when EOF is reached.
 *
 * @param reader Reader context
 * @param out_frame Pointer to store frame data (NULL when EOF, must free with SAFE_FREE)
 * @return ASCIICHAT_OK on success (including EOF)
 */
asciichat_error_t stdin_frame_reader_next(stdin_frame_reader_t *reader, char **out_frame);

/**
 * @brief Destroy stdin frame reader
 *
 * @param reader Reader context to destroy
 */
void stdin_frame_reader_destroy(stdin_frame_reader_t *reader);
