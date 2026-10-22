#include <ascii-chat/ui/errno_stacks.h>
#include <ascii-chat/ui/controller.h>
#include <ascii-chat/ui/frame_buffer.h>
#include <ascii-chat/ui/selectable_list.h>
#include <ascii-chat/ui/too_small.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/common/error_codes.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/util/display.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/lifecycle.h>
#include <string.h>
#include <stdlib.h>

#define ERRNO_TUI_MIN_COLS 88
#define ERRNO_TUI_MIN_ROWS 10
#define ERRNO_TUI_MAX_ROWS (ASCIICHAT_ERRNO_MAX_FRAMES + ASCIICHAT_ERRNO_HISTORY_SIZE)

typedef enum { ERRNO_TUI_CURRENT, ERRNO_TUI_HISTORY } errno_tui_tab_t;
typedef struct {
  asciichat_error_context_t context;
  uint64_t resolved_ns;
  uint64_t group_id;
  uint64_t group_timestamp;
  asciichat_errno_outcome_t outcome;
  bool historical;
} errno_tui_row_t;
typedef struct {
  bool visible;
  bool stack_sort;
  errno_tui_tab_t tab;
  ui_selectable_list_t list;
  int rows;
  int count;
  int output_fd;
  asciichat_error_context_t selected_context;
  bool selected_current;
} errno_tui_state_t;

static errno_tui_state_t g_state;
static mutex_t g_mutex;
static lifecycle_t g_lifecycle = LIFECYCLE_INIT;

static bool state_lock(void) {
  if (lifecycle_init_once(&g_lifecycle)) {
    if (mutex_init(&g_mutex, "errno_tui") != 0) {
      lifecycle_init_abort(&g_lifecycle);
      return false;
    }
    lifecycle_init_commit(&g_lifecycle);
  }
  if (!lifecycle_is_initialized(&g_lifecycle))
    return false;
  mutex_lock(&g_mutex);
  return true;
}

static void state_unlock(void) {
  mutex_unlock(&g_mutex);
}

static int by_created_desc(const void *left, const void *right) {
  const errno_tui_row_t *a = left, *b = right;
  return a->context.created_ns < b->context.created_ns ? 1 : a->context.created_ns > b->context.created_ns ? -1 : 0;
}

static int by_resolved_desc(const void *left, const void *right) {
  const errno_tui_row_t *a = left, *b = right;
  return a->resolved_ns < b->resolved_ns ? 1 : a->resolved_ns > b->resolved_ns ? -1 : 0;
}

static int by_stack_desc(const void *left, const void *right) {
  const errno_tui_row_t *a = left, *b = right;
  if (a->group_id != b->group_id)
    return a->group_timestamp < b->group_timestamp ? 1 : a->group_timestamp > b->group_timestamp ? -1 : 0;
  uint64_t at = a->historical ? a->resolved_ns : a->context.created_ns;
  uint64_t bt = b->historical ? b->resolved_ns : b->context.created_ns;
  return at < bt ? 1 : at > bt ? -1 : 0;
}

static size_t snapshot_current(errno_tui_row_t *rows, bool stack_sort, size_t capacity, size_t *stacks) {
  uint64_t threads[ASCIICHAT_ERRNO_MAX_THREADS];
  size_t thread_count = errno_threads_snapshot(threads, ASCIICHAT_ERRNO_MAX_THREADS);
  size_t count = 0;
  *stacks = 0;
  for (size_t t = 0; t < thread_count && t < ASCIICHAT_ERRNO_MAX_THREADS; ++t) {
    asciichat_error_context_t frames[ASCIICHAT_ERRNO_MAX_DEPTH];
    size_t depth = errno_snapshot(threads[t], frames, ASCIICHAT_ERRNO_MAX_DEPTH);
    if (!depth)
      continue;
    ++*stacks;
    for (size_t i = 0; i < depth && i < ASCIICHAT_ERRNO_MAX_DEPTH && count < capacity; ++i) {
      rows[count++] =
          (errno_tui_row_t){.context = frames[i], .group_id = threads[t], .group_timestamp = frames[0].created_ns};
    }
  }
  if (!stack_sort)
    qsort(rows, count, sizeof(*rows), by_created_desc);
  else {
    /* The newest frame in each per-thread snapshot orders the group; frames remain together. */
    qsort(rows, count, sizeof(*rows), by_stack_desc);
  }
  return count;
}

