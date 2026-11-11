#pragma once

#include <stdbool.h>

#define VISUALIZATION_HISTORY 48000
#define MAX_TERMINAL_WIDTH 512
#define MAX_TERMINAL_HEIGHT 256

void visualization_color_for_level(float frequency_hz, float level, int color_mode, unsigned char *red,
                                   unsigned char *green, unsigned char *blue);
char *visualization_encode(const char *cells, const unsigned char *colors, bool column_colors, unsigned int width,
                           unsigned int height, bool use_color, bool flip_x, bool flip_y);
