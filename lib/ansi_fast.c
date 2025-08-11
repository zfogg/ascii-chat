#include "ansi_fast.h"
#include "common.h"
#include <string.h>
#include <time.h>
#include <unistd.h>

// Global decimal lookup table - precomputed at startup (extern for testing)
dec3_t dec3[256];
static bool dec3_initialized = false;

// 256-color lookup table (optional)
static char color256_strings[256][16]; // Pre-built SGR strings like "\033[38;5;123m"
static bool color256_initialized = false;

// Initialize decimal lookup table - converts 0-255 to string format
void ansi_fast_init(void) {
  if (dec3_initialized)
    return;

  for (int v = 0; v < 256; v++) {
    int d2 = v / 100;
    int r = v % 100;
    int d1 = r / 10;
    int d0 = r % 10;

    char *p = dec3[v].s;
    if (d2) {
      // 100-255: three digits
      p[0] = '0' + d2;
      p[1] = '0' + d1;
      p[2] = '0' + d0;
      dec3[v].len = 3;
    } else if (d1) {
      // 10-99: two digits
      p[0] = '0' + d1;
      p[1] = '0' + d0;
      dec3[v].len = 2;
    } else {
      // 0-9: one digit
      p[0] = '0' + d0;
      dec3[v].len = 1;
    }
  }

  dec3_initialized = true;
}

// Fast foreground color: \033[38;2;R;G;Bm
char *append_truecolor_fg(char *dst, uint8_t r, uint8_t g, uint8_t b) {
  // Static prefix - 7 bytes
  memcpy(dst, "\033[38;2;", 7);
  dst += 7;

  // Red component + semicolon
  memcpy(dst, dec3[r].s, dec3[r].len);
  dst += dec3[r].len;
  *dst++ = ';';

  // Green component + semicolon
  memcpy(dst, dec3[g].s, dec3[g].len);
  dst += dec3[g].len;
  *dst++ = ';';

  // Blue component + suffix
  memcpy(dst, dec3[b].s, dec3[b].len);
  dst += dec3[b].len;
  *dst++ = 'm';

  return dst;
}

// Fast background color: \033[48;2;R;G;Bm
char *append_truecolor_bg(char *dst, uint8_t r, uint8_t g, uint8_t b) {
  memcpy(dst, "\033[48;2;", 7);
  dst += 7;

  memcpy(dst, dec3[r].s, dec3[r].len);
  dst += dec3[r].len;
  *dst++ = ';';

  memcpy(dst, dec3[g].s, dec3[g].len);
  dst += dec3[g].len;
  *dst++ = ';';

  memcpy(dst, dec3[b].s, dec3[b].len);
  dst += dec3[b].len;
  *dst++ = 'm';

  return dst;
}

// Combined foreground + background: \033[38;2;R;G;B;48;2;r;g;bm
char *append_truecolor_fg_bg(char *dst, uint8_t fg_r, uint8_t fg_g, uint8_t fg_b, uint8_t bg_r, uint8_t bg_g,
                             uint8_t bg_b) {
  memcpy(dst, "\033[38;2;", 7);
  dst += 7;

  // Foreground RGB
  memcpy(dst, dec3[fg_r].s, dec3[fg_r].len);
  dst += dec3[fg_r].len;
  *dst++ = ';';

  memcpy(dst, dec3[fg_g].s, dec3[fg_g].len);
  dst += dec3[fg_g].len;
  *dst++ = ';';

  memcpy(dst, dec3[fg_b].s, dec3[fg_b].len);
  dst += dec3[fg_b].len;

  // Background RGB
  memcpy(dst, ";48;2;", 6);
  dst += 6;

  memcpy(dst, dec3[bg_r].s, dec3[bg_r].len);
  dst += dec3[bg_r].len;
  *dst++ = ';';

  memcpy(dst, dec3[bg_g].s, dec3[bg_g].len);
  dst += dec3[bg_g].len;
  *dst++ = ';';

  memcpy(dst, dec3[bg_b].s, dec3[bg_b].len);
  dst += dec3[bg_b].len;
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

// Two pixels per cell using ▀ (U+2580 upper half block)
char *append_half_block_pixels(char *dst, uint8_t top_r, uint8_t top_g, uint8_t top_b, uint8_t bot_r, uint8_t bot_g,
                               uint8_t bot_b) {
  // Set foreground to top pixel color, background to bottom pixel color
  dst = append_truecolor_fg_bg(dst, top_r, top_g, top_b, bot_r, bot_g, bot_b);

  // Add the ▀ character (UTF-8: 0xE2 0x96 0x80)
  *dst++ = 0xE2;
  *dst++ = 0x96;
  *dst++ = 0x80;

  return dst;
}

// Get current time in seconds (high precision)
static double get_time_seconds(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
    return ts.tv_sec + ts.tv_nsec / 1e9;
  }
  // Fallback to lower precision
  return (double)clock() / CLOCKS_PER_SEC;
}

