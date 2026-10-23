#include <ascii-chat/ui/help_menu.h>
#include <ascii-chat/ui/text.h>
#include <ascii-chat/util/display.h>
#include <ascii-chat/util/string.h>
#include <stdio.h>
#include <string.h>

void ui_help_keys(char *out, size_t cap, const char *keys) {
  if (!out || !cap) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Help keys require output storage");
    return;
  }
  size_t used = 0;
  out[0] = '\0';
  if (!keys)
    return;
  for (const char *p = keys; *p;) {
    if (used + 1 >= cap) {
      SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Help key output buffer is too small");
      return;
    }
    if (*p == ' ' || *p == '/' || *p == '|' || *p == '(' || *p == ')') {
      out[used++] = *p++;
      out[used] = '\0';
    } else {
      const char *start = p;
      while (*p && *p != ' ' && *p != '/' && *p != '|' && *p != '(' && *p != ')')
        ++p;
      char key[96];
      if ((size_t)(p - start) >= sizeof(key)) {
        SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Help key token is too long");
        return;
      }
      snprintf(key, sizeof(key), "%.*s", (int)(p - start), start);
      const char *styled = colored_string(LOG_COLOR_WARN, key);
      size_t length = strlen(styled);
      if (length >= cap - used) {
        SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Help key output buffer is too small");
        return;
      }
      memcpy(out + used, styled, length + 1);
      used += length;
    }
  }
}

static void measure(const ui_help_menu_t *m, int *keys, int *left, int *right) {
  *keys = *left = *right = 0;
  for (size_t i = 0; i < m->count; ++i) {
    int k = m->entries[i].keys ? display_width(m->entries[i].keys) : 0;
    if (k > *keys)
      *keys = k;
  }
  for (size_t i = 0; i < m->count; ++i) {
    int w = display_width(m->entries[i].description);
    if (m->entries[i].keys)
      w += *keys + 2;
    if (w > *left)
      *left = w;
  }
  for (size_t i = 0; i < m->settings_count; ++i) {
    int w = display_width(m->settings[i]);
    if (w > *right)
      *right = w;
  }
}

terminal_size_t ui_help_menu_measure(const ui_help_menu_t *m) {
  if (!m || (m->count && !m->entries) || (m->settings_count && !m->settings)) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid help menu entries or settings");
    return (terminal_size_t){0};
  }
  int keys, left, right;
  measure(m, &keys, &left, &right);
  int width = left + (right ? right + 3 : 0);
  if (m->title && display_width(m->title) > width)
    width = display_width(m->title);
  if (m->footer && display_width(m->footer) > width)
    width = display_width(m->footer);
  size_t rows = m->count > m->settings_count ? m->count : m->settings_count;
  return (terminal_size_t){.cols = width + 6, .rows = (int)rows + 8 + m->top_margin};
}

bool ui_help_menu_render(frame_buffer_t *out, terminal_size_t size, const ui_help_menu_t *m) {
  if (!out || !m || (m->count && !m->entries) || (m->settings_count && !m->settings) || m->count > 62 ||
      m->settings_count > 62) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid help output or entry counts");
    return false;
  }
  char text[64][2048];
  const char *lines[64];
  size_t rows = m->count > m->settings_count ? m->count : m->settings_count;
  int keys, left, right;
  measure(m, &keys, &left, &right);
  lines[0] = "";
  for (size_t i = 0; i < rows; ++i) {
    char lhs[1024] = "", rhs[1024] = "", padded[2048];
    if (i < m->count) {
      const ui_help_entry_t *entry = &m->entries[i];
      if (!entry->keys)
        snprintf(lhs, sizeof(lhs), "%s", entry->description);
      else {
        char key[256], description[768];
        char styled_keys[512];
        ui_help_keys(styled_keys, sizeof(styled_keys), entry->keys);
        ui_text_cell(key, sizeof(key), styled_keys, keys, UI_ALIGN_LEFT);
        ui_text_values_style(description, sizeof(description), entry->description, LOG_COLOR_FATAL);
        snprintf(lhs, sizeof(lhs), "%s  %s", key, description);
      }
    }
    if (i < m->settings_count) {
      const char *setting = m->settings[i];
      const char *end = setting[0] == '(' ? strchr(setting, ')') : NULL;
      if (end) {
        char binding[64], styled[512];
        snprintf(binding, sizeof(binding), "%.*s", (int)(end + 1 - setting), setting);
        ui_help_keys(styled, sizeof(styled), binding);
        char values[512];
        ui_text_values_style(values, sizeof(values), end + 1, LOG_COLOR_FATAL);
        snprintf(rhs, sizeof(rhs), "%s%s", styled, values);
      } else
        ui_text_values_style(rhs, sizeof(rhs), setting, LOG_COLOR_FATAL);
    }
    ui_text_cell(padded, sizeof(padded), lhs, left, UI_ALIGN_LEFT);
    snprintf(text[i], sizeof(text[i]), "%s%s%s", padded, right ? "   " : "", rhs);
    lines[i + 1] = text[i];
  }
  ui_popup_t box = {
      .title = m->title, .lines = lines, .line_count = rows + 1, .footer = m->footer, .top_margin = m->top_margin};
  return ui_popup_render(out, size, &box);
}
