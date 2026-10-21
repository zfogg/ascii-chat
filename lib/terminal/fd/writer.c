/**
 * @file terminal/fd/writer.c
 * @ingroup terminal_fd
 * @brief Write ANSI ASCII frames to file descriptor
 */

#include <ascii-chat/terminal/fd/writer.h>
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/log/log.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/util/stdout_writer.h>

typedef struct terminal_fd_writer_s {
  FILE *fp; // File pointer created from FD
} terminal_fd_writer_t;

asciichat_error_t terminal_fd_writer_create(int fd, terminal_fd_writer_t **out) {
  if (fd < 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid fd: %d", fd);
  }

  FILE *fp = platform_fdopen("terminal_fd_writer", fd, "w");
  if (!fp) {
    return SET_ERRNO_SYS(ERROR_INVALID_PARAM, "fdopen failed for FD %d", fd);
  }

  terminal_fd_writer_t *writer = SAFE_CALLOC(1, sizeof(*writer), terminal_fd_writer_t *);
  writer->fp = fp;

  log_debug("created with fd=%d", fd);
  *out = writer;
  return ASCIICHAT_OK;
}

asciichat_error_t terminal_fd_writer_write(terminal_fd_writer_t *writer, const char *frame) {
  if (!writer) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid writer context");
  }

  if (!frame) {
    return ASCIICHAT_OK; // No-op for NULL frame
  }

  // Write frame and newline
  asciichat_error_t err = stdout_writer_write(writer->fp, frame, strlen(frame));
  if (err != ASCIICHAT_OK)
    return err;
  err = stdout_writer_write(writer->fp, "\n", 1);
  if (err != ASCIICHAT_OK)
    return err;

  log_debug_every(NS_PER_SEC_INT, "wrote frame (%zu bytes)", strlen(frame));
  return ASCIICHAT_OK;
}

void terminal_fd_writer_destroy(terminal_fd_writer_t *writer) {
  if (!writer)
    return;
  if (writer->fp) {
    fclose(writer->fp);
    writer->fp = NULL;
  }
  SAFE_FREE(writer);
}
