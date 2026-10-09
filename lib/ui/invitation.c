#include <ascii-chat/ui/invitation.h>
#include <ascii-chat/video/rgba/image.h>
#include <ascii-chat/discovery/strings.h>
#include <stdio.h>
#include <string.h>

static const rgb_pixel_t g_rainbow_colors[] = {
    {255, 0, 0},   // Red
    {255, 165, 0}, // Orange
    {255, 255, 0}, // Yellow
    {0, 255, 0},   // Green
    {0, 255, 255}, // Cyan
    {0, 0, 255},   // Blue
    {255, 0, 255}  // Magenta
};
#define RAINBOW_COLOR_COUNT 7

static rgb_pixel_t interpolate_color(rgb_pixel_t color1, rgb_pixel_t color2, double t) {
  rgb_pixel_t result;
  result.r = (uint8_t)(color1.r * (1.0 - t) + color2.r * t);
  result.g = (uint8_t)(color1.g * (1.0 - t) + color2.g * t);
  result.b = (uint8_t)(color1.b * (1.0 - t) + color2.b * t);
  return result;
}

/**
 * @brief Get RGB color for a position in the rainbow
 * @param position Position in the rainbow (0.0 to 1.0 or beyond for cycling)
 * @return RGB color at that position
 */
static rgb_pixel_t get_rainbow_color_rgb(double position) {
  // Normalize position to 0-1 range
  double norm_pos = position - (long)position;
  if (norm_pos < 0) {
    norm_pos += 1.0;
  }

  // Scale position to color range
  double color_pos = norm_pos * (RAINBOW_COLOR_COUNT - 1);
  int color_idx = (int)color_pos;
  double blend = color_pos - color_idx;

  // Wrap around at the end
  if (color_idx >= RAINBOW_COLOR_COUNT - 1) {
    color_idx = RAINBOW_COLOR_COUNT - 1;
    blend = 0;
  }

  int next_idx = (color_idx + 1) % RAINBOW_COLOR_COUNT;

  return interpolate_color(g_rainbow_colors[color_idx], g_rainbow_colors[next_idx], blend);
}

static void centered_line(frame_buffer_t *buf, const char *text, int len, int cols, int frame, bool colors) {
  int padding = (cols - len) / 2;
  frame_buffer_printf(buf, "%*s", padding > 0 ? padding : 0, "");
  for (int i = 0; i < len; ++i) {
    if (colors && text[i] != ' ') {
      rgb_pixel_t color = get_rainbow_color_rgb((i + frame / 5.0) / 30.0);
      frame_buffer_printf(buf, "\033[38;2;%u;%u;%um%c\033[0m", color.r, color.g, color.b, text[i]);
    } else {
      frame_buffer_printf(buf, "%c", text[i]);
    }
  }
  frame_buffer_printf(buf, "\n");
}

void invitation_render_logo(frame_buffer_t *buf, int cols, int frame, bool colors, bool compact) {
  const char *ascii_logo[4] = {
      "  __ _ ___  ___(_|_)       ___| |__   __ _| |_ ", " / _` / __|/ __| | |_____ / __| '_ \\ / _` | __| ",
      "| (_| \\__ \\ (__| | |_____| (__| | | | (_| | |_ ", " \\__,_|___/\\___|_|_|      \\___|_| |_|\\__,_|\\__| "};
  if (compact) {
    const char *title = "ascii-chat";
    int len = (int)strlen(title);
    centered_line(buf, title, len < cols ? len : cols, cols, frame, colors);
    return;
  }
  // Pad every row to the same width so the ASCII glyphs stay aligned.
  int width = 0;
  for (int i = 0; i < 4; ++i) {
    int len = (int)strlen(ascii_logo[i]);
    if (len > width)
      width = len;
  }
  for (int i = 0; i < 4; ++i) {
    char line[80];
    snprintf(line, sizeof(line), "%-*s", width, ascii_logo[i]);
    centered_line(buf, line, width < cols ? width : cols, cols, frame, colors);
  }
}

void invitation_render(frame_buffer_t *buf, terminal_size_t size, const char *session, bool joining, int frame,
                       bool colors) {
  if (!buf || size.cols < 2 || size.rows < 2)
    return;
  int cols = size.cols - 1;
  int rows = size.rows - 1;
  bool ready = session && session[0];
  const char *label =
      ready ? (joining ? "Connecting to session:" : "Share this string to connect:") : "Creating session...";
  char command[SESSION_STRING_BUFFER_SIZE + 32];
  snprintf(command, sizeof(command), "Run: ascii-chat %s", ready ? session : "");
  const char *lines[3] = {label, ready ? session : "", ready && !joining ? command : ""};
  int text_rows = 0;
  for (int i = 0; i < 3; ++i) {
    if (lines[i][0])
      text_rows += ((int)strlen(lines[i]) + cols - 1) / cols;
  }
  bool compact = cols < 52 || rows < text_rows + 7;
  int logo_rows = compact ? 1 : 4;
  bool show_logo = rows >= text_rows + logo_rows + 1;
  int height = text_rows + (show_logo ? logo_rows + 1 : 0);
  int top = rows > height ? (rows - height) / 2 : 0;
  int used = 0;
  for (; used < top; ++used)
    frame_buffer_printf(buf, "\n");
  if (show_logo) {
    invitation_render_logo(buf, cols, frame, colors, compact);
    frame_buffer_printf(buf, "\n");
    used += logo_rows + 1;
  }
  // On exceptionally short terminals prioritize the string and its command.
  int first = height > rows && ready ? 1 : 0;
  for (int i = first; i < 3 && used < rows; ++i) {
    const char *text = lines[i];
    size_t remaining = strlen(text);
    while (remaining && used < rows) {
      int length = remaining > (size_t)cols ? cols : (int)remaining;
      centered_line(buf, text, length, cols, frame, false);
      text += length;
      remaining -= length;
      ++used;
    }
  }
  // Erase stale logs/rows after a layout change without clearing between frames.
  frame_buffer_printf(buf, "\033[J");
}
