#include <ascii-chat/platform/frame_flush.h>
#include <ascii-chat/platform/terminal.h>

asciichat_error_t platform_frame_flush(int fd) {
  return terminal_flush(fd);
}
