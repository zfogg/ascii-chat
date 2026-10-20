#include <ascii-chat/ui/session.h>
#include <ascii-chat/common.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/util/display.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/video/rgba/color_filter.h>
#include <stdio.h>
#include <string.h>
/* Color codes for enabled/disabled settings */
#define ENABLED_COLOR LOG_COLOR_INFO   /* Green */
#define DISABLED_COLOR LOG_COLOR_ERROR /* Red */

/* ============================================================================
 * Keyboard Help Rendering
 * ============================================================================ */

/**
 * @brief Build a volume bar graph string
 *
 * Renders volume as a bar graph: "████████░░ 80%"
 *
 * @param volume Volume value [0.0, 1.0]
 * @param bar_output Output buffer for bar graph (must be at least 20 bytes)
 * @param bar_output_size Size of output buffer
 */
static void format_volume_bar(double volume, char *bar_output, size_t bar_output_size) {
  if (!bar_output || bar_output_size < 25) {
    return;
  }

  // Clamp volume to [0.0, 1.0]
  if (volume < 0.0) {
    volume = 0.0;
  }
  if (volume > 1.0) {
    volume = 1.0;
  }

  // Calculate filled blocks (10 blocks total for 10% granularity)
  int filled = (int)(volume * 10.0);
  int empty = 10 - filled;

  // Build bar: "[======    ] 80%"
  // Use simple ASCII characters to avoid UTF-8 encoding issues
  snprintf(bar_output, bar_output_size, "[%.*s%.*s] %d%%", filled, "==========", empty, "          ",
           (int)(volume * 100.0));
}

/**
 * @brief Get color mode name
 */
static const char *color_mode_to_string(int mode) {
  switch (mode) {
  case -1:
    return "Auto";
  case 0:
    return "Mono";
  case 1:
    return "16-color";
  case 2:
    return "256-color";
  case 3:
    return "Truecolor";
  default:
    return "Unknown";
  }
}

/**
 * @brief Get render mode name
 */
static const char *render_mode_to_string(int mode) {
  switch (mode) {
  case 0:
    return "Foreground";
  case 1:
    return "Background";
  case 2:
    return "Half-block";
  default:
    return "Unknown";
  }
}

/**
 * @brief Get color filter name
 */
static const char *color_filter_to_string(color_filter_t filter) {
  if (filter == COLOR_FILTER_NONE) {
    return "None";
  }
  const color_filter_def_t *def = color_filter_get_metadata(filter);
  return def ? def->name : "Unknown";
}

/**
 * @brief Build a help screen line with UTF-8 width-aware padding and truncation
 *
 * Constructs lines like "║  <content><padding>║" with the specified max width.
 * Properly accounts for multi-byte UTF-8 characters and truncates if needed.
 *
 * @param output Output buffer for the line
 * @param output_size Size of output buffer (must be at least 256 bytes)
 * @param content Content string to display (may contain UTF-8)
 * @param max_width Maximum total line width
 */
static void build_help_line(char *output, size_t output_size, const char *content, int max_width) {
  if (!output || output_size < 256 || !content || max_width < 6) {
    return;
  }

  // Available width: max_width - 3 (left border "║  ") - 1 (right border "║")
  int content_available = max_width - 4;

  // Truncate content if needed (ANSI-aware, with ellipsis indicator)
  char truncated[2048];
  truncate_with_ellipsis(content, truncated, sizeof(truncated), content_available);

  int content_width = display_width(truncated);
  int padding = content_available - content_width;
  if (padding < 0)
    padding = 0;

  // Build the line
  char *pos = output;
  int remaining = output_size;

  // Left border and spacing
  int n = snprintf(pos, remaining, "║  %s", truncated);
  if (n > 0) {
    pos += n;
    remaining -= n;
  }

  // Padding spaces
  for (int i = 0; i < padding && remaining > 1; i++) {
    *pos++ = ' ';
    remaining--;
  }

  // Right border
  if (remaining > 3) {
    snprintf(pos, remaining, "║");
  }
}

static const char *status_indicator(bool enabled) {
  return enabled ? colored_string(ENABLED_COLOR, "O") : colored_string(DISABLED_COLOR, "X");
}

