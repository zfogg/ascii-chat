/**
 * @file terminal/fd/reader.c
 * @ingroup terminal_fd
 * @brief Read ANSI ASCII frames from file descriptor, chunked by height
 */

#include <ascii-chat/terminal/fd/reader.h>
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/util/display.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct terminal_fd_reader_s {
  FILE *fp;          // File pointer created from FD
  int frame_height;
  int frame_width;   // Detected from first frame (0 = not yet detected)
  char *line_buffer; // Temporary buffer for one line
  size_t line_buffer_size;
  bool eof_reached;
  bool first_frame_processed; // Track if we've processed first frame yet
} terminal_fd_reader_t;

#define LINE_BUFFER_SIZE 16384 // Max bytes per line

asciichat_error_t terminal_fd_reader_create(int fd, int frame_height, terminal_fd_reader_t **out) {
  if (fd < 0 || frame_height <= 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid fd or frame height: fd=%d, height=%d", fd, frame_height);
  }

  FILE *fp = fdopen(fd, "r");
  if (!fp) {
    return SET_ERRNO_SYS(ERROR_INVALID_PARAM, "fdopen failed for FD %d", fd);
  }

  terminal_fd_reader_t *reader = SAFE_CALLOC(1, sizeof(*reader), terminal_fd_reader_t *);
  reader->fp = fp;
  reader->frame_height = frame_height;
  reader->frame_width = 0; // Will be detected from first frame
  reader->line_buffer = SAFE_MALLOC(LINE_BUFFER_SIZE, char *);
  reader->line_buffer_size = LINE_BUFFER_SIZE;
  reader->eof_reached = false;
  reader->first_frame_processed = false;

  log_debug("terminal_fd_reader: created with fd=%d, frame height %d (width auto-detect)", fd, frame_height);
  *out = reader;
  return ASCIICHAT_OK;
}

asciichat_error_t terminal_fd_reader_next(terminal_fd_reader_t *reader, char **out_frame) {
  if (!reader || !out_frame) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters to terminal_fd_reader_next");
  }

  if (reader->eof_reached) {
    *out_frame = NULL;
    return ASCIICHAT_OK; // EOF, but no error
  }

  // Allocate buffer for complete frame (frame_height lines)
  // Estimate: 16KB per line should be enough for ANSI + ASCII
  size_t frame_capacity = reader->frame_height * LINE_BUFFER_SIZE;
  char *frame_data = SAFE_MALLOC(frame_capacity, char *);
  size_t frame_pos = 0;

  // Read exactly frame_height lines from FD
  for (int line_num = 0; line_num < reader->frame_height; line_num++) {
    // Read one line from FD
    char *line = fgets(reader->line_buffer, (int)reader->line_buffer_size, reader->fp);

    if (!line) {
      // EOF reached
      reader->eof_reached = true;

      // If we read some lines but not a complete frame, that's ok - return partial
      if (frame_pos > 0) {
        log_debug("terminal_fd_reader: EOF reached after %d/%d lines, returning partial frame", line_num,
                  reader->frame_height);
        // Remove trailing newline from last line if present
        if (frame_pos > 0 && frame_data[frame_pos - 1] == '\n') {
          frame_pos--;
        }
        *out_frame = frame_data;
        return ASCIICHAT_OK;
      }

      SAFE_FREE(frame_data);
      *out_frame = NULL;
      return ASCIICHAT_OK; // EOF
    }

    // Append line to frame
    size_t line_len = strlen(line);
    if (frame_pos + line_len >= frame_capacity) {
      log_warn("terminal_fd_reader: frame buffer overflow, line %d too large", line_num);
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

  // Detect frame width from first frame (count visible chars in first line)
  if (!reader->first_frame_processed && frame_pos > 0) {
    reader->first_frame_processed = true;

    // Find first line by looking for newline
    char *first_newline = strchr(frame_data, '\n');
    if (first_newline) {
      // Null-terminate first line temporarily for width calculation
      char saved_char = *first_newline;
      *first_newline = '\0';
      reader->frame_width = display_width(frame_data);
      *first_newline = saved_char; // Restore
    } else {
      // Only one line in frame, use it for width
      reader->frame_width = display_width(frame_data);
    }

    log_info("terminal_fd_reader: detected frame width %d from first frame", reader->frame_width);
  }

  *out_frame = frame_data;
  log_debug_every(NS_PER_SEC_INT, "terminal_fd_reader: read frame (%zu bytes, %dx%d)", frame_pos, reader->frame_width,
                  reader->frame_height);
  return ASCIICHAT_OK;
}

void terminal_fd_reader_destroy(terminal_fd_reader_t *reader) {
  if (!reader)
    return;
  if (reader->fp) {
    fclose(reader->fp);
    reader->fp = NULL;
  }
  SAFE_FREE(reader->line_buffer);
  SAFE_FREE(reader);
}
