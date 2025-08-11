#ifndef ANSI_FAST_H
#define ANSI_FAST_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Fast ANSI escape sequence generation using precomputed lookup tables
// Based on ChatGPT's optimization recommendations

// Decimal string representation for 0-255
typedef struct {
  uint8_t len; // String length (1-3)
  char s[3];   // Decimal digits (no null terminator needed)
} dec3_t;

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

// Two-pixels-per-cell using ▀ (U+2580) - halves output size
char *append_half_block_pixels(char *dst, uint8_t top_r, uint8_t top_g, uint8_t top_b, uint8_t bot_r, uint8_t bot_g,
                               uint8_t bot_b);

// Complete optimized frame generation with timing breakdown
ansi_timing_t generate_ansi_frame_optimized(const uint8_t *pixels, int width, int height, char *output_buffer,
                                            size_t buffer_capacity, ansi_color_mode_t mode, bool use_half_blocks);

// 256-color mode (optional - trades color fidelity for maximum speed)
void ansi_fast_init_256color(void);
char *append_256color_fg(char *dst, uint8_t color_index);
uint8_t rgb_to_256color(uint8_t r, uint8_t g, uint8_t b);

#endif // ANSI_FAST_H