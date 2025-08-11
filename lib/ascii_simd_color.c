#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ascii_simd.h"
#include "options.h"
#include "common.h"
#include "image.h"
#include "buffer_pool.h"

/* ============================================================================
 * SIMD-Optimized Colored ASCII Generation
 *
 * This extends the basic SIMD luminance conversion to include full
 * ANSI color code generation for maximum performance.
 * ============================================================================
 */

// Pre-computed ANSI escape code templates
static const char ANSI_FG_PREFIX[] = "\033[38;2;";
static const char ANSI_BG_PREFIX[] = "\033[48;2;";
// static const char ANSI_SUFFIX[] = "m";
static const char ANSI_RESET[] = "\033[0m";

// Fast integer to string conversion (3 digits max for RGB values 0-255)
static inline int fast_uint8_to_str(uint8_t value, char *str) {
  if (value >= 100) {
    str[0] = '0' + (value / 100);
    str[1] = '0' + ((value % 100) / 10);
    str[2] = '0' + (value % 10);
    return 3;
  } else if (value >= 10) {
    str[0] = '0' + (value / 10);
    str[1] = '0' + (value % 10);
    return 2;
  } else {
    str[0] = '0' + value;
    return 1;
  }
}

// Generate ANSI foreground color code directly into buffer
static inline int generate_ansi_fg(uint8_t r, uint8_t g, uint8_t b, char *buffer) {
  char *pos = buffer;

  // Copy prefix: "\033[38;2;"
  memcpy(pos, ANSI_FG_PREFIX, sizeof(ANSI_FG_PREFIX) - 1);
  pos += sizeof(ANSI_FG_PREFIX) - 1;

  // Red component
  int len = fast_uint8_to_str(r, pos);
  pos += len;
  *pos++ = ';';

  // Green component
  len = fast_uint8_to_str(g, pos);
  pos += len;
  *pos++ = ';';

  // Blue component
  len = fast_uint8_to_str(b, pos);
  pos += len;

  // Suffix: "m"
  *pos++ = 'm';

  return pos - buffer;
}

// Generate ANSI background color code
static inline int generate_ansi_bg(uint8_t r, uint8_t g, uint8_t b, char *buffer) {
  char *pos = buffer;

  // Copy prefix: "\033[48;2;"
  memcpy(pos, ANSI_BG_PREFIX, sizeof(ANSI_BG_PREFIX) - 1);
  pos += sizeof(ANSI_BG_PREFIX) - 1;

  // RGB components (same as foreground)
  int len = fast_uint8_to_str(r, pos);
  pos += len;
  *pos++ = ';';

  len = fast_uint8_to_str(g, pos);
  pos += len;
  *pos++ = ';';

  len = fast_uint8_to_str(b, pos);
  pos += len;
  *pos++ = 'm';

  return pos - buffer;
}

// Forward declaration for _with_buffer wrapper function
size_t convert_row_with_color_optimized_with_buffer(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size,
                                                    int width, bool background_mode, char *reusable_ascii_chars);

// ----------------
char *image_print_colored_simd(image_t *image) {
  size_t max_output_size = image->w * image->h * 40; // Generous estimate for ANSI codes

  // Use buffer pool for internal processing but return malloc'd memory for compatibility
  data_buffer_pool_t *pool = data_buffer_pool_get_global();
  char *temp_buffer = data_buffer_pool_alloc(pool, max_output_size);
  if (!temp_buffer) {
    log_error("Failed to allocate temp buffer from buffer pool (size: %zu)", max_output_size);
    return NULL;
  }

  // Allocate row buffer once per frame to reduce buffer pool contention
  char *row_ascii_chars = data_buffer_pool_alloc(pool, image->w);
  if (!row_ascii_chars) {
    log_error("Failed to allocate row ASCII buffer");
    data_buffer_pool_free(pool, temp_buffer, max_output_size);
    return NULL;
  }

  // Process row by row with SIMD optimization
  size_t total_len = 0;
  for (int y = 0; y < image->h; y++) {
    // Use the reusable row buffer instead of allocating per row
    size_t row_len = convert_row_with_color_optimized_with_buffer((const rgb_pixel_t *)&image->pixels[y * image->w],
                                                                  temp_buffer + total_len, max_output_size - total_len,
                                                                  image->w, opt_background_color, row_ascii_chars);
    total_len += row_len;

    // Add newline after each row (except the last row)
    if (y != image->h - 1 && total_len < max_output_size - 1) {
      temp_buffer[total_len++] = '\n';
    }
  }
  temp_buffer[total_len] = '\0';

  // Copy to malloc'd memory for compatibility with free()
  char *ascii;
  SAFE_MALLOC(ascii, total_len + 1, char *);
  if (!ascii) {
    log_error("Failed to allocate final ASCII buffer");
    data_buffer_pool_free(pool, temp_buffer, max_output_size);
    data_buffer_pool_free(pool, row_ascii_chars, image->w);
    return NULL;
  }

  memcpy(ascii, temp_buffer, total_len + 1);
  data_buffer_pool_free(pool, temp_buffer, max_output_size);
  data_buffer_pool_free(pool, row_ascii_chars, image->w);

  return ascii;
}

