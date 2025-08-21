#pragma once

#include "ascii_simd.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Fast ANSI escape sequence generation using precomputed lookup tables
// Based on ChatGPT's optimization recommendations

// Access to internal lookup table for testing
extern dec3_t dec3[256];

// Color mode for ANSI generation
typedef enum {
  ANSI_MODE_FOREGROUND,           // \033[38;2;R;G;Bm
  ANSI_MODE_BACKGROUND,           // \033[48;2;R;G;Bm
  ANSI_MODE_FOREGROUND_BACKGROUND // \033[38;2;R;G;B;48;2;r;g;bm
} ansi_color_mode_t;

// Timing breakdown for performance measurement
typedef struct {
  double pixel_time;  // Luminance/ASCII conversion time
  double string_time; // ANSI string generation time
  double output_time; // Terminal write time
  double total_time;  // Overall frame time
} ansi_timing_t;

// Initialize the decimal lookup table (call once at startup)
void ansi_fast_init(void);

// Fast ANSI color sequence generation using precomputed decimals + memcpy
char *append_truecolor_fg(char *dst, uint8_t r, uint8_t g, uint8_t b);
char *append_truecolor_bg(char *dst, uint8_t r, uint8_t g, uint8_t b);
char *append_truecolor_fg_bg(char *dst, uint8_t fg_r, uint8_t fg_g, uint8_t fg_b, uint8_t bg_r, uint8_t bg_g,
                             uint8_t bg_b);

// Run-length encoded color output (emit SGR only when colors change)
typedef struct {
  char *buffer;                   // Output buffer
  size_t capacity;                // Buffer capacity
  size_t length;                  // Current buffer length
  uint8_t last_r, last_g, last_b; // Previous pixel color
  ansi_color_mode_t mode;         // Color mode
  bool first_pixel;               // First pixel flag
} ansi_rle_context_t;

// Initialize run-length encoding context
void ansi_rle_init(ansi_rle_context_t *ctx, char *buffer, size_t capacity, ansi_color_mode_t mode);

// Add a pixel with run-length encoding (only emits SGR when color changes)
void ansi_rle_add_pixel(ansi_rle_context_t *ctx, uint8_t r, uint8_t g, uint8_t b, char ascii_char);

// Finish RLE sequence (adds reset and null terminator)
void ansi_rle_finish(ansi_rle_context_t *ctx);

// 256-color mode (optional - trades color fidelity for maximum speed)
void ansi_fast_init_256color(void);
char *append_256color_fg(char *dst, uint8_t color_index);
uint8_t rgb_to_256color(uint8_t r, uint8_t g, uint8_t b);

// 16-color mode (basic ANSI colors)
void ansi_fast_init_16color(void);
char *append_16color_fg(char *dst, uint8_t color_index);
char *append_16color_bg(char *dst, uint8_t color_index);
uint8_t rgb_to_16color(uint8_t r, uint8_t g, uint8_t b);

// Dithering support for improved color approximation
typedef struct {
  int r, g, b; // Signed integers to handle error propagation
} rgb_error_t;

// Get the actual RGB values for a 16-color ANSI index
void get_16color_rgb(uint8_t color_index, uint8_t *r, uint8_t *g, uint8_t *b);

// Floyd-Steinberg dithering for 16-color terminals
uint8_t rgb_to_16color_dithered(int r, int g, int b, int x, int y, int width, int height, rgb_error_t *error_buffer);

// Terminal capability-aware color functions
// Use the enum from options.h
#include "options.h"
typedef terminal_color_mode_t color_mode_t;

char *append_color_fg_for_mode(char *dst, uint8_t r, uint8_t g, uint8_t b, color_mode_t mode);
