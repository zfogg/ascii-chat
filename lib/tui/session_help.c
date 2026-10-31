#include <ascii-chat/tui/shortcuts.h>
#include <ascii-chat/ui/help_menu.h>
#include <ascii-chat/tui/session.h>
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

void ui_session_help_free(char *text) {
  SAFE_FREE(text);
}

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

static const char *status_indicator(bool enabled) {
  return colored_string(enabled ? LOG_COLOR_INFO : LOG_COLOR_ERROR, enabled ? "O" : "X");
}

char *ui_session_help_render(terminal_size_t size, bool has_media, bool browser, terminal_size_t *minimum) {
  if (!minimum) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Help minimum geometry output is required");
    return NULL;
  }
  const char *navigation[] = {
      "Navigation & Control:",
      "─────────────────────",
      "?      Toggle this help screen TUI",
      "Esc    Close overlay / Quit app",
      browser ? NULL : "v      Open from clipboard",
      browser ? NULL : "Paste  Queue dropped/pasted media",
      browser ? NULL : "< / >  Previous/next media (restart)",
      has_media ? "Space  Play/Pause (files only)" : NULL,
      has_media ? "[      Toggle Media Playback" : NULL,
      has_media ? (terminal_should_use_utf8() ? "← / →  Seek backward/forward 30s"
                                              : "Left / Right  Seek backward/forward 30s")
                : NULL,
      "m / M  Mute/Unmute audio",
      terminal_should_use_utf8() ? "↑ / ↓  Volume up/down (10%)" : "Up / Down  Volume up/down (10%)",
      "c / C  Cycle color mode",
      "f / F  Cycle color filter",
      "x / X  Flip webcam horizontally",
      "y / Y  Flip webcam vertically",
      "w / W  Toggle Face Follow",
      "r / R  Cycle render mode",
      "-      FPS counter",
#ifndef NDEBUG
      browser ? NULL : "`      Print current sync primitive state",
#endif
#if defined(ASCIICHAT_DIAGNOSTIC_UI)
      browser ? NULL : "`      Error stacks diagnostic TUI",
#endif
      browser ? NULL : "o      TUI to open media from path or url",
      "]      Live log viewer/searcher TUI",
      "F12    Version update checker TUI",
      browser ? NULL : "=      Running app statistics TUI",
#ifndef NDEBUG
      browser ? NULL : "0      C code sync primitives TUI",
#endif
  };
  char settings[20][256] = {"Current Settings:", ""};
  char volume_bar[32];
  format_volume_bar(GET_OPTION(speakers_volume), volume_bar, sizeof(volume_bar));
  snprintf(settings[2], sizeof(settings[2]), "Audio  : %s", status_indicator(GET_OPTION(audio_enabled)));
  snprintf(settings[3], sizeof(settings[3]), "Volume : %s", volume_bar);
  snprintf(settings[4], sizeof(settings[4]), "Color  : %s", color_mode_to_string(GET_OPTION(color_mode)));
  snprintf(settings[5], sizeof(settings[5]), "Filter : %s", color_filter_to_string(GET_OPTION(color_filter)));
  snprintf(settings[6], sizeof(settings[6]), "Render : %s", render_mode_to_string(GET_OPTION(render_mode)));
  snprintf(settings[7], sizeof(settings[7]), "Flip   : rows=%s cols=%s", status_indicator(GET_OPTION(flip_y)),
           status_indicator(GET_OPTION(flip_x)));
  snprintf(settings[8], sizeof(settings[8]), "Face Follow : %s", status_indicator(GET_OPTION(face_follow)));
  int settings_count = 9;
  snprintf(settings[settings_count++], sizeof(settings[0]), "Animations (number key toggle):");
  snprintf(settings[settings_count++], sizeof(settings[0]), "-------------------------------");
  snprintf(settings[settings_count++], sizeof(settings[0]), "(1) Matrix \"Digital Rain\" : %s",
           status_indicator(GET_OPTION(matrix_rain)));
  snprintf(settings[settings_count++], sizeof(settings[0]), "(2) Audio Waveform : %s",
           status_indicator(GET_OPTION(waveform)));
  snprintf(settings[settings_count++], sizeof(settings[0]), "(3) Audio Frequencies (FFT) : %s",
           status_indicator(GET_OPTION(fft)));
  snprintf(settings[settings_count++], sizeof(settings[0]), "(z) Zalgo intensity : %d", GET_OPTION(zalgo));
  snprintf(settings[settings_count++], sizeof(settings[0]), "(-) FPS Counter : %s",
           status_indicator(GET_OPTION(fps_counter)));
  bool server = !browser && (GET_OPTION(detected_mode) == MODE_SERVER || GET_OPTION(detected_mode) == MODE_MCP_SERVER);
  bool discovery = !browser && GET_OPTION(detected_mode) == MODE_DISCOVERY_SERVICE;
  ui_help_entry_t entries[40];
  char key_storage[40][48];
  size_t count = 0;
  for (size_t i = 0; i < sizeof(navigation) / sizeof(navigation[0]); ++i) {
    const char *line = navigation[i];
    if (!line || i == 1)
      continue;
    if (line[0] == '?' || line[0] == ']' || line[0] == '=' || line[0] == '0' || line[0] == '`' || line[0] == 'o' ||
        !strncmp(line, "F12", 3))
      continue;
    if ((server || discovery) && i > 1 && line[0] != '?' && strncmp(line, "Esc", 3) && line[0] != ']' &&
        line[0] != '=' && line[0] != '0' && line[0] != '`' && strncmp(line, "F12", 3))
      continue;
    const char *end = strstr(line, "  ");
    if (end) {
      size_t length = (size_t)(end - line);
      if (length >= sizeof(key_storage[0]))
        length = sizeof(key_storage[0]) - 1;
      memcpy(key_storage[count], line, length);
      key_storage[count][length] = '\0';
      while (*end == ' ')
        ++end;
      entries[count] = (ui_help_entry_t){.keys = key_storage[count], .description = end};
    } else
      entries[count] = (ui_help_entry_t){.description = line};
    ++count;
  }
  count += tui_shortcut_help(entries + count, 40 - count, server || discovery, browser);
  if (server || discovery) {
    settings_count = 4;
    snprintf(settings[0], sizeof(settings[0]), "%s", discovery ? "Discovery service:" : "Server:");
    snprintf(settings[1], sizeof(settings[1]), "Port : %d", GET_OPTION(port));
    snprintf(settings[2], sizeof(settings[2]), "Status screen : %s", status_indicator(GET_OPTION(status_screen)));
    snprintf(settings[3], sizeof(settings[3]), "Log level : %d", GET_OPTION(log_level));
  }
  const char *settings_text[20];
  for (int i = 0; i < settings_count; ++i)
    settings_text[i] = settings[i];
  char footer[128];
  snprintf(footer, sizeof(footer), "Press %s to close", colored_string(LOG_COLOR_WARN, "?"));
  ui_help_menu_t menu = {.title = discovery ? "ascii-chat Discovery Service Keyboard Shortcuts"
                                  : server  ? "ascii-chat Server Keyboard Shortcuts"
                                            : "ascii-chat Keyboard Shortcuts",
                         .entries = entries,
                         .count = count,
                         .settings = settings_text,
                         .settings_count = (size_t)settings_count,
                         .footer = footer,
                         .top_margin = 0};
  *minimum = ui_help_menu_measure(&menu);
  frame_buffer_t *output = frame_buffer_create(size.rows, size.cols);
  if (!output)
    return NULL;
  ui_help_menu_render(output, size, &menu);
  size_t length = frame_buffer_get_length(output);
  char *result = SAFE_MALLOC(length + 1, char *);
  memcpy(result, frame_buffer_get_content(output), length);
  result[length] = '\0';
  frame_buffer_destroy(output);
  return result;
}