static size_t snapshot_history(errno_tui_row_t *rows, bool stack_sort, size_t capacity) {
  asciichat_errno_history_t *history =
      SAFE_CALLOC(ASCIICHAT_ERRNO_HISTORY_SIZE, sizeof(*history), asciichat_errno_history_t *);
  if (!history)
    return 0;
  size_t total = errno_history_snapshot(history, ASCIICHAT_ERRNO_HISTORY_SIZE);
  size_t count = total < capacity ? total : capacity;
  for (size_t i = 0; i < count; ++i) {
    rows[i] = (errno_tui_row_t){.context = history[i].context,
                                .resolved_ns = history[i].resolved_ns,
                                .outcome = history[i].outcome,
                                .historical = true,
                                .group_id = history[i].context.cause_id ? history[i].context.cause_id
                                                                        : history[i].context.error_id};
  }
  for (size_t i = 0; i < count; ++i) {
    uint64_t group = rows[i].group_id;
    for (size_t depth = 0; depth < count; ++depth) {
      bool found_parent = false;
      for (size_t j = 0; j < count; ++j) {
        if (rows[j].context.error_id == group && rows[j].context.cause_id) {
          group = rows[j].context.cause_id;
          found_parent = true;
          break;
        }
      }
      if (!found_parent)
        break;
    }
    rows[i].group_id = group;
    rows[i].group_timestamp = rows[i].resolved_ns;
    if (stack_sort)
      for (size_t j = 0; j < count; ++j)
        if (rows[j].group_id == group && rows[j].resolved_ns > rows[i].group_timestamp)
          rows[i].group_timestamp = rows[j].resolved_ns;
  }
  qsort(rows, count, sizeof(*rows), stack_sort ? by_stack_desc : by_resolved_desc);
  SAFE_FREE(history);
  return count;
}

static void outcome_counts(const asciichat_errno_history_t *history, size_t count, unsigned code, unsigned *resolved,
                           unsigned *dismissed, unsigned *handled) {
  *resolved = 0;
  *handled = 0;
  *dismissed = 0;
  for (size_t i = 0; i < count && i < ASCIICHAT_ERRNO_HISTORY_SIZE; ++i) {
    if ((unsigned)history[i].context.code != code)
      continue;
    ++*resolved;
    if (history[i].outcome == ASCIICHAT_ERRNO_HANDLED)
      ++*handled;
    else if (history[i].outcome == ASCIICHAT_ERRNO_DISMISSED)
      ++*dismissed;
  }
}

/** Copy the registered thread label without retaining registry-owned memory. */
static void format_thread_name(uint64_t thread_id, char *output, size_t output_size) {
  named_metadata_t metadata = {0};
  if (named_metadata_copy((uintptr_t)thread_id, &metadata) && metadata.name[0]) {
    safe_snprintf(output, output_size, "%s", metadata.name);
    return;
  }
  safe_snprintf(output, output_size, "0x%llx", (unsigned long long)thread_id);
}

static void put_color(frame_buffer_t *buffer, log_color_t color, bool enabled) {
  frame_buffer_printf(buffer, "%s", log_level_color_for_output(color, enabled));
}

static size_t duration_number_length(const char *duration) {
  size_t length = 0;
  while (duration[length] && ((duration[length] >= '0' && duration[length] <= '9') || duration[length] == '.'))
    ++length;
  return length;
}

