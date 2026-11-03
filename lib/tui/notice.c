#include <ascii-chat/tui/notice.h>
#include <ascii-chat/crypto/key_identity/display.h>
#include <ascii-chat/tui/controller.h>
#include <ascii-chat/platform/question.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/keyboard.h>
#include <stdatomic.h>
#include <ascii-chat/common.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/ui/popup.h>
#include <ascii-chat/ui/help_menu.h>
#include <ascii-chat/ui/text.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/util/time.h>
#include <string.h>

static _Thread_local ui_prompt_kind_t g_prompt_kind = UI_PROMPT_CONFIRMATION;
static _Thread_local ui_notice_severity_t g_prompt_severity = UI_NOTICE_INFO;

static const char *notice_color(ui_notice_severity_t severity) {
  switch (severity) {
  case UI_NOTICE_FATAL:
    return ui_text_color_prefix(LOG_COLOR_FATAL);
  case UI_NOTICE_DANGER:
    return ui_text_color_prefix(LOG_COLOR_ERROR);
  case UI_NOTICE_WARNING:
    return ui_text_color_prefix(LOG_COLOR_WARN);
  case UI_NOTICE_SUCCESS:
    return ui_text_color_prefix(LOG_COLOR_INFO);
  default:
    return ui_text_color_prefix(LOG_COLOR_RESET);
  }
}

static const char *notice_label(ui_notice_severity_t severity) {
  switch (severity) {
  case UI_NOTICE_FATAL:
    return "FATAL";
  case UI_NOTICE_DANGER:
    return "ERROR";
  case UI_NOTICE_WARNING:
    return "WARNING";
  case UI_NOTICE_SUCCESS:
    return "SUCCESS";
  default:
    return "INFO";
  }
}

static void border(frame_buffer_t *buffer, int width, ui_popup_edge_t edge, bool unicode, ui_notice_severity_t severity,
                   bool color) {
  if (!buffer)
    return;
  log_color_t shade = severity == UI_NOTICE_FATAL     ? LOG_COLOR_FATAL
                      : severity == UI_NOTICE_DANGER  ? LOG_COLOR_ERROR
                      : severity == UI_NOTICE_WARNING ? LOG_COLOR_WARN
                      : severity == UI_NOTICE_SUCCESS ? LOG_COLOR_INFO
                                                      : LOG_COLOR_RESET;
  ui_popup_rule(buffer, width, edge, true, unicode, color ? shade : LOG_COLOR_RESET);
  frame_buffer_printf(buffer, "\n%s%s", color ? "\033[1m" : "", color ? notice_color(severity) : "");
}

int ui_notice_render(frame_buffer_t *buffer, const char *text, ui_notice_severity_t severity, int cols, bool unicode,
                     bool color) {
  if (!text)
    text = "";
  const char *body = text;
  text = notice_label(severity);
  bool rendering_label = true;
  const char *text_end = text + strlen(text);
  // Reserve the final column to avoid terminal autowrap at the right border.
  int width = cols > 81 ? 80 : cols - 1;
  bool boxed = width >= 8;
  int content = boxed ? width - 4 : (cols > 2 ? cols - 1 : 2);
  const char *side = unicode ? "║" : "|";
  if (buffer && color)
    frame_buffer_printf(buffer, "\033[1m%s", notice_color(severity));
  if (boxed)
    border(buffer, width, UI_POPUP_TOP, unicode, severity, color);
  int rows = boxed ? 2 : 0;
  const char *art_end = NULL;
  const char *word_end = NULL;
  bool title_word = false;
  bool first_letter = false;
  do {
    // The Keymask formatter identifies complete plain-text blocks; input ANSI
    // escapes still pass through the normal control-character sanitization.
    const char *block_end = key_identity_art_end(text);
    if (block_end)
      art_end = block_end;
    bool neutral = art_end && text < art_end;
    int used = 0;
    if (buffer && boxed)
      frame_buffer_printf(buffer, "%s ", side);
    if (buffer && color && neutral)
      frame_buffer_append(buffer, "\033[0;39m", 7);
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
        else if (!rendering_label && !neutral) {
          if (!word_end || text >= word_end) {
            word_end = text;
            title_word = (*text >= 'A' && *text <= 'Z') || (*text >= 'a' && *text <= 'z');
            first_letter = true;
            while (*word_end && *word_end != ' ' && *word_end != '\n' && *word_end != '\t') {
              unsigned char c = (unsigned char)*word_end;
              bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
              bool punctuation = strchr(",.;:!?", c) && (!word_end[1] || word_end[1] == ' ' || word_end[1] == '\n');
              if (!letter && !punctuation)
                title_word = false;
              ++word_end;
            }
          }
          if (title_word && bytes == 1) {
            char c = *text;
            if (first_letter && c >= 'a' && c <= 'z')
              c -= 'a' - 'A';
            else if (!first_letter && c >= 'A' && c <= 'Z')
              c += 'a' - 'A';
            first_letter = false;
            frame_buffer_append(buffer, &c, 1);
          } else {
            frame_buffer_append(buffer, text, (size_t)bytes);
          }
        } else
          frame_buffer_append(buffer, text, (size_t)bytes);
      }
      used += cells;
      text += bytes;
    }
    if (buffer) {
      if (color && neutral)
        frame_buffer_printf(buffer, "\033[1m%s", notice_color(severity));
      if (boxed)
        frame_buffer_printf(buffer, "%*s %s", content - used, "", side);
      frame_buffer_append(buffer, "\n", 1);
    }
    ++rows;
    if (*text == '\n')
      ++text;
    if (!*text) {
      if (!rendering_label)
        break;
      if (boxed) {
        border(buffer, width, UI_POPUP_SEPARATOR, unicode, severity, color);
        ++rows;
      }
      rendering_label = false;
      text = body;
      text_end = text + strlen(text);
    }
  } while (true);
  if (boxed)
    border(buffer, width, UI_POPUP_BOTTOM, unicode, severity, color);
  if (buffer && color)
    frame_buffer_append(buffer, "\033[0m", 4);
  return rows;
}