/* ============================================================================
 * SIMD + Colored ASCII: Complete Row Processing
 * ============================================================================
 */

// Forward declarations for _with_buffer functions
#ifdef SIMD_SUPPORT_AVX2
size_t convert_row_with_color_avx2_with_buffer(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size,
                                               int width, bool background_mode, char *ascii_chars);
#endif

#ifdef SIMD_SUPPORT_SSE2
size_t convert_row_with_color_sse2_with_buffer(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size,
                                               int width, bool background_mode, char *ascii_chars);
#endif

#ifdef SIMD_SUPPORT_NEON
size_t convert_row_with_color_neon_with_buffer(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size,
                                               int width, bool background_mode, char *ascii_chars);
#endif

size_t convert_row_with_color_scalar_with_buffer(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size,
                                                 int width, bool background_mode, char *ascii_chars);

// Wrapper function declaration
size_t convert_row_with_color_optimized_with_buffer(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size,
                                                    int width, bool background_mode, char *reusable_ascii_chars);

// Wrapper function that uses a pre-allocated buffer to reduce buffer pool contention
size_t convert_row_with_color_optimized_with_buffer(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size,
                                                    int width, bool background_mode, char *reusable_ascii_chars) {
#ifdef SIMD_SUPPORT_AVX2
  return convert_row_with_color_avx2_with_buffer(pixels, output_buffer, buffer_size, width, background_mode,
                                                 reusable_ascii_chars);
#elif defined(SIMD_SUPPORT_SSE2)
  return convert_row_with_color_sse2_with_buffer(pixels, output_buffer, buffer_size, width, background_mode,
                                                 reusable_ascii_chars);
#elif defined(SIMD_SUPPORT_NEON)
  return convert_row_with_color_neon_with_buffer(pixels, output_buffer, buffer_size, width, background_mode,
                                                 reusable_ascii_chars);
#else
  return convert_row_with_color_scalar_with_buffer(pixels, output_buffer, buffer_size, width, background_mode,
                                                   reusable_ascii_chars);
#endif
}

#ifdef SIMD_SUPPORT_AVX2
// Process entire row with SIMD luminance + optimized color generation
size_t convert_row_with_color_avx2(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                   bool background_mode) {

  // Step 1: SIMD luminance conversion for entire row
  data_buffer_pool_t *pool = data_buffer_pool_get_global();
  char *ascii_chars = data_buffer_pool_alloc(pool, width);
  if (!ascii_chars) {
    log_error("Failed to allocate temporary ASCII chars buffer");
    return 0;
  }
  convert_pixels_avx2(pixels, ascii_chars, width);

  // Step 2: Generate colored output
  char *current_pos = output_buffer;
  char *buffer_end = output_buffer + buffer_size;

  for (int x = 0; x < width; x++) {
    const rgb_pixel_t *pixel = &pixels[x];
    char ascii_char = ascii_chars[x];

    size_t remaining = buffer_end - current_pos;
    if (remaining < 64)
      break; // Safety margin

    if (background_mode) {
      // Background mode: colored background, contrasting foreground
      uint8_t luminance = (77 * pixel->r + 150 * pixel->g + 29 * pixel->b) >> 8;
      uint8_t fg_color = (luminance < 127) ? 255 : 0;

      // Generate foreground color code
      int fg_len = generate_ansi_fg(fg_color, fg_color, fg_color, current_pos);
      current_pos += fg_len;

      // Generate background color code
      int bg_len = generate_ansi_bg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += bg_len;

      // Add ASCII character
      *current_pos++ = ascii_char;

    } else {
      // Foreground mode: colored character
      int fg_len = generate_ansi_fg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += fg_len;
      *current_pos++ = ascii_char;
    }
  }

  // Add reset sequence
  size_t remaining = buffer_end - current_pos;
  if (remaining >= sizeof(ANSI_RESET)) {
    memcpy(current_pos, ANSI_RESET, sizeof(ANSI_RESET) - 1);
    current_pos += sizeof(ANSI_RESET) - 1;
  }

  data_buffer_pool_free(pool, ascii_chars, width);
  return current_pos - output_buffer;
}