static void render_footer(frame_buffer_t *buffer, int width, bool colors) {
  const char *orange = log_level_color_for_output(LOG_COLOR_DEV, colors);
  const char *grey = log_level_color_for_output(LOG_COLOR_GREY, colors);
  const char *reset = log_level_color_for_output(LOG_COLOR_RESET, colors);
  char footer[512];
  safe_snprintf(
      footer, sizeof(footer),
      "%sTab%s%s: tab  %ss%s%s: sort  %sUp%s/%sk%s/%sDown%s/%sj%s%s: move  %sx%s%s: clear  %s`%s/%sEsc%s%s: close",
      orange, reset, grey, orange, reset, grey, orange, reset, orange, reset, orange, reset, orange, reset, grey,
      orange, reset, grey, orange, reset, orange, reset, grey);
  frame_buffer_render_centered(buffer, footer, width);
}

static void render_table_line(frame_buffer_t *buffer, const char *line, int width) {
  int table_width = width > 118 ? 114 : width - 4;
  int left_padding = (width - table_width) / 2;
  frame_buffer_printf(buffer, "%*s%s\n", left_padding, "", line);
}

/** Render the active errno view from fresh current and historical snapshots. */
static void render_errno_stacks(terminal_size_t size, const void *unused) {
  (void)unused;
  errno_tui_state_t state;
  if (!state_lock())
    return;
  state = g_state;
  state_unlock();

  frame_buffer_t *buffer = frame_buffer_create(size.rows, size.cols);
  if (!buffer)
    return;
  frame_buffer_append(buffer, "\033[0m", 4);
  frame_buffer_clear_screen(buffer);
  const terminal_size_t minimum = {.cols = ERRNO_TUI_MIN_COLS, .rows = ERRNO_TUI_MIN_ROWS};
  int output_fd = state.output_fd >= 0 ? state.output_fd : STDOUT_FILENO;
  if (ui_too_small(size, minimum)) {
    ui_too_small_render(buffer, size, minimum);
    ui_controller_write(output_fd, frame_buffer_get_content(buffer), frame_buffer_get_length(buffer));
    frame_buffer_destroy(buffer);
    return;
  }

  bool colors = !GET_OPTION(strip_ansi) && terminal_should_color_output(output_fd);
  errno_tui_row_t *rows = SAFE_CALLOC(ERRNO_TUI_MAX_ROWS, sizeof(*rows), errno_tui_row_t *);
  errno_tui_row_t *current_rows = SAFE_CALLOC(ERRNO_TUI_MAX_ROWS, sizeof(*current_rows), errno_tui_row_t *);
  if (!rows || !current_rows) {
    SAFE_FREE(rows);
    SAFE_FREE(current_rows);
    frame_buffer_destroy(buffer);
    return;
  }
  size_t stacks = 0;
  size_t current_count = snapshot_current(current_rows, state.stack_sort, ERRNO_TUI_MAX_ROWS, &stacks);
  size_t count = 0;
  if (state.tab == ERRNO_TUI_CURRENT) {
    memcpy(rows, current_rows, current_count * sizeof(*rows));
    count = current_count;
  } else {
    count = snapshot_history(rows, state.stack_sort, ERRNO_TUI_MAX_ROWS);
  }
  int list_rows = size.rows - 7;
  if (list_rows < 1)
    list_rows = 1;
  if (state_lock()) {
    g_state.rows = list_rows;
    g_state.count = (int)count;
    ui_selectable_list_keep_visible(&g_state.list, (int)count, list_rows);
    g_state.selected_current =
        state.tab == ERRNO_TUI_CURRENT && g_state.list.selected >= 0 && g_state.list.selected < (int)count;
    if (g_state.selected_current)
      g_state.selected_context = rows[g_state.list.selected].context;
    state = g_state;
    state_unlock();
  }

  int width = size.cols - 1;
  bool utf8 = utf8_output_enabled();
  frame_buffer_render_divider(buffer, width, utf8, colors);
  asciichat_errno_history_t *history =
      SAFE_CALLOC(ASCIICHAT_ERRNO_HISTORY_SIZE, sizeof(*history), asciichat_errno_history_t *);
  size_t old_count = history ? errno_history_snapshot(history, ASCIICHAT_ERRNO_HISTORY_SIZE) : 0;
  uint64_t latest = 0;
  for (size_t i = 0; i < current_count; ++i)
    if (current_rows[i].context.created_ns > latest)
      latest = current_rows[i].context.created_ns;
  for (size_t i = 0; i < old_count; ++i)
    if (history[i].resolved_ns > latest)
      latest = history[i].resolved_ns;
  char header[768];
  const char *blue = log_level_color_for_output(LOG_COLOR_DEBUG, colors);
  const char *purple = log_level_color_for_output(LOG_COLOR_FATAL, colors);
  const char *reset = log_level_color_for_output(LOG_COLOR_RESET, colors);
  int header_length =
      safe_snprintf(header, sizeof(header), "%serrno stacks%s  errors %s%zu%s  stacks %s%zu%s  old %s%zu%s", blue,
                    reset, purple, current_count, reset, purple, stacks, reset, purple, old_count, reset);
  if (latest && header_length > 0 && (size_t)header_length < sizeof(header)) {
    char ago[32];
    time_pretty(time_get_ns() - latest, -1, ago, sizeof(ago));
    size_t value_length = duration_number_length(ago);
    safe_snprintf(header + header_length, sizeof(header) - (size_t)header_length, "  latest %s%.*s%s%s ago", purple,
                  (int)value_length, ago, reset, ago + value_length);
  }
  frame_buffer_render_centered(buffer, header, width);
  frame_buffer_render_divider(buffer, width, utf8, colors);
  char tabs[256];
  safe_snprintf(tabs, sizeof(tabs), "Current | Past %s sort", state.stack_sort ? "stack" : "recent");
  frame_buffer_render_centered(buffer, tabs, width);
  char columns[256];
  safe_snprintf(columns, sizeof(columns),
                "%sAgo        R/D/H    Code  Thread          Standard message       User message%s", blue, reset);
  render_table_line(buffer, columns, width);
  int table_width = width > 118 ? 114 : width - 4;

  for (int i = 0; i < list_rows; ++i) {
    int index = state.list.first_visible + i;
    if (index >= (int)count) {
      frame_buffer_printf(buffer, "\n");
      continue;
    }
    const errno_tui_row_t *row = &rows[index];
    char ago[32], thread_name[256], thread[256], message[256], user[256];
    uint64_t timestamp = row->historical ? row->resolved_ns : row->context.created_ns;
    time_pretty(time_get_ns() - timestamp, -1, ago, sizeof(ago));
    size_t ago_value_length = duration_number_length(ago);
    int ago_padding = 9 - (int)strlen(ago);
    if (ago_padding < 0)
      ago_padding = 0;
    format_thread_name(row->context.thread_id, thread_name, sizeof(thread_name));
    truncate_with_ellipsis(thread_name, thread, sizeof(thread), 14);
    truncate_with_ellipsis(asciichat_error_string(row->context.code), message, sizeof(message), 23);
    truncate_with_ellipsis(row->context.context_message, user, sizeof(user), table_width - 65);
    int thread_padding = 14 - display_width(thread);
    int message_padding = 23 - display_width(message);
    if (thread_padding < 0)
      thread_padding = 0;
    if (message_padding < 0)
      message_padding = 0;
    unsigned resolved, dismissed, handled;
    outcome_counts(history, old_count, (unsigned)row->context.code, &resolved, &dismissed, &handled);
    char line[768];
    safe_snprintf(line, sizeof(line), "%s%c %s%.*s%s%s%*s %s%u%s/%s%u%s/%s%u%s   %s%-5d%s %s%*s %s%*s %s",
                  index == state.list.selected ? log_level_color_for_output(LOG_COLOR_DEBUG, colors) : "",
                  index == state.list.selected ? '>' : ' ', purple, (int)ago_value_length, ago, reset,
                  ago + ago_value_length, ago_padding, "", purple, resolved, reset, purple, dismissed, reset, purple,
                  handled, reset, purple, row->context.code, reset, thread, thread_padding, "", message, message_padding,
                  "", user);
    render_table_line(buffer, line, width);
  }
  frame_buffer_render_divider(buffer, width, utf8, colors);
  render_footer(buffer, width, colors);
  put_color(buffer, LOG_COLOR_RESET, colors);
  ui_controller_write(output_fd, frame_buffer_get_content(buffer), frame_buffer_get_length(buffer));
  SAFE_FREE(history);
  SAFE_FREE(current_rows);
  SAFE_FREE(rows);
  frame_buffer_destroy(buffer);
}