char *ui_session_context_help_render(terminal_size_t size, ui_screen_t screen, terminal_size_t *minimum) {
  if (!minimum) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Help minimum geometry output is required");
    return NULL;
  }
  static const ui_help_entry_t sync[] = {{"0 / Esc", "Close sync viewer"},
                                         {"Up / Down", "Select primitive"},
                                         {"Left / Right", "Previous/next page"},
                                         {"Home / End", "First/last primitive"},
                                         {"=", "Running app statistics"},
                                         {"]", "Log viewer"},
                                         {"?", "Close this help"}};
  static const ui_help_entry_t stats[] = {{"= / Esc", "Close statistics"},
                                          {"Tab / Left / Right", "Switch data page"},
                                          {"Up / Down", "Select and scroll rows"},
                                          {"Home / End", "First/last row"},
                                          {"0", "Sync primitives"},
                                          {"]", "Log viewer"},
                                          {"?", "Close this help"}};
  static const ui_help_entry_t errors[] = {
      {"` / Esc", "Close error stacks"}, {"Tab", "Current/history page"}, {"Up / Down", "Select and scroll errors"},
      {"s", "Change sort order"},        {"x", "Clear selected error"},   {"=", "Running app statistics"},
      {"0", "Sync primitives"},          {"?", "Close this help"}};
  static const ui_help_entry_t update[] = {{"F12 / Esc", "Close update checker"},
                                           {"Left / Right / Tab", "Select button"},
                                           {"Enter", "Activate selected button"},
                                           {"O", "Open release page"},
                                           {"R", "Check again"},
                                           {"?", "Close this help"}};
  static const ui_help_entry_t lan[] = {{"Up / Down / j / k", "Select LAN server"},
                                        {"0-9", "Enter server number"},
                                        {"Enter", "Connect to selected server"},
                                        {"Esc", "Cancel LAN selection"},
                                        {"=", "Running app statistics"},
                                        {"0", "Sync primitives"},
                                        {"?", "Close this help"}};
  static const ui_help_entry_t media[] = {
      {"Enter", "Play the entered paths or URLs"},  {"Esc / Ctrl-O", "Cancel media input"},
      {"Left / Right", "Move the text cursor"},     {"Home / End", "Beginning/end of input"},
      {"Backspace / Delete", "Delete a character"}, {"?", "Help when the input is empty"}};
  const ui_help_entry_t *entries = NULL;
  size_t count = 0;
  const char *title = NULL;
  switch (screen) {
  case UI_SCREEN_ERROR: {
    static const ui_help_entry_t popup[] = {{"Left / Right", "Previous / next error"},
                                            {"Home / End", "First / last error"},
                                            {"Up / Down", "Scroll error details"},
                                            {"PgUp / PgDn", "Scroll a page"},
                                            {"Enter / Esc", "Dismiss all errors"},
                                            {"q", "Dismiss all errors"},
                                            {"?", "Close this help"}};
    entries = popup;
    count = sizeof(popup) / sizeof(*popup);
    title = "Error Popup Keyboard Shortcuts";
    break;
  }
  case UI_SCREEN_SYNC:
    entries = sync;
    count = sizeof(sync) / sizeof(*sync);
    title = "Sync Viewer Keyboard Shortcuts";
    break;
  case UI_SCREEN_STATS:
    entries = stats;
    count = sizeof(stats) / sizeof(*stats);
    title = "Statistics Keyboard Shortcuts";
    break;
  case UI_SCREEN_ERRNO:
    entries = errors;
    count = sizeof(errors) / sizeof(*errors);
    title = "Error Stacks Keyboard Shortcuts";
    break;
  case UI_SCREEN_PROMPT:
    entries = media;
    count = sizeof(media) / sizeof(*media);
    title = "Media Input Keyboard Shortcuts";
    break;
  case UI_SCREEN_MDNS:
    entries = lan;
    count = sizeof(lan) / sizeof(*lan);
    title = "LAN Selection Keyboard Shortcuts";
    break;
  case UI_SCREEN_UPDATE:
    entries = update;
    count = sizeof(update) / sizeof(*update);
    title = "Update Checker Keyboard Shortcuts";
    break;
  default:
    return ui_session_help_render(size, GET_OPTION(media_file)[0] || GET_OPTION(media_url)[0], false, minimum);
  }
  char footer[128];
  snprintf(footer, sizeof(footer), "Press %s to close", colored_string(LOG_COLOR_WARN, "?"));
  ui_help_menu_t menu = {.title = title, .entries = entries, .count = count, .footer = footer, .top_margin = 0};
  *minimum = ui_help_menu_measure(&menu);
  frame_buffer_t *buffer = frame_buffer_create(size.rows, size.cols);
  if (!buffer)
    return NULL;
  ui_help_menu_render(buffer, size, &menu);
  size_t length = frame_buffer_get_length(buffer);
  char *result = SAFE_MALLOC(length + 1, char *);
  memcpy(result, frame_buffer_get_content(buffer), length);
  result[length] = '\0';
  frame_buffer_destroy(buffer);
  return result;
}
