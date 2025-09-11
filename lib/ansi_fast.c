#include "common.h"
#include "image2ascii/simd/ascii_simd.h"
#include "ansi_fast.h"
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <limits.h>

// 256-color lookup table (optional)
static char color256_strings[256][16]; // Pre-built SGR strings like "\033[38;5;123m"
static bool color256_initialized = false;

// Fast foreground color: \033[38;2;R;G;Bm
char *append_truecolor_fg(char *dst, uint8_t r, uint8_t g, uint8_t b) {
  // Static prefix - 7 bytes
  memcpy(dst, "\033[38;2;", 7);
  dst += 7;

  // Red component + semicolon
  memcpy(dst, g_dec3_cache.dec3_table[r].s, g_dec3_cache.dec3_table[r].len);
  dst += g_dec3_cache.dec3_table[r].len;
  *dst++ = ';';

  // Green component + semicolon
  memcpy(dst, g_dec3_cache.dec3_table[g].s, g_dec3_cache.dec3_table[g].len);
  dst += g_dec3_cache.dec3_table[g].len;
  *dst++ = ';';

  // Blue component + suffix
  memcpy(dst, g_dec3_cache.dec3_table[b].s, g_dec3_cache.dec3_table[b].len);
  dst += g_dec3_cache.dec3_table[b].len;
  *dst++ = 'm';

  return dst;
}

// Fast background color: \033[48;2;R;G;Bm
char *append_truecolor_bg(char *dst, uint8_t r, uint8_t g, uint8_t b) {
  memcpy(dst, "\033[48;2;", 7);
  dst += 7;

  memcpy(dst, g_dec3_cache.dec3_table[r].s, g_dec3_cache.dec3_table[r].len);
  dst += g_dec3_cache.dec3_table[r].len;
  *dst++ = ';';

  memcpy(dst, g_dec3_cache.dec3_table[g].s, g_dec3_cache.dec3_table[g].len);
  dst += g_dec3_cache.dec3_table[g].len;
  *dst++ = ';';

  memcpy(dst, g_dec3_cache.dec3_table[b].s, g_dec3_cache.dec3_table[b].len);
  dst += g_dec3_cache.dec3_table[b].len;
  *dst++ = 'm';

  return dst;
}

// Combined foreground + background: \033[38;2;R;G;B;48;2;r;g;bm
char *append_truecolor_fg_bg(char *dst, uint8_t fg_r, uint8_t fg_g, uint8_t fg_b, uint8_t bg_r, uint8_t bg_g,
                             uint8_t bg_b) {
  memcpy(dst, "\033[38;2;", 7);
  dst += 7;

  // Foreground RGB
  memcpy(dst, g_dec3_cache.dec3_table[fg_r].s, g_dec3_cache.dec3_table[fg_r].len);
  dst += g_dec3_cache.dec3_table[fg_r].len;
  *dst++ = ';';

  memcpy(dst, g_dec3_cache.dec3_table[fg_g].s, g_dec3_cache.dec3_table[fg_g].len);
  dst += g_dec3_cache.dec3_table[fg_g].len;
  *dst++ = ';';

  memcpy(dst, g_dec3_cache.dec3_table[fg_b].s, g_dec3_cache.dec3_table[fg_b].len);
  dst += g_dec3_cache.dec3_table[fg_b].len;

  // Background RGB
  memcpy(dst, ";48;2;", 6);
  dst += 6;

  memcpy(dst, g_dec3_cache.dec3_table[bg_r].s, g_dec3_cache.dec3_table[bg_r].len);
  dst += g_dec3_cache.dec3_table[bg_r].len;
  *dst++ = ';';

  memcpy(dst, g_dec3_cache.dec3_table[bg_g].s, g_dec3_cache.dec3_table[bg_g].len);
  dst += g_dec3_cache.dec3_table[bg_g].len;
  *dst++ = ';';

  memcpy(dst, g_dec3_cache.dec3_table[bg_b].s, g_dec3_cache.dec3_table[bg_b].len);
  dst += g_dec3_cache.dec3_table[bg_b].len;
  *dst++ = 'm';

  return dst;
}