static void publish(void) {
  const unsigned marker = 1;
  int fd = STDOUT_FILENO;
  if (state_lock()) {
    fd = g_state.output_fd >= 0 ? g_state.output_fd : STDOUT_FILENO;
    state_unlock();
  }
  if (ui_controller_submit(UI_SCREEN_ERRNO, fd, (terminal_size_t){ERRNO_TUI_MIN_COLS, ERRNO_TUI_MIN_ROWS},
                           render_errno_stacks, &marker, sizeof(marker)) != ASCIICHAT_OK)
    CLEAR_ERRNO();
}

void ui_errno_stacks_toggle(void) {
#if defined(ASCIICHAT_DIAGNOSTIC_UI)
  if (!state_lock())
    return;
  g_state.visible = !g_state.visible;
  if (g_state.visible)
    ui_selectable_list_reset(&g_state.list);
  if (g_state.visible)
    g_state.output_fd = ui_controller_active_fd();
  bool visible = g_state.visible;
  state_unlock();
  if (visible)
    publish();
  else
    ui_controller_remove(UI_SCREEN_ERRNO);
#endif
}

void ui_errno_stacks_hide(void) {
  if (!state_lock())
    return;
  g_state.visible = false;
  state_unlock();
  ui_controller_remove(UI_SCREEN_ERRNO);
}