typedef struct {
  ui_notice_severity_t severity;
  bool unicode;
  bool color;
  uint64_t started;
  size_t marker_offset;
  size_t title_offset;
  atomic_int page;
  atomic_int pages;
  atomic_int presented_page;
  int rendered_page;
  int rendered_cols;
  int rendered_rows;
  char text[];
} notice_snapshot_t;

const char *ui_notice_title(const void *data) {
  const notice_snapshot_t *notice = data;
  return notice->severity == UI_NOTICE_WARNING                                         ? "warning"
         : notice->severity == UI_NOTICE_DANGER || notice->severity == UI_NOTICE_FATAL ? "error"
                                                                                       : "notice";
}

#define FIRST_USE_LINE_COUNT 64
#define FIRST_USE_LINE_BYTES 512
#define FIRST_USE_CONTENT_COLUMNS 66

static size_t first_use_wrap(const char *text, int width, char lines[FIRST_USE_LINE_COUNT][FIRST_USE_LINE_BYTES]) {
  size_t count = 0;
  for (const char *line = text; *line && count < FIRST_USE_LINE_COUNT;) {
    const char *end = strchr(line, '\n');
    if (!end)
      end = line + strlen(line);
    const char *tab = memchr(line, '\t', (size_t)(end - line));
    bool shortcut = tab && tab - line == 1;
    char key[2] = {0}, styled[128] = {0};
    if (shortcut) {
      key[0] = *line;
      ui_help_keys(styled, sizeof(styled), key);
      line = tab + 1;
    }
    if (line == end) {
      lines[count++][0] = '\0';
    } else {
      bool first = true;
      while (line < end && count < FIRST_USE_LINE_COUNT) {
        while (line < end && *line == ' ')
          ++line;
        if (line == end)
          break;
        int available = width - (shortcut ? 3 : 0);
        size_t length = (size_t)(end - line);
        if (length > (size_t)available) {
          length = (size_t)available;
          size_t split = length;
          while (split > 0 && line[split] != ' ')
            --split;
          if (split)
            length = split;
        }
        if (shortcut)
          snprintf(lines[count], FIRST_USE_LINE_BYTES, "%s  %.*s", first ? styled : " ", (int)length, line);
        else
          snprintf(lines[count], FIRST_USE_LINE_BYTES, "%.*s", (int)length, line);
        ++count;
        first = false;
        line += length;
      }
    }
    line = *end ? end + 1 : end;
  }
  return count;
}