// AVX2 version with pre-allocated buffer to reduce buffer pool contention
size_t convert_row_with_color_avx2_with_buffer(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size,
                                               int width, bool background_mode, char *ascii_chars) {
  // Step 1: Use provided buffer for SIMD luminance conversion
  convert_pixels_avx2(pixels, ascii_chars, width);

  // Step 2: Generate colored output (same as regular AVX2 version)
  char *current_pos = output_buffer;
  char *buffer_end = output_buffer + buffer_size;

  for (int x = 0; x < width; x++) {
    const rgb_pixel_t *pixel = &pixels[x];
    char ascii_char = ascii_chars[x];

    size_t remaining = buffer_end - current_pos;
    if (remaining < 64)
      break; // Safety margin

    if (background_mode) {
      // Background mode: colored background, contrasting foreground
      uint8_t luminance = (77 * pixel->r + 150 * pixel->g + 29 * pixel->b) >> 8;
      uint8_t fg_color = (luminance < 127) ? 255 : 0;

      // Generate foreground color code
      int fg_len = generate_ansi_fg(fg_color, fg_color, fg_color, current_pos);
      current_pos += fg_len;

      // Generate background color code
      int bg_len = generate_ansi_bg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += bg_len;

      // Add ASCII character
      *current_pos++ = ascii_char;

    } else {
      // Foreground mode: colored character
      int fg_len = generate_ansi_fg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += fg_len;
      *current_pos++ = ascii_char;
    }
  }

  // Add reset sequence
  size_t remaining2 = buffer_end - current_pos;
  if (remaining2 >= sizeof(ANSI_RESET)) {
    memcpy(current_pos, ANSI_RESET, sizeof(ANSI_RESET) - 1);
    current_pos += sizeof(ANSI_RESET) - 1;
  }

  // No buffer pool free - using pre-allocated buffer
  return current_pos - output_buffer;
}
#endif

#ifdef SIMD_SUPPORT_SSE2
// SSE2 version for older Intel/AMD systems
size_t convert_row_with_color_sse2(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                   bool background_mode) {

  // Step 1: SIMD luminance conversion for entire row using SSE2
  data_buffer_pool_t *pool = data_buffer_pool_get_global();
  char *ascii_chars = data_buffer_pool_alloc(pool, width);
  if (!ascii_chars) {
    log_error("Failed to allocate temporary ASCII chars buffer");
    return 0;
  }
  convert_pixels_sse2(pixels, ascii_chars, width);

  // Step 2: Generate colored output (same as AVX2/NEON versions)
  char *current_pos = output_buffer;
  char *buffer_end = output_buffer + buffer_size;

  for (int x = 0; x < width; x++) {
    const rgb_pixel_t *pixel = &pixels[x];
    char ascii_char = ascii_chars[x];

    size_t remaining = buffer_end - current_pos;
    if (remaining < 64)
      break; // Safety margin

    if (background_mode) {
      // Background mode: colored background, contrasting foreground
      uint8_t luminance = (77 * pixel->r + 150 * pixel->g + 29 * pixel->b) >> 8;
      uint8_t fg_color = (luminance < 127) ? 255 : 0;

      // Generate foreground color code
      int fg_len = generate_ansi_fg(fg_color, fg_color, fg_color, current_pos);
      current_pos += fg_len;

      // Generate background color code
      int bg_len = generate_ansi_bg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += bg_len;

      // Add ASCII character
      *current_pos++ = ascii_char;

    } else {
      // Foreground mode: colored character
      int fg_len = generate_ansi_fg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += fg_len;
      *current_pos++ = ascii_char;
    }
  }

  // Add reset sequence
  size_t remaining = buffer_end - current_pos;
  if (remaining >= sizeof(ANSI_RESET)) {
    memcpy(current_pos, ANSI_RESET, sizeof(ANSI_RESET) - 1);
    current_pos += sizeof(ANSI_RESET) - 1;
  }

  data_buffer_pool_free(pool, ascii_chars, width);
  return current_pos - output_buffer;
}

