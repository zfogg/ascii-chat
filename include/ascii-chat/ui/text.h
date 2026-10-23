#pragma once
#include <stddef.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/ui/frame_buffer.h>
typedef enum { UI_ALIGN_LEFT, UI_ALIGN_RIGHT, UI_ALIGN_CENTER } ui_alignment_t;
/** ANSI-aware, UTF-8-safe clipping/padding into caller-owned storage. */
size_t ui_text_cell(char *out, size_t capacity, const char *text, int width, ui_alignment_t alignment);
/** Theme-aware numeric/boolean styling; numeric suffixes retain default color. */
void ui_text_values(char *out, size_t capacity, const char *text);
void ui_text_values_style(char *out, size_t capacity, const char *text, log_color_t zero_color);
void ui_text_render_values(frame_buffer_t *out, const char *text);
/** Theme-aware prefix for incremental formatting, derived from colored_string(). */
const char *ui_text_color_prefix(log_color_t color);