static void render_first_use(terminal_size_t size, const notice_snapshot_t *notice) {
  int width = size.cols - 6;
  if (width > FIRST_USE_CONTENT_COLUMNS)
    width = FIRST_USE_CONTENT_COLUMNS;
  if (width < 4 || size.rows < 8)
    return;
  char lines[FIRST_USE_LINE_COUNT][FIRST_USE_LINE_BYTES];
  size_t count = first_use_wrap(notice->text, width, lines);
  if (!count)
    return;
  int page_rows = size.rows - 7;
  int pages = (int)((count + (size_t)page_rows - 1) / (size_t)page_rows);
  notice_snapshot_t *state = (notice_snapshot_t *)notice;
  if (state->rendered_cols != size.cols || state->rendered_rows != size.rows) {
    atomic_store(&state->presented_page, -1);
    state->rendered_cols = size.cols;
    state->rendered_rows = size.rows;
  }
  atomic_store(&state->pages, pages);
  int page = atomic_load(&state->page);
  if (page >= pages) {
    page = pages - 1;
    atomic_store(&state->page, page);
  }
  const char *visible[FIRST_USE_LINE_COUNT];
  size_t start = (size_t)page * (size_t)page_rows;
  size_t visible_count = count - start;
  if (visible_count > (size_t)page_rows)
    visible_count = (size_t)page_rows;
  for (size_t i = 0; i < visible_count; ++i)
    visible[i] = lines[start + i];
  char enter[128], escape[128], footer[512];
  ui_help_keys(enter, sizeof(enter), "Enter");
  ui_help_keys(escape, sizeof(escape), "Esc");
  snprintf(footer, sizeof(footer), "%d/%d %s %s %s%s", page + 1, pages, enter, page + 1 == pages ? "done" : "next",
           escape, width >= 25 ? " close" : "");
  ui_popup_t popup = {.title = notice->text + notice->title_offset,
                      .lines = visible,
                      .line_count = visible_count,
                      .footer = footer,
                      .minimum_width = width + 4};
  frame_buffer_t *output = frame_buffer_create(size.rows, size.cols);
  if (!output)
    return;
  frame_buffer_append(output, "\033[H\033[2J", 7);
  if (ui_popup_render(output, size, &popup)) {
    state->rendered_page = page;
    ui_controller_write(STDERR_FILENO, frame_buffer_get_content(output), frame_buffer_get_length(output));
  }
  frame_buffer_destroy(output);
}