// Initialize run-length encoding context
void ansi_rle_init(ansi_rle_context_t *ctx, char *buffer, size_t capacity, ansi_color_mode_t mode) {
  ctx->buffer = buffer;
  ctx->capacity = capacity;
  ctx->length = 0;
  ctx->mode = mode;
  ctx->first_pixel = true;
  // Initialize with impossible color values to force first SGR
  ctx->last_r = 0xFF;
  ctx->last_g = 0xFF;
  ctx->last_b = 0xFF;
}

// Add pixel with run-length encoding - only emit SGR when color changes
void ansi_rle_add_pixel(ansi_rle_context_t *ctx, uint8_t r, uint8_t g, uint8_t b, char ascii_char) {
  // Check if we need to emit a new SGR sequence
  bool color_changed = ctx->first_pixel || (r != ctx->last_r) || (g != ctx->last_g) || (b != ctx->last_b);

  if (color_changed && (ctx->length + 32 < ctx->capacity)) { // Reserve space for SGR
    char *pos = ctx->buffer + ctx->length;

    switch (ctx->mode) {
    case ANSI_MODE_FOREGROUND:
      pos = append_truecolor_fg(pos, r, g, b);
      break;
    case ANSI_MODE_BACKGROUND:
      pos = append_truecolor_bg(pos, r, g, b);
      break;
    case ANSI_MODE_FOREGROUND_BACKGROUND:
      // For FG+BG mode, use a default background (black) or implement dual-color logic
      pos = append_truecolor_fg_bg(pos, r, g, b, 0, 0, 0);
      break;
    }

    ctx->length = pos - ctx->buffer;
    ctx->last_r = r;
    ctx->last_g = g;
    ctx->last_b = b;
    ctx->first_pixel = false;
  }

  // Add the ASCII character
  if (ctx->length < ctx->capacity - 1) {
    ctx->buffer[ctx->length++] = ascii_char;
  }
}

// Finish RLE sequence with reset and null terminator
void ansi_rle_finish(ansi_rle_context_t *ctx) {
  // Add reset sequence
  if (ctx->length + 5 < ctx->capacity) {
    memcpy(ctx->buffer + ctx->length, "\033[0m", 4);
    ctx->length += 4;
  }

  // Null terminate
  if (ctx->length < ctx->capacity) {
    ctx->buffer[ctx->length] = '\0';
  }
}

// 256-color mode initialization (optional high-speed mode)
void ansi_fast_init_256color(void) {
  if (color256_initialized)
    return;

  for (int i = 0; i < 256; i++) {
    snprintf(color256_strings[i], sizeof(color256_strings[i]), "\033[38;5;%dm", i);
  }

  color256_initialized = true;
}

// Fast 256-color foreground
char *append_256color_fg(char *dst, uint8_t color_index) {
  const char *color_str = color256_strings[color_index];
  int len = strlen(color_str);
  memcpy(dst, color_str, len);
  return dst + len;
}

// Convert RGB to closest 256-color palette index
uint8_t rgb_to_256color(uint8_t r, uint8_t g, uint8_t b) {
  // Map to 6x6x6 color cube (216 colors) + grayscale ramp

  // Check if it's close to grayscale
  int avg = (r + g + b) / 3;
  int gray_diff = abs(r - avg) + abs(g - avg) + abs(b - avg);

  if (gray_diff < 30) {
    // Use grayscale ramp (colors 232-255)
    int gray_level = (avg * 23) / 255;
    return 232 + gray_level;
  }

  // Use 6x6x6 color cube (colors 16-231)
  int r6 = (r * 5) / 255;
  int g6 = (g * 5) / 255;
  int b6 = (b * 5) / 255;

  return 16 + (r6 * 36) + (g6 * 6) + b6;
}

// 16-color mode support
static char color16_fg_strings[16][16];
static char color16_bg_strings[16][16];
static bool color16_initialized = false;

