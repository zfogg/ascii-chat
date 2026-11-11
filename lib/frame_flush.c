#include <ascii-chat/platform/frame_flush.h>
#include <ascii-chat/platform/terminal.h>
#ifdef PLATFORM_IOS
#include <ascii-chat/platform/ios/render_bridge.h>
#endif

asciichat_error_t platform_frame_flush(int fd) {
#ifdef PLATFORM_IOS
  (void)fd;
  ios_render_bridge_flush();
  return ASCIICHAT_OK;
#else
  return terminal_flush(fd);
#endif
}