// SSE2 version with pre-allocated buffer to reduce buffer pool contention
size_t convert_row_with_color_sse2_with_buffer(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size,
                                               int width, bool background_mode, char *ascii_chars) {
  // Step 1: Use provided buffer for SIMD luminance conversion
  convert_pixels_sse2(pixels, ascii_chars, width);

  // Step 2: Generate colored output (same as regular SSE2 version)
  char *current_pos = output_buffer;
  char *buffer_end = output_buffer + buffer_size;

  for (int x = 0; x < width; x++) {
    const rgb_pixel_t *pixel = &pixels[x];
    char ascii_char = ascii_chars[x];

    size_t remaining = buffer_end - current_pos;
    if (remaining < 64)
      break; // Safety margin

    if (background_mode) {
      // Background mode: colored background, contrasting foreground
      uint8_t luminance = (77 * pixel->r + 150 * pixel->g + 29 * pixel->b) >> 8;
      uint8_t fg_color = (luminance < 127) ? 255 : 0;

      // Generate foreground color code
      int fg_len = generate_ansi_fg(fg_color, fg_color, fg_color, current_pos);
      current_pos += fg_len;

      // Generate background color code
      int bg_len = generate_ansi_bg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += bg_len;

      // Add ASCII character
      *current_pos++ = ascii_char;

    } else {
      // Foreground mode: colored character
      int fg_len = generate_ansi_fg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += fg_len;
      *current_pos++ = ascii_char;
    }
  }

  // Add reset sequence
  size_t remaining2 = buffer_end - current_pos;
  if (remaining2 >= sizeof(ANSI_RESET)) {
    memcpy(current_pos, ANSI_RESET, sizeof(ANSI_RESET) - 1);
    current_pos += sizeof(ANSI_RESET) - 1;
  }

  // No buffer pool free - using pre-allocated buffer
  return current_pos - output_buffer;
}
#endif

// Scalar version for comparison
size_t convert_row_with_color_scalar(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                     bool background_mode) {

  char *current_pos = output_buffer;
  char *buffer_end = output_buffer + buffer_size;

  for (int x = 0; x < width; x++) {
    const rgb_pixel_t *pixel = &pixels[x];

    // Calculate luminance (scalar)
    int luminance = (77 * pixel->r + 150 * pixel->g + 29 * pixel->b) >> 8;
    if (luminance > 255)
      luminance = 255;

    // Get ASCII character
    static const char palette[] = " .,:;ox%#@";
    char ascii_char = palette[luminance * (sizeof(palette) - 2) / 255];

    size_t remaining = buffer_end - current_pos;
    if (remaining < 64)
      break;

    if (background_mode) {
      uint8_t fg_color = (luminance < 127) ? 255 : 0;
      int fg_len = generate_ansi_fg(fg_color, fg_color, fg_color, current_pos);
      current_pos += fg_len;
      int bg_len = generate_ansi_bg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += bg_len;
      *current_pos++ = ascii_char;
    } else {
      int fg_len = generate_ansi_fg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += fg_len;
      *current_pos++ = ascii_char;
    }
  }

  // Reset sequence
  size_t remaining = buffer_end - current_pos;
  if (remaining >= sizeof(ANSI_RESET)) {
    memcpy(current_pos, ANSI_RESET, sizeof(ANSI_RESET) - 1);
    current_pos += sizeof(ANSI_RESET) - 1;
  }

  return current_pos - output_buffer;
}