bool ui_errno_stacks_visible(void) {
  if (!state_lock())
    return false;
  bool visible = g_state.visible;
  state_unlock();
  return visible;
}

bool ui_errno_stacks_handle_key(keyboard_key_t key) {
  if (!ui_errno_stacks_visible())
    return false;
  if (!state_lock())
    return false;
  if (key == KEY_BACKTICK || key == KEY_ESCAPE) {
    g_state.visible = false;
    state_unlock();
    ui_controller_remove(UI_SCREEN_ERRNO);
    return true;
  }
  if (key == '\t') {
    g_state.tab = g_state.tab == ERRNO_TUI_CURRENT ? ERRNO_TUI_HISTORY : ERRNO_TUI_CURRENT;
    ui_selectable_list_reset(&g_state.list);
  } else if (key == 's' || key == 'S') {
    g_state.stack_sort = !g_state.stack_sort;
    ui_selectable_list_reset(&g_state.list);
  } else if (key == KEY_UP || key == 'k') {
    ui_selectable_list_move(&g_state.list, g_state.count, g_state.rows, -1);
  } else if (key == KEY_DOWN || key == 'j') {
    ui_selectable_list_move(&g_state.list, g_state.count, g_state.rows, 1);
  } else if (key == 'x' || key == 'X') {
    errno_tui_state_t state = g_state;
    state_unlock();
    if (state.selected_current)
      errno_clear_if_current(state.selected_context.thread_id, state.selected_context.generation,
                             state.selected_context.error_id);
    publish();
    return true;
  } else {
    state_unlock();
    return false;
  }
  state_unlock();
  publish();
  return true;
}
