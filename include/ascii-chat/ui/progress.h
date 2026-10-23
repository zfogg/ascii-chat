#pragma once
#include <stdbool.h>
#include <ascii-chat/ui/frame_buffer.h>
#include <stdint.h>
/** Caller-owned ASCII/UTF-8 bar, styled using the active theme. */
void ui_progress_format(char *output, size_t capacity, double fraction, int width);
const char *ui_spinner_glyph(uint64_t elapsed_ns, bool use_colors);
/** Eight-dot animated indicator extracted from the update checker. */
void ui_spinner_render(frame_buffer_t *output, int row, int column, uint64_t elapsed_ns);