void ansi_fast_init_16color(void) {
  if (color16_initialized)
    return;

  // Standard ANSI color codes
  const char *fg_codes[] = {"30", "31", "32", "33", "34", "35", "36", "37",          // Normal colors (30-37)
                            "90", "91", "92", "93", "94", "95", "96", "97"};         // Bright colors (90-97)
  const char *bg_codes[] = {"40",  "41",  "42",  "43",  "44",  "45",  "46",  "47",   // Normal colors (40-47)
                            "100", "101", "102", "103", "104", "105", "106", "107"}; // Bright colors (100-107)

  for (int i = 0; i < 16; i++) {
    snprintf(color16_fg_strings[i], sizeof(color16_fg_strings[i]), "\033[%sm", fg_codes[i]);
    snprintf(color16_bg_strings[i], sizeof(color16_bg_strings[i]), "\033[%sm", bg_codes[i]);
  }

  color16_initialized = true;
}

char *append_16color_fg(char *dst, uint8_t color_index) {
  if (!color16_initialized) {
    ansi_fast_init_16color();
  }

  if (color_index >= 16) {
    color_index = 7; // Default to white
  }

  const char *color_str = color16_fg_strings[color_index];
  while (*color_str) {
    *dst++ = *color_str++;
  }

  return dst;
}

char *append_16color_bg(char *dst, uint8_t color_index) {
  if (!color16_initialized) {
    ansi_fast_init_16color();
  }

  if (color_index >= 16) {
    color_index = 0; // Default to black background
  }

  const char *color_str = color16_bg_strings[color_index];
  while (*color_str) {
    *dst++ = *color_str++;
  }

  return dst;
}

uint8_t rgb_to_16color(uint8_t r, uint8_t g, uint8_t b) {
  // Convert RGB to the closest 16-color ANSI color
  // This uses a simple distance-based approach

  // Define the 16 ANSI colors in RGB
  static const uint8_t ansi_colors[16][3] = {
      {0, 0, 0},       // 0: Black
      {128, 0, 0},     // 1: Dark Red
      {0, 128, 0},     // 2: Dark Green
      {128, 128, 0},   // 3: Dark Yellow (Brown)
      {0, 0, 128},     // 4: Dark Blue
      {128, 0, 128},   // 5: Dark Magenta
      {0, 128, 128},   // 6: Dark Cyan
      {192, 192, 192}, // 7: Light Gray
      {128, 128, 128}, // 8: Dark Gray
      {255, 0, 0},     // 9: Bright Red
      {0, 255, 0},     // 10: Bright Green
      {255, 255, 0},   // 11: Bright Yellow
      {0, 0, 255},     // 12: Bright Blue
      {255, 0, 255},   // 13: Bright Magenta
      {0, 255, 255},   // 14: Bright Cyan
      {255, 255, 255}  // 15: White
  };

  int best_match = 0;
  int min_distance = INT_MAX;

  for (int i = 0; i < 16; i++) {
    int dr = (int)r - (int)ansi_colors[i][0];
    int dg = (int)g - (int)ansi_colors[i][1];
    int db = (int)b - (int)ansi_colors[i][2];
    int distance = dr * dr + dg * dg + db * db;

    if (distance < min_distance) {
      min_distance = distance;
      best_match = i;
    }
  }

  return best_match;
}

// Get the actual RGB values for a 16-color ANSI index
void get_16color_rgb(uint8_t color_index, uint8_t *r, uint8_t *g, uint8_t *b) {
  // Same color table as rgb_to_16color()
  static const uint8_t ansi_colors[16][3] = {
      {0, 0, 0},       // 0: Black
      {128, 0, 0},     // 1: Dark Red
      {0, 128, 0},     // 2: Dark Green
      {128, 128, 0},   // 3: Dark Yellow (Brown)
      {0, 0, 128},     // 4: Dark Blue
      {128, 0, 128},   // 5: Dark Magenta
      {0, 128, 128},   // 6: Dark Cyan
      {192, 192, 192}, // 7: Light Gray
      {128, 128, 128}, // 8: Dark Gray
      {255, 0, 0},     // 9: Bright Red
      {0, 255, 0},     // 10: Bright Green
      {255, 255, 0},   // 11: Bright Yellow
      {0, 0, 255},     // 12: Bright Blue
      {255, 0, 255},   // 13: Bright Magenta
      {0, 255, 255},   // 14: Bright Cyan
      {255, 255, 255}  // 15: White
  };

  if (color_index >= 16) {
    color_index = 7; // Default to light gray
  }

  *r = ansi_colors[color_index][0];
  *g = ansi_colors[color_index][1];
  *b = ansi_colors[color_index][2];
}

