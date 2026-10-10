#include <ascii-chat/ui/notice.h>
#include <ascii-chat/ui/controller.h>
#include <ascii-chat/platform/question.h>
#include <ascii-chat/common.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/util/time.h>
#include <string.h>

static _Thread_local ui_notice_severity_t g_prompt_severity = UI_NOTICE_INFO;

static const char *notice_color(ui_notice_severity_t severity) {
  switch (severity) {
  case UI_NOTICE_FATAL:
    return "\033[1;35m";
  case UI_NOTICE_DANGER:
    return "\033[1;31m";
  case UI_NOTICE_WARNING:
    return "\033[1;33m";
  default:
    return "\033[1;37m";
  }
}

static void border(frame_buffer_t *buffer, int width, const char *left, const char *fill, const char *right) {
  if (!buffer)
    return;
  frame_buffer_printf(buffer, "%s", left);
  for (int i = 0; i < width - 2; ++i)
    frame_buffer_printf(buffer, "%s", fill);
  frame_buffer_printf(buffer, "%s\n", right);
}

int ui_notice_render(frame_buffer_t *buffer, const char *text, ui_notice_severity_t severity, int cols, bool unicode,
                     bool color) {
  if (!text)
    text = "";
  const char *text_end = text + strlen(text);
  // Reserve the final column to avoid terminal autowrap at the right border.
  int width = cols > 81 ? 80 : cols - 1;
  bool boxed = width >= 8;
  int content = boxed ? width - 4 : (cols > 2 ? cols - 1 : 2);
  const char *side = unicode ? "║" : "|";
  if (buffer && color)
    frame_buffer_printf(buffer, "%s", notice_color(severity));
  if (boxed)
    border(buffer, width, unicode ? "╔" : "+", unicode ? "═" : "-", unicode ? "╗" : "+");
  int rows = boxed ? 2 : 0;
  do {
    int used = 0;
    if (buffer && boxed)
      frame_buffer_printf(buffer, "%s ", side);
    while (*text && *text != '\n') {
      int bytes = utf8_next_char_bytes(text, (size_t)(text_end - text));
      if (bytes <= 0)
        bytes = 1;
      bool control = (unsigned char)*text < 32 || (unsigned char)*text == 127 ||
                     (bytes == 2 && (unsigned char)text[0] == 0xc2 && (unsigned char)text[1] >= 0x80 &&
                      (unsigned char)text[1] <= 0x9f);
      int cells = control ? 1 : utf8_display_width_n(text, (size_t)bytes);
      if (cells < 0)
        cells = 1;
      if (used + cells > content && used)
        break;
      if (buffer) {
        if (control)
          frame_buffer_append(buffer, "?", 1);
        else
          frame_buffer_append(buffer, text, (size_t)bytes);
      }
      used += cells;
      text += bytes;
    }
    if (buffer) {
      if (boxed)
        frame_buffer_printf(buffer, "%*s %s", content - used, "", side);
      frame_buffer_append(buffer, "\n", 1);
    }
    ++rows;
    if (*text == '\n')
      ++text;
  } while (*text);
  if (boxed)
    border(buffer, width, unicode ? "╚" : "+", unicode ? "═" : "-", unicode ? "╝" : "+");
  if (buffer && color)
    frame_buffer_append(buffer, "\033[0m", 4);
  return rows;
}

typedef struct {
  ui_notice_severity_t severity;
  bool unicode;
  bool color;
  uint64_t started;
  char text[];
} notice_snapshot_t;

static void render_notice(terminal_size_t size, const void *data) {
  const notice_snapshot_t *notice = data;
  // Only the presentation thread owns these counters. Every queued notice gets
  // its own full reading interval when it first becomes visible.
  static uint64_t active_id;
  static uint64_t started;
  static uint64_t last_tick;
  static terminal_size_t previous_size;
  uint64_t now = time_get_ns();
  if (active_id != notice->started || previous_size.cols != size.cols || previous_size.rows != size.rows) {
    active_id = notice->started;
    started = now;
  } else if (last_tick && now - last_tick > NS_PER_SEC_INT) {
    // Time spent behind a prompt must not consume the reading interval.
    started += now - last_tick;
  }
  last_tick = now;
  previous_size = size;
  frame_buffer_t *buffer = frame_buffer_create(0, 0);
  if (!buffer)
    return;
  int rows = ui_notice_render(buffer, notice->text, notice->severity, size.cols, notice->unicode, notice->color);
  int page_rows = size.rows > 2 ? size.rows - 2 : 1;
  int pages = (rows + page_rows - 1) / page_rows;
  uint64_t page = (time_get_ns() - started) / (10 * NS_PER_SEC_INT);
  if (page >= (uint64_t)pages) {
    frame_buffer_destroy(buffer);
    ui_controller_remove(UI_SCREEN_NOTICE);
    return;
  }
  const char *start = frame_buffer_get_content(buffer);
  const char *end = start + frame_buffer_get_length(buffer);
  for (uint64_t i = 0; i < page * (uint64_t)page_rows && start < end; ++i) {
    const char *newline = memchr(start, '\n', (size_t)(end - start));
    start = newline ? newline + 1 : end;
  }
  const char *stop = start;
  for (int i = 0; i < page_rows && stop < end; ++i) {
    const char *newline = memchr(stop, '\n', (size_t)(end - stop));
    stop = newline ? newline + 1 : end;
  }
  ui_controller_write(STDERR_FILENO, "\033[H\033[2J", 7);
  if (notice->color)
    ui_controller_write(STDERR_FILENO, notice_color(notice->severity), strlen(notice_color(notice->severity)));
  ui_controller_write(STDERR_FILENO, start, (size_t)(stop - start));
  if (notice->color)
    ui_controller_write(STDERR_FILENO, "\033[0m", 4);
  if (pages > 1)
    ui_controller_printf(STDERR_FILENO, "Page %llu/%d (advances automatically)", (unsigned long long)page + 1, pages);
  frame_buffer_destroy(buffer);
}

