/**
 * @file session/stdin_reader.c
 * @ingroup session
 * @brief Read ANSI ASCII frames from stdin, chunked by height
 */

#include <ascii-chat/session/stdin_reader.h>
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/log/logging.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct stdin_frame_reader_s {
  int frame_height;
  int frame_width;
  char *line_buffer;        // Temporary buffer for one line
  size_t line_buffer_size;
  bool eof_reached;
} stdin_frame_reader_t;

#define LINE_BUFFER_SIZE 16384  // Max bytes per line

asciichat_error_t stdin_frame_reader_create(int frame_height, int frame_width,
                                            stdin_frame_reader_t **out) {
  if (frame_height <= 0 || frame_width <= 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM,
                     "Invalid frame dimensions: height=%d width=%d",
                     frame_height, frame_width);
  }

  stdin_frame_reader_t *reader = SAFE_CALLOC(1, sizeof(*reader), stdin_frame_reader_t *);
  reader->frame_height = frame_height;
  reader->frame_width = frame_width;
  reader->line_buffer = SAFE_MALLOC(LINE_BUFFER_SIZE, char *);
  reader->line_buffer_size = LINE_BUFFER_SIZE;
  reader->eof_reached = false;

  log_debug("stdin_reader: created with frame size %dx%d", frame_width, frame_height);
  *out = reader;
  return ASCIICHAT_OK;
}

asciichat_error_t stdin_frame_reader_next(stdin_frame_reader_t *reader, char **out_frame) {
  if (!reader || !out_frame) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters to stdin_frame_reader_next");
  }

  if (reader->eof_reached) {
    *out_frame = NULL;
    return ASCIICHAT_OK;  // EOF, but no error
  }

  // Allocate buffer for complete frame (frame_height lines)
  // Estimate: 16KB per line should be enough for ANSI + ASCII
  size_t frame_capacity = reader->frame_height * LINE_BUFFER_SIZE;
  char *frame_data = SAFE_MALLOC(frame_capacity, char *);
  size_t frame_pos = 0;

  // Read exactly frame_height lines from stdin
  for (int line_num = 0; line_num < reader->frame_height; line_num++) {
    // Read one line from stdin
    char *line = fgets(reader->line_buffer, (int)reader->line_buffer_size, stdin);

    if (!line) {
      // EOF reached
      reader->eof_reached = true;

      // If we read some lines but not a complete frame, that's ok - return partial
      if (frame_pos > 0) {
        log_debug("stdin_reader: EOF reached after %d/%d lines, returning partial frame",
                  line_num, reader->frame_height);
        // Remove trailing newline from last line if present
        if (frame_pos > 0 && frame_data[frame_pos - 1] == '\n') {
          frame_pos--;
        }
        *out_frame = frame_data;
        return ASCIICHAT_OK;
      }

      SAFE_FREE(frame_data);
      *out_frame = NULL;
      return ASCIICHAT_OK;  // EOF
    }

    // Append line to frame
    size_t line_len = strlen(line);
    if (frame_pos + line_len >= frame_capacity) {
      log_warn("stdin_reader: frame buffer overflow, line %d too large", line_num);
      // Realloc and continue
      frame_capacity *= 2;
      char *new_frame = SAFE_MALLOC(frame_capacity, char *);
      memcpy(new_frame, frame_data, frame_pos);
      SAFE_FREE(frame_data);
      frame_data = new_frame;
    }

    memcpy(frame_data + frame_pos, line, line_len);
    frame_pos += line_len;
  }

  // Remove trailing newline from last line (we'll add it back in display)
  if (frame_pos > 0 && frame_data[frame_pos - 1] == '\n') {
    frame_data[frame_pos - 1] = '\0';
    frame_pos--;
  }

  *out_frame = frame_data;
  log_debug_every(NS_PER_SEC_INT, "stdin_reader: read frame (%zu bytes)", frame_pos);
  return ASCIICHAT_OK;
}

void stdin_frame_reader_destroy(stdin_frame_reader_t *reader) {
  if (!reader) return;
  SAFE_FREE(reader->line_buffer);
  SAFE_FREE(reader);
}
