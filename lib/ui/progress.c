#include <ascii-chat/ui/progress.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/util/string.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
void ui_progress_format(char *out, size_t cap, double fraction, int width) {
  if (!out || !cap || width < 1 || !isfinite(fraction)) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid progress buffer, value or width");
    return;
  }
  out[0] = '\0';
  if (fraction < 0)
    fraction = 0;
  if (fraction > 1)
    fraction = 1;
  int filled = (int)(fraction * width);
  size_t used = 0;
  for (int i = 0; i < width; ++i) {
    const char *glyph = terminal_should_use_utf8() ? (i < filled ? "█" : "░") : (i < filled ? "#" : ".");
    const char *cell = colored_string(i < filled ? LOG_COLOR_INFO : LOG_COLOR_GREY, glyph);
    size_t n = strlen(cell);
    if (n >= cap - used) {
      SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Progress output buffer is too small");
      return;
    }
    memcpy(out + used, cell, n);
    used += n;
    out[used] = '\0';
  }
}
const char *ui_spinner_glyph(uint64_t elapsed_ns, bool use_colors) {
  static const char *unicode[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧"};
  static const char *ascii[] = {"|", "/", "-", "\\"};
  uint64_t step = elapsed_ns / 125000000;
  const char *glyph = terminal_should_use_utf8() ? unicode[step % 8] : ascii[step % 4];
  return use_colors ? colored_string(LOG_COLOR_WARN, glyph) : glyph;
}
void ui_spinner_render(frame_buffer_t *out, int row, int col, uint64_t elapsed_ns) {
  if (!out || row < 1 || col < 1) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid spinner output or position");
    return;
  }
  static const int x[] = {3, 5, 6, 5, 3, 1, 0, 1}, y[] = {0, 0, 1, 2, 2, 2, 1, 0};
  unsigned phase = (unsigned)((elapsed_ns / 100000000) % 8);
  for (unsigned dot = 0; dot < 8; ++dot) {
    bool lit = (phase + 8 - dot) % 8 < 3;
    frame_buffer_printf(out, "\033[%d;%dH%s", row + y[dot], col + x[dot],
                        colored_string(lit ? LOG_COLOR_WARN : LOG_COLOR_GREY, terminal_should_use_utf8() ? "●"
                                                                              : lit                      ? "o"
                                                                                                         : "."));
  }
}
