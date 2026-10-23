#pragma once
#include <ascii-chat/ui/frame_buffer.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/platform/keyboard.h>
#include <stddef.h>
/** UTF-8 cursor movement/deletion in caller-owned single-line storage. */
void ui_input_edit_key(char *buffer, size_t capacity, size_t *length, size_t *cursor, keyboard_key_t key);

/** Render a scrollable UTF-8 field with an active-row marker and visible cursor.
 * Width excludes the two-cell marker. Quoted paths may retain their opening quote. */
asciichat_error_t ui_input_render(frame_buffer_t *output, int row, int column, int width, int height, const char *text,
                                  size_t cursor, bool preserve_quotes);

/** Insert UTF-8 text atomically, retaining storage unchanged on failure. */
asciichat_error_t ui_input_insert(char *buffer, size_t capacity, size_t *length, size_t *cursor, const char *text);
