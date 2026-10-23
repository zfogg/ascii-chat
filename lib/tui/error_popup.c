#include <ascii-chat/tui/error_popup.h>
#include <ascii-chat/ui/buttons.h>
#include <ascii-chat/tui/controller.h>
#include <ascii-chat/tui/too_small.h>
#include <ascii-chat/ui/popup.h>
#include <ascii-chat/ui/scrollbar.h>
#include <ascii-chat/ui/text.h>
#include <ascii-chat/debug/backtrace.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/utf8.h>
#include <stdatomic.h>
#include <string.h>

// Only short value copies and navigation run under this guard, never rendering or symbolization.
static atomic_flag g_guard = ATOMIC_FLAG_INIT;
static asciichat_error_context_t g_entries[ASCIICHAT_ERRNO_HISTORY_SIZE];
static size_t g_count, g_selected, g_scroll, g_body_rows = 1, g_page_rows = 1;
static uint64_t g_latest, g_dropped;

static void lock_state(void) {
  while (atomic_flag_test_and_set_explicit(&g_guard, memory_order_acquire)) {
  }
}
static void unlock_state(void) {
  atomic_flag_clear_explicit(&g_guard, memory_order_release);
}

static void refresh_locked(void) {
  uint64_t selected_id = g_count ? g_entries[g_selected].error_id : 0;
  g_count = errno_notifications_snapshot(g_entries, ASCIICHAT_ERRNO_HISTORY_SIZE, &g_dropped);
  uint64_t latest = g_count ? g_entries[g_count - 1].error_id : 0;
  if (latest != g_latest) {
    g_selected = g_count ? g_count - 1 : 0;
    g_scroll = 0;
    g_latest = latest;
  } else {
    g_selected = 0;
    for (size_t i = 0; i < g_count; ++i)
      if (g_entries[i].error_id == selected_id)
        g_selected = i;
  }
}

bool ui_error_popup_visible(void) {
  return errno_notifications_pending();
}

terminal_size_t ui_error_popup_minimum(void) {
  return (terminal_size_t){.cols = 60, .rows = 14};
}

void ui_error_popup_key(keyboard_key_t key) {
  lock_state();
  refresh_locked();
  uint64_t dismiss = 0;
  size_t previous = g_selected;
  switch ((int)key) {
  case KEY_ESCAPE:
  case 'q':
  case '\r':
  case '\n':
    dismiss = g_latest;
    break;
  case KEY_LEFT:
    if (g_selected)
      --g_selected;
    break;
  case KEY_RIGHT:
    if (g_selected + 1 < g_count)
      ++g_selected;
    break;
  case KEY_HOME:
    g_selected = 0;
    break;
  case KEY_END:
    g_selected = g_count ? g_count - 1 : 0;
    break;
  case KEY_UP:
    if (g_scroll)
      --g_scroll;
    break;
  case KEY_DOWN:
    ++g_scroll;
    break;
  case KEY_PAGEUP:
    g_scroll = g_scroll > g_page_rows ? g_scroll - g_page_rows : 0;
    break;
  case KEY_PAGEDOWN:
    g_scroll += g_page_rows;
    break;
  default:
    break;
  }
  if (g_selected != previous)
    g_scroll = 0;
  size_t maximum = g_body_rows > g_page_rows ? g_body_rows - g_page_rows : 0;
  if (g_scroll > maximum)
    g_scroll = maximum;
  unlock_state();
  if (dismiss)
    errno_notifications_dismiss(dismiss);
  ui_controller_redraw();
}

// These caches belong exclusively to the presentation thread. Raw backtraces are copied,
// symbolized once per selection, then freed; notification snapshots never own symbols.
static uint64_t g_cached_id;
static int g_cached_width;
static bool g_cached_color;
static char g_wrapped[65536];
static size_t g_lines[2048], g_line_count, g_trace_start;
#ifdef ASCIICHAT_DIAGNOSTIC_UI
static backtrace_text_t g_trace;
#endif

static void sanitize(char *out, size_t capacity, const char *text) {
  size_t used = 0;
  for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p && used + 1 < capacity; ++p) {
    // Messages may contain paths or remote text. Never interpret terminal controls from them.
    unsigned char c = *p;
    if (c == 0xc2 && p[1] >= 0x80 && p[1] <= 0x9f) {
      out[used++] = '?';
      ++p;
      continue;
    }
    if (c == '\t')
      c = ' ';
    else if ((c < 32 && c != '\n') || c == 127)
      c = '?';
    out[used++] = (char)c;
  }
  out[used] = '\0';
}

static void append_bytes(size_t *used, const char *text, size_t bytes) {
  bytes = MIN(bytes, sizeof(g_wrapped) - 1 - *used);
  memcpy(g_wrapped + *used, text, bytes);
  *used += bytes;
  g_wrapped[*used] = '\0';
}