asciichat_error_t ui_notice_present(ui_notice_severity_t severity, const char *text) {
  if (!text)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing notice text");
  bool tty = platform_isatty(STDERR_FILENO);
  bool color = !GET_OPTION(strip_ansi) && terminal_should_color_output(STDERR_FILENO);
  bool unicode = tty && terminal_supports_utf8();
  if (severity != UI_NOTICE_FATAL && tty && ui_controller_is_presenting() && !ui_controller_is_owner() &&
      !GET_OPTION(snapshot_mode)) {
    size_t bytes = sizeof(notice_snapshot_t) + strlen(text) + 1;
    notice_snapshot_t *snapshot = SAFE_MALLOC(bytes, notice_snapshot_t *);
    if (!snapshot)
      return SET_ERRNO(ERROR_MEMORY, "Cannot allocate notice");
    snapshot->severity = severity;
    snapshot->unicode = unicode;
    snapshot->color = color;
    snapshot->started = time_get_ns();
    memcpy(snapshot->text, text, strlen(text) + 1);
    asciichat_error_t result = ui_controller_submit(
        UI_SCREEN_NOTICE, STDERR_FILENO, (terminal_size_t){.cols = 1, .rows = 1}, render_notice, snapshot, bytes);
    SAFE_FREE(snapshot);
    return result;
  }
  frame_buffer_t *buffer = frame_buffer_create(0, 0);
  if (!buffer)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate notice buffer");
  if (severity == UI_NOTICE_FATAL && tty)
    frame_buffer_append(buffer, "\033[0m\033[?25h\r\n", 12);
  int cols = tty ? ui_controller_size().cols : 80;
  ui_notice_render(buffer, text, severity, cols, unicode, color);
  asciichat_error_t result = ASCIICHAT_OK;
  if (severity == UI_NOTICE_FATAL && !ui_controller_is_owner())
    ui_controller_finish(STDERR_FILENO, frame_buffer_get_content(buffer), frame_buffer_get_length(buffer));
  else
    result = ui_controller_write(STDERR_FILENO, frame_buffer_get_content(buffer), frame_buffer_get_length(buffer));
  frame_buffer_destroy(buffer);
  return result;
}

bool ui_notice_confirm(ui_notice_severity_t severity, const char *text, unsigned timeout_seconds) {
  ui_notice_severity_t previous = g_prompt_severity;
  g_prompt_severity = severity;
  if (terminal_can_prompt_user())
    log_msg(severity >= UI_NOTICE_DANGER ? LOG_ERROR : LOG_WARN, __FILE__, __LINE__, __func__, "%s", text);
  else
    ui_notice_log(severity, __FILE__, __LINE__, __func__, "CONFIRMATION REQUIRED", "%s", text);
  bool accepted = platform_prompt_yes_no_timeout(text, false, timeout_seconds);
  g_prompt_severity = previous;
  return accepted;
}

ui_notice_severity_t ui_notice_prompt_severity(void) {
  return g_prompt_severity;
}

void ui_notice_flush_snapshot(int fd, const void *data) {
  const notice_snapshot_t *notice = data;
  frame_buffer_t *buffer = frame_buffer_create(0, 0);
  if (!buffer)
    return;
  if (platform_isatty(fd))
    frame_buffer_append(buffer, "\033[0m\n", 5);
  terminal_size_t size = {.cols = 80};
  if (platform_isatty(fd))
    (void)terminal_get_size_fd(fd, &size);
  ui_notice_render(buffer, notice->text, notice->severity, size.cols, notice->unicode, notice->color);
  platform_write_all(fd, frame_buffer_get_content(buffer), frame_buffer_get_length(buffer));
  frame_buffer_destroy(buffer);
}

void ui_notice_dismiss(void) {
  ui_controller_remove(UI_SCREEN_NOTICE);
}