// Only the binding prefix is a key; separators and descriptions keep the foreground.
static void color_help_text(const char *text, size_t key_end, bool use_color, char *output, size_t capacity) {
  size_t used = 0;
  for (size_t i = 0; text[i] && used + 128 < capacity;) {
    if (text[i] == '\033' && text[i + 1] == '[') {
      do {
        output[used++] = text[i++];
      } while (text[i] && !(text[i - 1] >= '@' && text[i - 1] <= '~' && text[i - 1] != '['));
      continue;
    }
    bool number = text[i] >= '0' && text[i] <= '9';
    bool key = i < key_end && text[i] != ' ' && text[i] != '/' && text[i] != '(' && text[i] != ')';
    if (use_color && (number || key)) {
      size_t start = i++;
      if (number) {
        while (text[i] >= '0' && text[i] <= '9')
          ++i;
      } else {
        while (i < key_end && text[i] && text[i] != ' ' && text[i] != '/' && text[i] != ')' &&
               !(text[i] >= '0' && text[i] <= '9'))
          ++i;
      }
      used += snprintf(output + used, capacity - used, "%s%.*s%s",
                       log_level_color(number ? LOG_COLOR_FATAL : LOG_COLOR_WARN), (int)(i - start), text + start,
                       log_level_color(LOG_COLOR_RESET));
    } else {
      output[used++] = text[i++];
    }
  }
  output[used] = '\0';
}

/**
 * @brief Helper to append a help line to the buffer
 */
static void append_help_line(char *buffer, size_t *buf_pos, size_t BUFFER_SIZE, int start_row, int *current_row,
                             int start_col, int box_width, const char *content) {
  if (!buffer || *buf_pos >= BUFFER_SIZE || !content) {
    return;
  }

  char line_buf[2048];
  int remaining = BUFFER_SIZE - *buf_pos;

  int written = snprintf(buffer + *buf_pos, remaining, "\033[%d;%dH", start_row + *current_row, start_col + 1);
  if (written > 0) {
    *buf_pos += written;
  }

  build_help_line(line_buf, sizeof(line_buf), content, box_width);
  written = snprintf(buffer + *buf_pos, BUFFER_SIZE - *buf_pos, "%s", line_buf);
  if (written > 0) {
    *buf_pos += written;
  }

  (*current_row)++;
}

