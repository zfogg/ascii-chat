#include <ascii-chat/platform/clipboard.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/common.h>
#include <string.h>

asciichat_error_t platform_clipboard_read(char *output, size_t capacity) {
  if (!output || capacity < 2)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid clipboard buffer");
  const char *wayland_files[] = {"wl-paste", "--no-newline", "--type", "text/uri-list", NULL};
  const char *wayland_text[] = {"wl-paste", "--no-newline", NULL};
  const char *x11_files[] = {"xclip", "-selection", "clipboard", "-out", "-target", "text/uri-list", NULL};
  const char *x11_text[] = {"xclip", "-selection", "clipboard", "-out", NULL};
  const char **commands[] = {wayland_files, wayland_text, x11_files, x11_text};
  asciichat_errno_scope_t scope = errno_scope_begin();
  for (size_t i = 0; i < 4; ++i) {
    output[0] = '\0';
    if (platform_execute_subprocess(commands[i][0], commands[i], output, capacity) == 0 && output[0]) {
      errno_scope_end(scope, ASCIICHAT_ERRNO_DISMISSED);
      return ASCIICHAT_OK;
    }
  }
  errno_scope_end(scope, ASCIICHAT_ERRNO_DISMISSED);
  return SET_ERRNO(ERROR_GENERAL, "Cannot read clipboard; install wl-clipboard or xclip, or paste into Open media");
}