static void render_notice(terminal_size_t size, const void *data) {
  const notice_snapshot_t *notice = data;
  if (notice->marker_offset) {
    render_first_use(size, notice);
    return;
  }
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
  if (notice->color) {
    ui_controller_write(STDERR_FILENO, "\033[1m", 4);
    ui_controller_write(STDERR_FILENO, notice_color(notice->severity), strlen(notice_color(notice->severity)));
  }
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
  bool unicode = tty && terminal_should_use_utf8();
  if (severity != UI_NOTICE_FATAL && tty && ui_controller_is_presenting() && !ui_controller_is_owner() &&
      !GET_OPTION(snapshot_mode)) {
    size_t text_bytes = strlen(text) + 1;
    size_t bytes = sizeof(notice_snapshot_t) + text_bytes;
    notice_snapshot_t *snapshot = SAFE_MALLOC(bytes, notice_snapshot_t *);
    if (!snapshot)
      return SET_ERRNO(ERROR_MEMORY, "Cannot allocate notice");
    snapshot->severity = severity;
    snapshot->unicode = unicode;
    snapshot->color = color;
    snapshot->started = time_get_ns();
    snapshot->marker_offset = snapshot->title_offset = 0;
    atomic_init(&snapshot->page, 0);
    atomic_init(&snapshot->pages, 0);
    atomic_init(&snapshot->presented_page, -1);
    snapshot->rendered_page = -1;
    snapshot->rendered_cols = snapshot->rendered_rows = 0;
    memcpy(snapshot->text, text, text_bytes);
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

asciichat_error_t ui_notice_present_first_use(const char *title, const char *text, const char *marker_path) {
  if (!title || !text || !marker_path || !platform_isatty(STDERR_FILENO) || GET_OPTION(snapshot_mode))
    return SET_ERRNO(ERROR_INVALID_PARAM, "First-use notice requires an interactive terminal and marker path");
  size_t text_bytes = strlen(text) + 1, title_bytes = strlen(title) + 1, marker_bytes = strlen(marker_path) + 1;
  if (title_bytes > SIZE_MAX - marker_bytes - sizeof(notice_snapshot_t) ||
      text_bytes > SIZE_MAX - title_bytes - marker_bytes - sizeof(notice_snapshot_t))
    return SET_ERRNO(ERROR_BUFFER_OVERFLOW, "First-use notice is too large");
  size_t bytes = sizeof(notice_snapshot_t) + text_bytes + title_bytes + marker_bytes;
  notice_snapshot_t *snapshot = SAFE_MALLOC(bytes, notice_snapshot_t *);
  if (!snapshot)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate first-use notice");
  snapshot->severity = UI_NOTICE_INFO;
  snapshot->unicode = terminal_should_use_utf8();
  snapshot->color = !GET_OPTION(strip_ansi) && terminal_should_color_output(STDERR_FILENO);
  snapshot->started = time_get_ns();
  snapshot->title_offset = text_bytes;
  snapshot->marker_offset = text_bytes + title_bytes;
  atomic_init(&snapshot->page, 0);
  atomic_init(&snapshot->pages, 0);
  atomic_init(&snapshot->presented_page, -1);
  snapshot->rendered_page = -1;
  snapshot->rendered_cols = snapshot->rendered_rows = 0;
  memcpy(snapshot->text, text, text_bytes);
  memcpy(snapshot->text + text_bytes, title, title_bytes);
  memcpy(snapshot->text + snapshot->marker_offset, marker_path, marker_bytes);
  asciichat_error_t result = ui_controller_submit(
      UI_SCREEN_NOTICE, STDERR_FILENO, (terminal_size_t){.cols = 20, .rows = 8}, render_notice, snapshot, bytes);
  SAFE_FREE(snapshot);
  return result;
}

void ui_notice_frame_presented(const void *data) {
  notice_snapshot_t *notice = (notice_snapshot_t *)data;
  if (notice && notice->marker_offset)
    atomic_store(&notice->presented_page, notice->rendered_page);
}

ui_notice_key_result_t ui_notice_first_use_key(const void *data, int key) {
  notice_snapshot_t *notice = (notice_snapshot_t *)data;
  if (!notice || !notice->marker_offset)
    return UI_NOTICE_KEY_ORDINARY;
  if (key == KEY_ESCAPE || key == 'q' || key == 3)
    return UI_NOTICE_KEY_DISMISS;
  if (key == KEY_QUESTION)
    return UI_NOTICE_KEY_HELP;
  int page = atomic_load(&notice->page);
  int pages = atomic_load(&notice->pages);
  if (key == KEY_LEFT && page > 0) {
    atomic_store(&notice->presented_page, -1);
    atomic_store(&notice->page, page - 1);
  } else if ((key == '\r' || key == '\n' || key == KEY_RIGHT) && pages > 0 &&
             atomic_load(&notice->presented_page) == page) {
    if (page + 1 < pages) {
      atomic_store(&notice->presented_page, -1);
      atomic_store(&notice->page, page + 1);
    } else if (key == '\r' || key == '\n') {
      return UI_NOTICE_KEY_COMPLETE;
    }
  }
  return UI_NOTICE_KEY_HANDLED;
}

void ui_notice_first_use_complete(const void *data) {
  const notice_snapshot_t *notice = data;
  if (!notice || !notice->marker_offset)
    return;
  asciichat_errno_scope_t scope = errno_scope_begin();
  FILE *marker = platform_fopen("first-use instructions", notice->text + notice->marker_offset, "w");
  if (marker)
    fclose(marker);
  errno_scope_end(scope, ASCIICHAT_ERRNO_DISMISSED);
}

bool ui_notice_confirm(ui_notice_severity_t severity, const char *text, unsigned timeout_seconds) {
  return ui_notice_confirm_kind(UI_PROMPT_CONFIRMATION, severity, text, timeout_seconds);
}

bool ui_notice_confirm_kind(ui_prompt_kind_t kind, ui_notice_severity_t severity, const char *text,
                            unsigned timeout_seconds) {
  ui_prompt_kind_t previous_kind = g_prompt_kind;
  g_prompt_kind = kind;
  ui_notice_severity_t previous = g_prompt_severity;
  g_prompt_severity = severity;
  if (terminal_can_prompt_user())
    log_msg(severity == UI_NOTICE_FATAL     ? LOG_FATAL
            : severity == UI_NOTICE_DANGER  ? LOG_ERROR
            : severity == UI_NOTICE_WARNING ? LOG_WARN
                                            : LOG_INFO,
            __FILE__, __LINE__, __func__, "%s", text);
  else
    ui_notice_log(severity, __FILE__, __LINE__, __func__, "CONFIRMATION REQUIRED", "%s", text);
  bool accepted = platform_prompt_yes_no_timeout(text, false, timeout_seconds);
  g_prompt_kind = previous_kind;
  g_prompt_severity = previous;
  return accepted;
}

ui_prompt_kind_t ui_notice_prompt_kind(void) {
  return g_prompt_kind;
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
