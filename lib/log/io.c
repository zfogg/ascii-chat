/**
 * @file log/io.c
 * @brief IO capture implementation
 */

#include <ascii-chat/log/io.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/platform/abstraction.h>
#include <string.h>

#define LOG_IO_BUFFER_SIZE 8192

log_io_t log_io_start(void) {
  log_io_t capture = {.saved_stdout_fd = -1, .saved_stderr_fd = -1, .output = NULL};

  // A pipe cannot be drained until the wrapped call returns. A temporary file
  // lets backend worker threads finish even when diagnostics exceed pipe capacity.
  capture.output = platform_tmpfile();
  if (!capture.output)
    return capture;

  fflush(stdout);
  fflush(stderr);
  capture.saved_stdout_fd = platform_dup(STDOUT_FILENO);
  capture.saved_stderr_fd = platform_dup(STDERR_FILENO);
  if (capture.saved_stdout_fd < 0 || capture.saved_stderr_fd < 0)
    goto fail;

  int output_fd = fileno(capture.output);
  if (dup2(output_fd, STDOUT_FILENO) < 0)
    goto fail;
  if (dup2(output_fd, STDERR_FILENO) < 0) {
    // Restore stdout if only the first redirection succeeded.
    dup2(capture.saved_stdout_fd, STDOUT_FILENO);
    goto fail;
  }
  return capture;

fail:
  if (capture.saved_stdout_fd >= 0)
    platform_close(capture.saved_stdout_fd);
  if (capture.saved_stderr_fd >= 0)
    platform_close(capture.saved_stderr_fd);
  fclose(capture.output);
  return (log_io_t){.saved_stdout_fd = -1, .saved_stderr_fd = -1, .output = NULL};
}

void log_io_stop(log_io_t capture, const char *prefix) {
  if (capture.saved_stdout_fd < 0 || capture.saved_stderr_fd < 0) {
    return; // Capture was not started successfully
  }

  // Flush before restoring so buffered backend output is captured.
  fflush(stdout);
  fflush(stderr);

  // Restore stdout and stderr
  dup2(capture.saved_stdout_fd, STDOUT_FILENO);
  dup2(capture.saved_stderr_fd, STDERR_FILENO);
  platform_close(capture.saved_stdout_fd);
  platform_close(capture.saved_stderr_fd);

  // Read and log the captured output
  if (capture.output) {
    char buffer[LOG_IO_BUFFER_SIZE];
    size_t bytes_read;

    // Replay only after restoring the descriptors, so logger output cannot feed
    // back into this capture. fseek synchronizes the stream with descriptor writes.
    if (fseek(capture.output, 0, SEEK_SET) != 0) {
      fclose(capture.output);
      return;
    }
    while ((bytes_read = fread(buffer, 1, sizeof(buffer) - 1, capture.output)) > 0) {
      buffer[bytes_read] = '\0';

      // Split by lines and log each line
      char *line_start = buffer;
      char *line_end;

      while ((line_end = strchr(line_start, '\n')) != NULL) {
        // Null-terminate the line (overwriting the newline)
        *line_end = '\0';

        // Log the line if it's not empty
        if (line_start[0] != '\0') {
          if (prefix) {
            log_debug("[%s] %s", prefix, line_start);
          } else {
            log_debug("%s", line_start);
          }
        }

        // Move to the next line
        line_start = line_end + 1;
      }

      // Log any remaining partial line
      if (line_start[0] != '\0') {
        if (prefix) {
          log_debug("[%s] %s", prefix, line_start);
        } else {
          log_debug("%s", line_start);
        }
      }
    }

    fclose(capture.output);
  }
}