static void wrap_body(const char *text, int width) {
  size_t used = 0;
  int cells = 0;
  char style[128] = "";
  g_line_count = 1;
  g_lines[0] = 0;
  g_wrapped[0] = '\0';
  while (*text && used + 256 < sizeof(g_wrapped) && g_line_count < ARRAY_SIZE(g_lines)) {
    if (text[0] == '\033' && text[1] == '[') {
      const char *end = text + 2;
      while (*end && !(*end >= '@' && *end <= '~'))
        ++end;
      if (*end != 'm') {
        // The formatter supplies SGR only. Ignore any unexpected control sequence.
        text = *end ? end + 1 : end;
        continue;
      }
      size_t bytes = (size_t)(end + 1 - text);
      append_bytes(&used, text, bytes);
      if (bytes < sizeof(style)) {
        memcpy(style, text, bytes);
        style[bytes] = '\0';
      }
      text = end + 1;
      continue;
    }
    int bytes = utf8_next_char_bytes(text, strlen(text));
    if (bytes <= 0)
      bytes = 1;
    int columns = utf8_display_width_n(text, (size_t)bytes);
    if (*text == '\n' || (cells && cells + columns > width)) {
      append_bytes(&used, "\033[0m", 4);
      g_lines[g_line_count++] = used + 1;
      append_bytes(&used, "\n", 1);
      append_bytes(&used, style, strlen(style));
      cells = 0;
      if (*text == '\n')
        ++text;
      continue;
    }
    append_bytes(&used, text, (size_t)bytes);
    cells += MAX(columns, 0);
    text += bytes;
  }
}

static void build_body(const asciichat_error_context_t *error, int width, bool colors) {
  g_trace_start = 0;
  char message[ASCIICHAT_ERRNO_MESSAGE_MAX], thread[160], file[1024], function[256], line[24];
  sanitize(message, sizeof(message), error->context_message);
  named_metadata_t metadata = {0};
  bool named = named_metadata_copy((uintptr_t)error->thread_id, &metadata) && metadata.name[0];
  if (named)
    sanitize(thread, sizeof(thread), metadata.name);
  else
    safe_snprintf(thread, sizeof(thread), "0x%llx", (unsigned long long)error->thread_id);
  frame_buffer_t *body = frame_buffer_create(0, 0);
  if (!body) {
    wrap_body("Error details unavailable (out of memory)", width);
    return;
  }
  const char *grey = colors ? ui_text_color_prefix(LOG_COLOR_GREY) : "";
  const char *reset = colors ? "\033[0m" : "";
  frame_buffer_printf(body, "Details: %s%s\n\n%sThread: %s", message[0] ? message : "No additional details",
                      error->message_truncated ? " [truncated]" : "", grey,
                      colors ? colored_string(named ? LOG_COLOR_INFO : LOG_COLOR_FATAL, thread) : thread);
  if (named) {
    char address[40];
    safe_snprintf(address, sizeof(address), "(0x%llx)", (unsigned long long)error->thread_id);
    frame_buffer_printf(body, " %s", colors ? colored_string(LOG_COLOR_FATAL, address) : address);
  }
  frame_buffer_printf(body, "%s\n", reset);
  if (error->has_system_error)
    frame_buffer_printf(body, "System error: %d (%s)\n", error->system_errno, SAFE_STRERROR(error->system_errno));
  if (error->has_wsa_error)
    frame_buffer_printf(body, "Socket error: %d\n", error->wsa_error);
#ifdef ASCIICHAT_DIAGNOSTIC_UI
  sanitize(file, sizeof(file), error->file ? extract_project_relative_path(error->file) : "?");
  sanitize(function, sizeof(function), error->function);
  safe_snprintf(line, sizeof(line), "%d", error->line);
  frame_buffer_printf(body, "%sSource: %s %s:%s%s\n", grey, colors ? colored_string(LOG_COLOR_DEBUG, file) : file,
                      colors ? colored_string(LOG_COLOR_DEV, function) : function,
                      colors ? colored_string(LOG_COLOR_GREY, line) : line, reset);
  if (g_cached_id != error->error_id) {
    memset(&g_trace, 0, sizeof(g_trace));
    backtrace_t trace = error->backtrace;
    trace.symbols = NULL;
    trace.tried_symbolize = false;
    if (trace.count) {
      backtrace_symbolize(&trace);
      if (trace.symbols)
        backtrace_format_styled(&g_trace, "Backtrace", &trace, 0, 0, NULL);
      backtrace_t_free(&trace);
    }
  }
  wrap_body(frame_buffer_get_content(body), width);
  if (g_trace.plain[0])
    g_trace_start = g_line_count + 1;
  const char *trace_text = g_trace.plain[0] ? (colors ? g_trace.colored : g_trace.plain) : "Backtrace unavailable";
  size_t trace_length = strlen(trace_text);
  if (trace_length && trace_text[trace_length - 1] == '\n')
    --trace_length;
  frame_buffer_printf(body, "\n%.*s", (int)trace_length, trace_text);
#else
  (void)file;
  (void)function;
  (void)line;
#endif
  wrap_body(frame_buffer_get_content(body), width);
  frame_buffer_destroy(body);
  g_cached_id = error->error_id;
  g_cached_width = width;
  g_cached_color = colors;
}