// Scalar version with pre-allocated buffer to reduce buffer pool contention
size_t convert_row_with_color_scalar_with_buffer(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size,
                                                 int width, bool background_mode, char *ascii_chars) {
  char *current_pos = output_buffer;
  char *buffer_end = output_buffer + buffer_size;

  // Step 1: Generate ASCII characters using provided buffer
  for (int x = 0; x < width; x++) {
    const rgb_pixel_t *pixel = &pixels[x];

    // Calculate luminance (scalar)
    int luminance = (77 * pixel->r + 150 * pixel->g + 29 * pixel->b) >> 8;
    if (luminance > 255)
      luminance = 255;

    // Get ASCII character
    static const char palette[] = " .,:;ox%#@";
    ascii_chars[x] = palette[luminance * (sizeof(palette) - 2) / 255];
  }

  // Step 2: Generate colored output
  for (int x = 0; x < width; x++) {
    const rgb_pixel_t *pixel = &pixels[x];
    char ascii_char = ascii_chars[x];

    size_t remaining = buffer_end - current_pos;
    if (remaining < 64)
      break;

    if (background_mode) {
      uint8_t luminance = (77 * pixel->r + 150 * pixel->g + 29 * pixel->b) >> 8;
      uint8_t fg_color = (luminance < 127) ? 255 : 0;
      int fg_len = generate_ansi_fg(fg_color, fg_color, fg_color, current_pos);
      current_pos += fg_len;
      int bg_len = generate_ansi_bg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += bg_len;
      *current_pos++ = ascii_char;
    } else {
      int fg_len = generate_ansi_fg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += fg_len;
      *current_pos++ = ascii_char;
    }
  }

  // Reset sequence
  size_t remaining2 = buffer_end - current_pos;
  if (remaining2 >= sizeof(ANSI_RESET)) {
    memcpy(current_pos, ANSI_RESET, sizeof(ANSI_RESET) - 1);
    current_pos += sizeof(ANSI_RESET) - 1;
  }

  // No buffer pool free - using pre-allocated buffer
  return current_pos - output_buffer;
}

#ifdef SIMD_SUPPORT_NEON
// ARM NEON version for Apple Silicon
size_t convert_row_with_color_neon(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                   bool background_mode) {

  // Step 1: SIMD luminance conversion for entire row using NEON
  data_buffer_pool_t *pool = data_buffer_pool_get_global();
  char *ascii_chars = data_buffer_pool_alloc(pool, width);
  if (!ascii_chars) {
    log_error("Failed to allocate temporary ASCII chars buffer");
    return 0;
  }
  convert_pixels_neon(pixels, ascii_chars, width);

  // Step 2: Generate colored output (same as AVX2 version)
  char *current_pos = output_buffer;
  char *buffer_end = output_buffer + buffer_size;

  for (int x = 0; x < width; x++) {
    const rgb_pixel_t *pixel = &pixels[x];
    char ascii_char = ascii_chars[x];

    size_t remaining = buffer_end - current_pos;
    if (remaining < 64)
      break; // Safety margin

    if (background_mode) {
      // Background mode: colored background, contrasting foreground
      uint8_t luminance = (77 * pixel->r + 150 * pixel->g + 29 * pixel->b) >> 8;
      uint8_t fg_color = (luminance < 127) ? 255 : 0;

      // Generate foreground color code
      int fg_len = generate_ansi_fg(fg_color, fg_color, fg_color, current_pos);
      current_pos += fg_len;

      // Generate background color code
      int bg_len = generate_ansi_bg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += bg_len;

      // Add ASCII character
      *current_pos++ = ascii_char;

    } else {
      // Foreground mode: colored character
      int fg_len = generate_ansi_fg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += fg_len;
      *current_pos++ = ascii_char;
    }
  }

  // Add reset sequence
  size_t remaining = buffer_end - current_pos;
  if (remaining >= sizeof(ANSI_RESET)) {
    memcpy(current_pos, ANSI_RESET, sizeof(ANSI_RESET) - 1);
    current_pos += sizeof(ANSI_RESET) - 1;
  }

  data_buffer_pool_free(pool, ascii_chars, width);
  return current_pos - output_buffer;
}

