/**
 * @file log/io.c
 * @brief IO capture implementation
 */

#include <ascii-chat/log/io.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/platform/abstraction.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define LOG_IO_BUFFER_SIZE 8192

log_io_t log_io_start(void) {
  log_io_t capture = {.saved_stdout_fd = -1, .saved_stderr_fd = -1, .pipe_fd = -1};

  // Save original stdout and stderr
  capture.saved_stdout_fd = dup(STDOUT_FILENO);
  capture.saved_stderr_fd = dup(STDERR_FILENO);

  if (capture.saved_stdout_fd < 0 || capture.saved_stderr_fd < 0) {
    if (capture.saved_stdout_fd >= 0)
      close(capture.saved_stdout_fd);
    if (capture.saved_stderr_fd >= 0)
      close(capture.saved_stderr_fd);
    return (log_io_t){.saved_stdout_fd = -1, .saved_stderr_fd = -1, .pipe_fd = -1};
  }

  // Create a pipe for capturing output
  int pipefd[2];
  if (pipe(pipefd) < 0) {
    close(capture.saved_stdout_fd);
    close(capture.saved_stderr_fd);
    return (log_io_t){.saved_stdout_fd = -1, .saved_stderr_fd = -1, .pipe_fd = -1};
  }

  // pipefd[0] is read end, pipefd[1] is write end
  int write_fd = pipefd[1];
  capture.pipe_fd = pipefd[0];

  // Make the read end non-blocking so we can read without hanging
  int flags = fcntl(capture.pipe_fd, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(capture.pipe_fd, F_SETFL, flags | O_NONBLOCK);
  }

  // Redirect both stdout and stderr to the pipe
  if (dup2(write_fd, STDOUT_FILENO) < 0 || dup2(write_fd, STDERR_FILENO) < 0) {
    close(write_fd);
    close(capture.pipe_fd);
    close(capture.saved_stdout_fd);
    close(capture.saved_stderr_fd);
    return (log_io_t){.saved_stdout_fd = -1, .saved_stderr_fd = -1, .pipe_fd = -1};
  }

  // Close the write end in the parent process (we only need it in redirected streams)
  close(write_fd);

  return capture;
}

void log_io_stop(log_io_t capture, const char *prefix) {
  if (capture.saved_stdout_fd < 0 || capture.saved_stderr_fd < 0) {
    return;  // Capture was not started successfully
  }

  // Flush stdout/stderr BEFORE restoring to ensure all output is in the pipe
  fflush(stdout);
  fflush(stderr);

  // Restore stdout and stderr
  dup2(capture.saved_stdout_fd, STDOUT_FILENO);
  dup2(capture.saved_stderr_fd, STDERR_FILENO);
  close(capture.saved_stdout_fd);
  close(capture.saved_stderr_fd);

  // Read and log the captured output
  if (capture.pipe_fd >= 0) {
    char buffer[LOG_IO_BUFFER_SIZE];
    ssize_t bytes_read;

    // Read all available data from the pipe
    while ((bytes_read = read(capture.pipe_fd, buffer, sizeof(buffer) - 1)) > 0) {
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
            log_info("[%s] %s", prefix, line_start);
          } else {
            log_info("%s", line_start);
          }
        }

        // Move to the next line
        line_start = line_end + 1;
      }

      // Log any remaining partial line
      if (line_start[0] != '\0') {
        if (prefix) {
          log_info("[%s] %s", prefix, line_start);
        } else {
          log_info("%s", line_start);
        }
      }
    }

    close(capture.pipe_fd);
  }
}