void ui_error_popup_render(frame_buffer_t *out, terminal_size_t size) {
  asciichat_error_context_t error;
  lock_state();
  refresh_locked();
  size_t count = g_count, selected = g_selected;
  uint64_t dropped = g_dropped;
  if (count)
    error = g_entries[selected];
  unlock_state();
  if (!count)
    return;
  terminal_size_t minimum = ui_error_popup_minimum();
  frame_buffer_append(out, "\033[0m\033[?25l", 10);
  if (ui_too_small(size, minimum)) {
    ui_too_small_render(out, size, minimum);
    return;
  }
  bool colors = !GET_OPTION(strip_ansi) && terminal_should_color_output(frame_buffer_get_screen_output_fd());
  int width = MIN(size.cols - 4, 100);
  int body_width = width - 6;
  if (g_cached_id != error.error_id || g_cached_width != body_width || g_cached_color != colors)
    build_body(&error, body_width, colors);
  size_t desired_rows = g_trace_start ? MIN(g_line_count, g_trace_start + 12) : g_line_count;
  int body_rows = MIN(size.rows - 8, MIN(22, (int)desired_rows));
  size_t fixed_rows = g_trace_start && g_trace_start < (size_t)body_rows ? g_trace_start : 0;
  if (g_trace_start && !fixed_rows)
    body_rows = MIN(body_rows, 12);
  int height = body_rows + 6;
  int top = (size.rows - height) / 2 + 1, left = (size.cols - width) / 2 + 1;
  size_t scroll_rows = g_line_count - fixed_rows;
  size_t page_rows = (size_t)body_rows - fixed_rows;
  lock_state();
  g_body_rows = scroll_rows;
  g_page_rows = page_rows;
  size_t maximum = scroll_rows > page_rows ? scroll_rows - page_rows : 0;
  g_scroll = MIN(g_scroll, maximum);
  size_t scroll = g_scroll;
  unlock_state();
  ui_popup_frame(out, top, left, width, height, LOG_COLOR_RESET);
  char title[128];
  safe_snprintf(title, sizeof(title), "Error (%zu)   %zu of %zu%s", count, selected + 1, count,
                dropped ? " [older errors dropped]" : "");
  char heading[256];
  ui_text_cell(heading, sizeof(heading), title, width - 4, UI_ALIGN_LEFT);
  frame_buffer_printf(out, "\033[%d;%dH%s", top + 1, left + 2,
                      colors ? colored_string(LOG_COLOR_ERROR, heading) : heading);
  char code[512], cell[1024];
  safe_snprintf(code, sizeof(code), "%s (%d): %s", asciichat_error_name(error.code), error.code,
                asciichat_error_string(error.code));
  ui_text_cell(cell, sizeof(cell), code, width - 4, UI_ALIGN_LEFT);
  frame_buffer_printf(out, "\033[%d;%dH%s", top + 2, left + 2, cell);
  for (int i = 0; i < body_rows; ++i) {
    size_t index = (size_t)i < fixed_rows ? (size_t)i : (size_t)i + scroll;
    if (index >= g_line_count)
      break;
    const char *start = g_wrapped + g_lines[index];
    const char *end = strchr(start, '\n');
    frame_buffer_printf(out, "\033[%d;%dH%.*s\033[0m", top + 4 + i, left + 2,
                        (int)(end ? end - start : (ptrdiff_t)strlen(start)), start);
  }
  ui_scrollbar_render(out, top + 4 + (int)fixed_rows, left + width - 3, (int)page_rows, scroll_rows, page_rows, scroll);
  const char *orange = colors ? ui_text_color_prefix(LOG_COLOR_DEV) : "";
  const char *reset = colors ? "\033[0m" : "";
  frame_buffer_printf(out, "\033[%d;%dH%s?%s Help", top + height - 2, left + 2, orange, reset);
  ui_button_t button = {.label = "Dismiss all", .shortcut = '\r'};
  ui_buttons_render(out, top + height - 2, left + 10, width - 20, &button, 1, 0);
}

void ui_error_popup_reset(void) {
  lock_state();
  g_count = g_selected = g_scroll = 0;
  g_latest = g_dropped = 0;
  unlock_state();
  g_cached_id = 0;
}