// Floyd-Steinberg dithering for 16-color terminals
uint8_t rgb_to_16color_dithered(int r, int g, int b, int x, int y, int width, int height, rgb_error_t *error_buffer) {
  // Add accumulated error from previous pixels
  if (error_buffer) {
    int error_idx = y * width + x;
    r += error_buffer[error_idx].r;
    g += error_buffer[error_idx].g;
    b += error_buffer[error_idx].b;

    // Reset error for this pixel
    error_buffer[error_idx].r = 0;
    error_buffer[error_idx].g = 0;
    error_buffer[error_idx].b = 0;
  }

  // Clamp values to [0, 255]
  r = (r < 0) ? 0 : (r > 255) ? 255 : r;
  g = (g < 0) ? 0 : (g > 255) ? 255 : g;
  b = (b < 0) ? 0 : (b > 255) ? 255 : b;

  // Find the closest 16-color match
  uint8_t closest_color = rgb_to_16color((uint8_t)r, (uint8_t)g, (uint8_t)b);

  // Calculate quantization error if dithering is enabled
  if (error_buffer) {
    uint8_t actual_r, actual_g, actual_b;
    get_16color_rgb(closest_color, &actual_r, &actual_g, &actual_b);

    int error_r = r - (int)actual_r;
    int error_g = g - (int)actual_g;
    int error_b = b - (int)actual_b;

    // Distribute error using Floyd-Steinberg weights:
    //     * 7/16
    // 3/16 5/16 1/16

    // Error to right pixel (x+1, y)
    if (x + 1 < width) {
      int right_idx = y * width + (x + 1);
      error_buffer[right_idx].r += (error_r * 7) / 16;
      error_buffer[right_idx].g += (error_g * 7) / 16;
      error_buffer[right_idx].b += (error_b * 7) / 16;
    }

    // Error to pixels on next row (y+1)
    if (y + 1 < height) {
      // Bottom-left pixel (x-1, y+1)
      if (x - 1 >= 0) {
        int bl_idx = (y + 1) * width + (x - 1);
        error_buffer[bl_idx].r += (error_r * 3) / 16;
        error_buffer[bl_idx].g += (error_g * 3) / 16;
        error_buffer[bl_idx].b += (error_b * 3) / 16;
      }

      // Bottom pixel (x, y+1)
      int bottom_idx = (y + 1) * width + x;
      error_buffer[bottom_idx].r += (error_r * 5) / 16;
      error_buffer[bottom_idx].g += (error_g * 5) / 16;
      error_buffer[bottom_idx].b += (error_b * 5) / 16;

      // Bottom-right pixel (x+1, y+1)
      if (x + 1 < width) {
        int br_idx = (y + 1) * width + (x + 1);
        error_buffer[br_idx].r += (error_r * 1) / 16;
        error_buffer[br_idx].g += (error_g * 1) / 16;
        error_buffer[br_idx].b += (error_b * 1) / 16;
      }
    }
  }

  return closest_color;
}

// Terminal capability-aware color function
char *append_color_fg_for_mode(char *dst, uint8_t r, uint8_t g, uint8_t b, color_mode_t mode) {
  switch (mode) {
  case COLOR_MODE_TRUECOLOR:
    return append_truecolor_fg(dst, r, g, b);

  case COLOR_MODE_256_COLOR: {
    uint8_t color_index = rgb_to_256color(r, g, b);
    return append_256color_fg(dst, color_index);
  }

  case COLOR_MODE_16_COLOR: {
    uint8_t color_index = rgb_to_16color(r, g, b);
    return append_16color_fg(dst, color_index);
  }

  case COLOR_MODE_MONO:
  case COLOR_MODE_AUTO:
  default:
    // No color output for monochrome mode or auto mode (fallback)
    return dst;
  }
}