// NEON version with pre-allocated buffer to reduce buffer pool contention
size_t convert_row_with_color_neon_with_buffer(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size,
                                               int width, bool background_mode, char *ascii_chars) {
  // Step 1: Use provided buffer for SIMD luminance conversion
  convert_pixels_neon(pixels, ascii_chars, width);

  // Step 2: Generate colored output (same as regular NEON version)
  char *current_pos = output_buffer;
  char *buffer_end = output_buffer + buffer_size;

  for (int x = 0; x < width; x++) {
    const rgb_pixel_t *pixel = &pixels[x];
    char ascii_char = ascii_chars[x];

    size_t remaining = buffer_end - current_pos;
    if (remaining < 64)
      break; // Safety margin

    if (background_mode) {
      // Background mode: colored background, contrasting foreground
      uint8_t luminance = (77 * pixel->r + 150 * pixel->g + 29 * pixel->b) >> 8;
      uint8_t fg_color = (luminance < 127) ? 255 : 0;

      // Generate foreground color code
      int fg_len = generate_ansi_fg(fg_color, fg_color, fg_color, current_pos);
      current_pos += fg_len;

      // Generate background color code
      int bg_len = generate_ansi_bg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += bg_len;

      // Add ASCII character
      *current_pos++ = ascii_char;

    } else {
      // Foreground mode: colored character
      int fg_len = generate_ansi_fg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += fg_len;
      *current_pos++ = ascii_char;
    }
  }

  // Add reset sequence
  size_t remaining2 = buffer_end - current_pos;
  if (remaining2 >= sizeof(ANSI_RESET)) {
    memcpy(current_pos, ANSI_RESET, sizeof(ANSI_RESET) - 1);
    current_pos += sizeof(ANSI_RESET) - 1;
  }

  // No buffer pool free - using pre-allocated buffer
  return current_pos - output_buffer;
}
#endif

// Auto-dispatch version
size_t convert_row_with_color_optimized(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                        bool background_mode) {
#ifdef SIMD_SUPPORT_AVX2
  return convert_row_with_color_avx2(pixels, output_buffer, buffer_size, width, background_mode);
#elif defined(SIMD_SUPPORT_NEON)
  return convert_row_with_color_neon(pixels, output_buffer, buffer_size, width, background_mode);
#elif defined(SIMD_SUPPORT_SSE2)
  return convert_row_with_color_sse2(pixels, output_buffer, buffer_size, width, background_mode);
#else
  return convert_row_with_color_scalar(pixels, output_buffer, buffer_size, width, background_mode);
#endif
}

/* ============================================================================
 * Benchmark Colored ASCII Performance
 * ============================================================================
 */

typedef struct {
  double scalar_time;
  double simd_time;
  double speedup;
  size_t output_size_scalar;
  size_t output_size_simd;
} color_benchmark_t;

color_benchmark_t benchmark_colored_ascii(int width, int height, int iterations, bool background_mode) {
  color_benchmark_t result = {0};

  int pixel_count = width * height;
  size_t max_output_size = pixel_count * 40; // Generous estimate for ANSI codes

  // Generate test data
  rgb_pixel_t *test_pixels = malloc(pixel_count * sizeof(rgb_pixel_t));
  char *scalar_output = malloc(max_output_size);
  char *simd_output = malloc(max_output_size);

  srand(12345);
  for (int i = 0; i < pixel_count; i++) {
    test_pixels[i].r = rand() % 256;
    test_pixels[i].g = rand() % 256;
    test_pixels[i].b = rand() % 256;
  }

  printf("Benchmarking colored ASCII %dx%d (%s mode) x %d iterations...\n", width, height,
         background_mode ? "background" : "foreground", iterations);

  // Benchmark scalar version
  double start = (double)clock() / CLOCKS_PER_SEC;
  for (int iter = 0; iter < iterations; iter++) {
    for (int y = 0; y < height; y++) {
      size_t row_size = convert_row_with_color_scalar(&test_pixels[y * width], scalar_output, max_output_size, width,
                                                      background_mode);
      if (iter == 0)
        result.output_size_scalar += row_size;
    }
  }
  result.scalar_time = (double)clock() / CLOCKS_PER_SEC - start;

  // Benchmark SIMD version
  start = (double)clock() / CLOCKS_PER_SEC;
  for (int iter = 0; iter < iterations; iter++) {
    for (int y = 0; y < height; y++) {
      size_t row_size = convert_row_with_color_optimized(&test_pixels[y * width], simd_output, max_output_size, width,
                                                         background_mode);
      if (iter == 0)
        result.output_size_simd += row_size;
    }
  }
  result.simd_time = (double)clock() / CLOCKS_PER_SEC - start;

  result.speedup = result.scalar_time / result.simd_time;

  free(test_pixels);
  free(scalar_output);
  free(simd_output);

  return result;
}