char *ui_session_help_render(terminal_size_t size, bool has_media, bool browser, terminal_size_t *minimum) {
  int term_width = size.cols, term_height = size.rows;
  terminal_color_mode_t mode = GET_OPTION(color_mode);
  bool use_color = mode != TERM_COLOR_NONE && GET_OPTION(color) != COLOR_SETTING_FALSE && !GET_OPTION(strip_ansi) &&
                   (mode != TERM_COLOR_AUTO || detect_terminal_capabilities().color_level != TERM_COLOR_NONE);
  const char *navigation[] = {
      "Navigation & Control:",
      "─────────────────────",
      "?      Toggle this help screen",
      "Esc    Close overlay / Quit app",
      browser ? NULL : "o      Open media path or URL",
      browser ? NULL : "v      Open from clipboard",
      browser ? NULL : "Paste  Queue dropped/pasted media",
      browser ? NULL : "< / >  Previous/next media (restart)",
      has_media ? "Space  Play/Pause (files only)" : NULL,
      has_media ? "[      Toggle Media Playback" : NULL,
      has_media ? "← / →  Seek backward/forward 30s" : NULL,
      "m / M  Mute/Unmute audio",
      "↑ / ↓  Volume up/down (10%)",
      "c / C  Cycle color mode",
      "f / F  Cycle color filter",
      "x / X  Flip webcam horizontally",
      "y / Y  Flip webcam vertically",
      "r / R  Cycle render mode",
      "]      Toggle application logs",
      "-      FPS counter",
      browser ? NULL : "=      Live statistics",
#ifndef NDEBUG
      browser ? NULL : "`      Print current sync primitive state",
#endif
  };
  char settings[20][256] = {"Current Settings:", "─────────────────"};
  char volume_bar[32];
  format_volume_bar(GET_OPTION(speakers_volume), volume_bar, sizeof(volume_bar));
  snprintf(settings[2], sizeof(settings[2]), "Audio  : %s", status_indicator(GET_OPTION(audio_enabled)));
  snprintf(settings[3], sizeof(settings[3]), "Volume : %s", volume_bar);
  snprintf(settings[4], sizeof(settings[4]), "Color  : %s", color_mode_to_string(GET_OPTION(color_mode)));
  snprintf(settings[5], sizeof(settings[5]), "Filter : %s", color_filter_to_string(GET_OPTION(color_filter)));
  snprintf(settings[6], sizeof(settings[6]), "Render : %s", render_mode_to_string(GET_OPTION(render_mode)));
  snprintf(settings[7], sizeof(settings[7]), "Flip   : rows=%s cols=%s", status_indicator(GET_OPTION(flip_y)),
           status_indicator(GET_OPTION(flip_x)));
  int settings_count = 9;
  snprintf(settings[settings_count++], sizeof(settings[0]), "Animations (number key toggle):");
  snprintf(settings[settings_count++], sizeof(settings[0]), "───────────────────────────────");
  snprintf(settings[settings_count++], sizeof(settings[0]), "(1) Matrix \"Digital Rain\" : %s",
           status_indicator(GET_OPTION(matrix_rain)));
  snprintf(settings[settings_count++], sizeof(settings[0]), "(2) Audio Waveform : %s",
           status_indicator(GET_OPTION(waveform)));
  snprintf(settings[settings_count++], sizeof(settings[0]), "(3) Audio Frequencies (FFT) : %s",
           status_indicator(GET_OPTION(fft)));
  snprintf(settings[settings_count++], sizeof(settings[0]), "(4) Face follow : %s",
           status_indicator(GET_OPTION(face_follow)));
  snprintf(settings[settings_count++], sizeof(settings[0]), "(z) Zalgo intensity : %d", GET_OPTION(zalgo));
#ifndef NDEBUG
  if (!browser)
    snprintf(settings[settings_count++], sizeof(settings[0]), "(0) Sync primitives / deadlocks");
#endif
  snprintf(settings[settings_count++], sizeof(settings[0]), "(-) FPS Counter : %s",
           status_indicator(GET_OPTION(fps_counter)));
  const char *left[sizeof(navigation) / sizeof(navigation[0])];
  int left_count = 0, left_width = 0, right_width = 0;
  for (size_t i = 0; i < sizeof(navigation) / sizeof(navigation[0]); ++i) {
    if (!navigation[i])
      continue;
    left[left_count++] = navigation[i];
    int width = display_width(navigation[i]);
    if (width > left_width)
      left_width = width;
  }
  for (int i = 0; i < settings_count; ++i) {
    int width = display_width(settings[i]);
    if (width > right_width)
      right_width = width;
  }
  const int column_gap = 2;
  int box_width = left_width + column_gap + right_width + 6;

  // Calculate centering position (true mathematical centering)
  // Horizontal centering
  int start_col = (term_width - box_width) / 2;
  if (start_col < 0) {
    start_col = 0;
  }

  // Calculate box height dynamically based on content
  // The submitted layout carries the height measured while building its rows.
  int box_height = (left_count > settings_count ? left_count : settings_count) + 6;

  // Vertical centering with dynamic box height
  int start_row = (term_height - box_height) / 2;
  if (start_row < 0) {
    start_row = 0;
  }

  // Build help screen content
  const size_t BUFFER_SIZE = 8192; // Increased from 4096 to ensure all content fits
  char *buffer = SAFE_MALLOC(BUFFER_SIZE, char *);
  if (!buffer)
    return NULL;
  size_t buf_pos = 0;

#define APPEND(fmt, ...)                                                                                               \
  do {                                                                                                                 \
    int written = snprintf(buffer + buf_pos, BUFFER_SIZE - buf_pos, fmt, ##__VA_ARGS__);                               \
    if (written > 0) {                                                                                                 \
      buf_pos += (size_t)written < BUFFER_SIZE - buf_pos ? (size_t)written : BUFFER_SIZE - buf_pos - 1;                \
    }                                                                                                                  \
  } while (0)

  // The session clears on entry or layout changes; settings repaint in place.
  APPEND("\033[H");

  // Build help screen with proper spacing using UTF-8 width-aware padding
  char line_buf[256];
  char border_buf[512];

  // Generate top border
  APPEND("\033[%d;%dH", start_row + 1, start_col + 1);
  border_buf[0] = '\0';
  int border_pos = 0;
  int result = SAFE_SNPRINTF(border_buf + border_pos, sizeof(border_buf) - border_pos, "%s", "╔");
  if (result < 0) {
    log_error("Failed to write top-left corner: snprintf returned %d", result);
  } else {
    border_pos += result;
  }
  for (int i = 1; i < box_width - 1; i++) {
    result = SAFE_SNPRINTF(border_buf + border_pos, sizeof(border_buf) - border_pos, "%s", "═");
    if (result < 0) {
      log_error("Failed to write horizontal line: snprintf returned %d", result);
      break;
    }
    border_pos += result;
  }
  result = SAFE_SNPRINTF(border_buf + border_pos, sizeof(border_buf) - border_pos, "%s", "╗");
  if (result < 0) {
    log_error("Failed to write top-right corner: snprintf returned %d", result);
  }
  APPEND("%s", border_buf);

  // Title
  APPEND("\033[%d;%dH", start_row + 2, start_col + 1);
  build_help_line(line_buf, sizeof(line_buf), "ascii-chat Keyboard Shortcuts", box_width);
  APPEND("%s", line_buf);

  // Generate separator border
  APPEND("\033[%d;%dH", start_row + 3, start_col + 1);
  border_buf[0] = '\0';
  border_pos = 0;
  result = SAFE_SNPRINTF(border_buf + border_pos, sizeof(border_buf) - border_pos, "%s", "╠");
  if (result < 0) {
    log_error("Failed to write left T: snprintf returned %d", result);
  } else {
    border_pos += result;
  }
  for (int i = 1; i < box_width - 1; i++) {
    result = SAFE_SNPRINTF(border_buf + border_pos, sizeof(border_buf) - border_pos, "%s", "═");
    if (result < 0) {
      log_error("Failed to write horizontal line: snprintf returned %d", result);
      break;
    }
    border_pos += result;
  }
  result = SAFE_SNPRINTF(border_buf + border_pos, sizeof(border_buf) - border_pos, "%s", "╣");
  if (result < 0) {
    log_error("Failed to write right T: snprintf returned %d", result);
  }
  APPEND("%s", border_buf);

  int current_row = 4;
  int content_rows = left_count > settings_count ? left_count : settings_count;
  for (int i = 0; i < content_rows; ++i) {
    const char *lhs = i < left_count ? left[i] : "";
    const char *rhs = i < settings_count ? settings[i] : "";
    char colored_left[1024], colored_right[1024], columns[2048];
    const char *binding_end = i >= 2 ? strstr(lhs, "  ") : NULL;
    color_help_text(lhs, binding_end ? (size_t)(binding_end - lhs) : 0, use_color, colored_left, sizeof(colored_left));
    const char *key_end = rhs[0] == '(' ? strchr(rhs, ')') : NULL;
    color_help_text(rhs, key_end ? (size_t)(key_end - rhs) : 0, use_color, colored_right, sizeof(colored_right));
    int padding = left_width - display_width(lhs) + column_gap;
    snprintf(columns, sizeof(columns), "%s%*s%s", colored_left, padding, "", colored_right);
    append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width, columns);
  }

  // Blank line before footer
  append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width, "");

  // Footer
  char footer[256];
  snprintf(footer, sizeof(footer), "Press %s?%s to close", use_color ? log_level_color(LOG_COLOR_WARN) : "",
           use_color ? log_level_color(LOG_COLOR_RESET) : "");
  append_help_line(buffer, &buf_pos, BUFFER_SIZE, start_row, &current_row, start_col, box_width, footer);

  // Bottom border
  int remaining_buf = BUFFER_SIZE - buf_pos;
  int written = snprintf(buffer + buf_pos, remaining_buf, "\033[%d;%dH", start_row + current_row, start_col + 1);
  if (written > 0) {
    buf_pos += written;
  }
  border_buf[0] = '\0';
  border_pos = 0;
  result = SAFE_SNPRINTF(border_buf + border_pos, sizeof(border_buf) - border_pos, "%s", "╚");
  if (result < 0) {
    log_error("Failed to write bottom-left corner: snprintf returned %d", result);
  } else {
    border_pos += result;
  }
  for (int i = 1; i < box_width - 1; i++) {
    result = SAFE_SNPRINTF(border_buf + border_pos, sizeof(border_buf) - border_pos, "%s", "═");
    if (result < 0) {
      log_error("Failed to write horizontal line: snprintf returned %d", result);
      break;
    }
    border_pos += result;
  }
  result = SAFE_SNPRINTF(border_buf + border_pos, sizeof(border_buf) - border_pos, "%s", "╝");
  if (result < 0) {
    log_error("Failed to write bottom-right corner: snprintf returned %d", result);
  }
  written = snprintf(buffer + buf_pos, BUFFER_SIZE - buf_pos, "%s", border_buf);
  if (written > 0) {
    buf_pos += written;
  }

#undef APPEND
  *minimum = (terminal_size_t){.cols = box_width, .rows = current_row};
  return buffer;
}