// Complete optimized frame generation with detailed timing
ansi_timing_t generate_ansi_frame_optimized(const uint8_t *pixels, int width, int height, char *output_buffer,
                                            size_t buffer_capacity, ansi_color_mode_t mode, bool use_half_blocks) {
  ansi_timing_t timing = {0};
  double start_time = get_time_seconds();

  // Phase 1: Pixel processing (luminance, ASCII conversion)
  double pixel_start = get_time_seconds();

  // ASCII palette (matching existing implementation)
  static const char ascii_palette[] = "   ...',;:clodxkO0KXNWM";
  static const int palette_len = sizeof(ascii_palette) - 2;

// Luminance weights (NTSC standard)
#define LUMA_RED 77
#define LUMA_GREEN 150
#define LUMA_BLUE 29

  int pixel_count = width * height;
  char *ascii_chars;
  SAFE_MALLOC(ascii_chars, pixel_count, char *);

  // Convert RGB pixels to ASCII characters
  for (int i = 0; i < pixel_count; i++) {
    const uint8_t *p = &pixels[i * 3]; // Assuming RGB format

    // Calculate luminance using integer arithmetic
    int luminance = (LUMA_RED * p[0] + LUMA_GREEN * p[1] + LUMA_BLUE * p[2]) >> 8;

    // Map luminance to ASCII character
    int palette_index = (luminance * palette_len) / 255;
    if (palette_index >= palette_len)
      palette_index = palette_len - 1;
    ascii_chars[i] = ascii_palette[palette_index];
  }

  timing.pixel_time = get_time_seconds() - pixel_start;

  // Phase 2: String generation (ANSI escape sequences)
  double string_start = get_time_seconds();

  ansi_rle_context_t rle_ctx;
  ansi_rle_init(&rle_ctx, output_buffer, buffer_capacity, mode);

  if (use_half_blocks && height >= 2) {
    // Two-pixels-per-cell mode (halves output)
    int effective_height = height / 2;

    for (int y = 0; y < effective_height; y++) {
      for (int x = 0; x < width; x++) {
        int top_idx = y * 2 * width + x;
        int bot_idx = (y * 2 + 1) * width + x;

        if (bot_idx < pixel_count) {
          const uint8_t *top_pixel = &pixels[top_idx * 3];
          const uint8_t *bot_pixel = &pixels[bot_idx * 3];

          // Use half-block approach
          char *pos = rle_ctx.buffer + rle_ctx.length;
          if (rle_ctx.length + 64 < rle_ctx.capacity) { // Reserve space
            pos = append_half_block_pixels(pos, top_pixel[0], top_pixel[1], top_pixel[2], bot_pixel[0], bot_pixel[1],
                                           bot_pixel[2]);
            rle_ctx.length = pos - rle_ctx.buffer;
          }
        } else {
          // Handle odd height case
          const uint8_t *top_pixel = &pixels[top_idx * 3];
          ansi_rle_add_pixel(&rle_ctx, top_pixel[0], top_pixel[1], top_pixel[2], ascii_chars[top_idx]);
        }
      }

      // Add newline
      if (rle_ctx.length < rle_ctx.capacity - 1) {
        rle_ctx.buffer[rle_ctx.length++] = '\n';
      }
    }
  } else {
    // Standard one-pixel-per-cell mode
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        int idx = y * width + x;
        const uint8_t *pixel = &pixels[idx * 3];

        ansi_rle_add_pixel(&rle_ctx, pixel[0], pixel[1], pixel[2], ascii_chars[idx]);
      }

      // Add newline (except for last row)
      if (y < height - 1 && rle_ctx.length < rle_ctx.capacity - 1) {
        rle_ctx.buffer[rle_ctx.length++] = '\n';
      }
    }
  }

  ansi_rle_finish(&rle_ctx);
  timing.string_time = get_time_seconds() - string_start;

  // Phase 3: Terminal output (single write)
  double output_start = get_time_seconds();
  write(STDOUT_FILENO, output_buffer, rle_ctx.length);
  timing.output_time = get_time_seconds() - output_start;

  timing.total_time = get_time_seconds() - start_time;

  free(ascii_chars);
  return timing;
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