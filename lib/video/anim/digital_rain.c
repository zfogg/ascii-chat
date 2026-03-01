/**
 * @file video/anim/digital_rain.c
 * @ingroup video
 * @brief Matrix-style digital rain effect implementation
 */

#include <ascii-chat/video/anim/digital_rain.h>
#include <ascii-chat/video/color_filter.h>
#include <ascii-chat/debug/memory.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/util/utf8.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Math Helpers
 * ============================================================================ */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SQRT_2 1.4142135623730951
#define SQRT_5 2.23606797749979

/**
 * Generate pseudo-random float from 2D coordinates
 * Uses hash function to generate deterministic randomness per column
 */
static float random_float(float x, float y) {
  float dt = x * 12.9898f + y * 78.233f;
  float sn = fmodf(dt, (float)M_PI);
  return fmodf(sinf(sn) * 43758.5453f, 1.0f);
}

/**
 * Apply organic wobble to time value
 * Prevents mechanical-looking wave patterns
 */
static float wobble(float x) {
  return x + 0.3f * sinf((float)SQRT_2 * x) + 0.2f * sinf((float)SQRT_5 * x);
}

/**
 * Fractional part of float (wraps to 0-1 range)
 */
static inline float fract(float x) {
  return x - floorf(x);
}

/* ============================================================================
 * Core Algorithm
 * ============================================================================ */

/**
 * Calculate rain brightness for a given position and time
 *
 * This is the heart of the Matrix rain effect. It creates a sawtooth wave
 * pattern that repeats, producing multiple "raindrops" per column.
 *
 * @param rain Digital rain context
 * @param col Column index (x position)
 * @param row Row index (y position)
 * @param sim_time Current simulation time
 * @return Brightness value (0.0 to 1.0)
 */
static float get_rain_brightness(digital_rain_t *rain, int col, int row, float sim_time) {
  if (col < 0 || col >= rain->num_columns) {
    return 0.0f;
  }

  digital_rain_column_t *column = &rain->columns[col];

  // Calculate time for this column (with random offset and speed variation)
  float column_time = column->time_offset + sim_time * rain->fall_speed * column->speed_multiplier;

  // Calculate rain time for this cell
  // Subtract row from column_time so the wave moves DOWN (to higher row numbers)
  // As column_time increases, the pattern shifts to higher rows (downward)
  float rain_time = (column_time - (float)row) / rain->raindrop_length;

  // Apply wobble for organic variation
  rain_time = wobble(rain_time);

  // Create sawtooth wave: fract() wraps time to 0-1, creating repeating drops
  // Subtract from 1.0 so brightness increases toward bottom of drop
  return 1.0f - fract(rain_time);
}

/* ============================================================================
 * Initialization and Cleanup
 * ============================================================================ */

digital_rain_t *digital_rain_init(int num_columns, int num_rows) {
  if (num_columns <= 0 || num_rows <= 0) {
    log_error("digital_rain_init: invalid dimensions %dx%d", num_columns, num_rows);
    return NULL;
  }

  digital_rain_t *rain = SAFE_CALLOC(1, sizeof(digital_rain_t), digital_rain_t *);
  if (!rain) {
    log_error("digital_rain_init: failed to allocate context");
    return NULL;
  }

  rain->num_columns = num_columns;
  rain->num_rows = num_rows;

  // Allocate column state array
  rain->columns = SAFE_CALLOC((size_t)num_columns, sizeof(digital_rain_column_t), digital_rain_column_t *);
  if (!rain->columns) {
    log_error("digital_rain_init: failed to allocate column array");
    SAFE_FREE(rain);
    return NULL;
  }

  // Allocate previous brightness array
  size_t grid_size = (size_t)num_columns * (size_t)num_rows;
  rain->previous_brightness = SAFE_CALLOC(grid_size, sizeof(float), float *);
  if (!rain->previous_brightness) {
    log_error("digital_rain_init: failed to allocate brightness array");
    SAFE_FREE(rain->columns);
    SAFE_FREE(rain);
    return NULL;
  }

  // Initialize per-column random offsets
  for (int col = 0; col < num_columns; col++) {
    digital_rain_column_t *column = &rain->columns[col];
    column->time_offset = random_float((float)col, 0.0f) * 1000.0f;
    column->speed_multiplier = random_float((float)col + 0.1f, 0.0f) * 0.5f + 0.5f;
    column->phase_offset = random_float((float)col + 0.2f, 0.0f) * (float)M_PI * 2.0f;
  }

  // Set default parameters
  rain->fall_speed = DIGITAL_RAIN_DEFAULT_FALL_SPEED;
  rain->raindrop_length = DIGITAL_RAIN_DEFAULT_RAINDROP_LENGTH;
  rain->brightness_decay = DIGITAL_RAIN_DEFAULT_BRIGHTNESS_DECAY;
  rain->animation_speed = DIGITAL_RAIN_DEFAULT_ANIMATION_SPEED;
  rain->color_r = DIGITAL_RAIN_DEFAULT_COLOR_R;
  rain->color_g = DIGITAL_RAIN_DEFAULT_COLOR_G;
  rain->color_b = DIGITAL_RAIN_DEFAULT_COLOR_B;
  rain->cursor_brightness = DIGITAL_RAIN_DEFAULT_CURSOR_BRIGHTNESS;
  rain->rainbow_mode = false;
  rain->first_frame = true;
  rain->time = 0.0f;

  log_info("Digital rain initialized: %dx%d grid", num_columns, num_rows);
  return rain;
}

void digital_rain_destroy(digital_rain_t *rain) {
  if (!rain) {
    return;
  }

  SAFE_FREE(rain->columns);
  SAFE_FREE(rain->previous_brightness);
  SAFE_FREE(rain);
}

void digital_rain_reset(digital_rain_t *rain) {
  if (!rain) {
    return;
  }

  rain->time = 0.0f;
  rain->first_frame = true;

  // Clear previous brightness
  size_t grid_size = (size_t)rain->num_columns * (size_t)rain->num_rows;
  memset(rain->previous_brightness, 0, grid_size * sizeof(float));
}

/* ============================================================================
 * Parameter Adjustment
 * ============================================================================ */

void digital_rain_set_fall_speed(digital_rain_t *rain, float speed) {
  if (rain) {
    rain->fall_speed = speed;
  }
}

void digital_rain_set_raindrop_length(digital_rain_t *rain, float length) {
  if (rain) {
    rain->raindrop_length = length;
  }
}

void digital_rain_set_color(digital_rain_t *rain, uint8_t r, uint8_t g, uint8_t b) {
  if (rain) {
    rain->color_r = r;
    rain->color_g = g;
    rain->color_b = b;
  }
}

void digital_rain_set_color_from_filter(digital_rain_t *rain, color_filter_t filter) {
  if (!rain) {
    return;
  }

  // If no filter is set, use default Matrix green
  if (filter == COLOR_FILTER_NONE) {
    rain->rainbow_mode = false;
    digital_rain_set_color(rain, DIGITAL_RAIN_DEFAULT_COLOR_R, DIGITAL_RAIN_DEFAULT_COLOR_G,
                           DIGITAL_RAIN_DEFAULT_COLOR_B);
    return;
  }

  // Rainbow mode: enable dynamic color cycling
  if (filter == COLOR_FILTER_RAINBOW) {
    rain->rainbow_mode = true;
    // Set initial color to red (will be overridden during rendering)
    digital_rain_set_color(rain, 255, 0, 0);
    return;
  }

  // Get color from filter metadata
  rain->rainbow_mode = false;
  const color_filter_def_t *filter_def = color_filter_get_metadata(filter);
  if (filter_def) {
    digital_rain_set_color(rain, filter_def->r, filter_def->g, filter_def->b);
  }
}

/* ============================================================================
 * Frame Processing
 * ============================================================================ */

/**
 * Parse ANSI truecolor sequence and extract RGB values
 * Returns pointer after sequence, or NULL if not a color sequence
 * Sets r, g, b to parsed values if found
 */
static const char *parse_ansi_color(const char *str, int *r, int *g, int *b, bool *is_foreground) {
  if (*str != '\033') {
    return NULL;
  }

  const char *p = str + 1;
  if (*p != '[') {
    return NULL;
  }
  p++; // Skip [

  // Check for foreground (38) or background (48) truecolor
  if (*p == '3' && *(p + 1) == '8') {
    *is_foreground = true;
    p += 2;
  } else if (*p == '4' && *(p + 1) == '8') {
    *is_foreground = false;
    p += 2;
  } else {
    return NULL;
  }

  // Expect ";2;" for RGB mode
  if (*p != ';' || *(p + 1) != '2' || *(p + 2) != ';') {
    return NULL;
  }
  p += 3;

  // Parse R
  *r = 0;
  while (*p >= '0' && *p <= '9') {
    *r = *r * 10 + (*p - '0');
    p++;
  }
  if (*p != ';')
    return NULL;
  p++;

  // Parse G
  *g = 0;
  while (*p >= '0' && *p <= '9') {
    *g = *g * 10 + (*p - '0');
    p++;
  }
  if (*p != ';')
    return NULL;
  p++;

  // Parse B
  *b = 0;
  while (*p >= '0' && *p <= '9') {
    *b = *b * 10 + (*p - '0');
    p++;
  }
  if (*p != 'm')
    return NULL;
  p++;

  return p;
}

/**
 * Skip ANSI escape sequences in a string
 * Returns pointer to next character after the sequence
 */
static const char *skip_ansi_sequence(const char *str) {
  if (*str != '\033') {
    return str;
  }

  str++; // Skip ESC

  if (*str == '[') {
    str++; // Skip [
    // Skip until we find a terminator (@ through ~)
    while (*str && !(*str >= '@' && *str <= '~')) {
      str++;
    }
    if (*str) {
      str++; // Skip terminator
    }
  }

  return str;
}

/**
 * Generate brightness-modulated ANSI color code
 * Multiplies RGB values by brightness factor
 */
static int generate_modulated_color(char *buf, size_t buf_size, int r, int g, int b, float brightness,
                                    bool is_foreground, bool is_cursor) {
  // Apply cursor brightness boost
  if (is_cursor) {
    brightness *= 2.0f;
  }

  // Clamp brightness
  if (brightness < 0.0f)
    brightness = 0.0f;
  if (brightness > 1.0f)
    brightness = 1.0f;

  // Modulate RGB by brightness
  int new_r = (int)((float)r * brightness);
  int new_g = (int)((float)g * brightness);
  int new_b = (int)((float)b * brightness);

  // Clamp to valid range
  if (new_r < 0)
    new_r = 0;
  if (new_r > 255)
    new_r = 255;
  if (new_g < 0)
    new_g = 0;
  if (new_g > 255)
    new_g = 255;
  if (new_b < 0)
    new_b = 0;
  if (new_b > 255)
    new_b = 255;

  // Generate ANSI code
  if (is_foreground) {
    return snprintf(buf, buf_size, "\033[38;2;%d;%d;%dm", new_r, new_g, new_b);
  } else {
    return snprintf(buf, buf_size, "\033[48;2;%d;%d;%dm", new_r, new_g, new_b);
  }
}

char *digital_rain_apply(digital_rain_t *rain, const char *frame, float delta_time) {
  if (!rain || !frame) {
    log_error("digital_rain_apply: NULL parameter");
    return NULL;
  }

  // Update time
  rain->time += delta_time * rain->animation_speed;
  float sim_time = rain->time;

  // Update rainbow color if rainbow mode is enabled
  if (rain->rainbow_mode) {
    color_filter_calculate_rainbow(sim_time, &rain->color_r, &rain->color_g, &rain->color_b);
  }

  // Allocate output buffer (input size + overhead for ANSI codes)
  // Estimate: each character might need up to 20 bytes for ANSI codes
  size_t input_len = strlen(frame);
  size_t output_capacity = input_len * 20 + 1024;
  char *output = SAFE_MALLOC(output_capacity, char *);
  if (!output) {
    log_error("digital_rain_apply: failed to allocate output buffer");
    return NULL;
  }

  const char *src = frame;
  char *dst = output;
  size_t remaining = output_capacity;

  int col = 0;
  int row = 0;

  while (*src && remaining > 100) {
    // Try to parse and modify ANSI color sequences
    if (*src == '\033') {
      int r, g, b;
      bool is_foreground;
      const char *after = parse_ansi_color(src, &r, &g, &b, &is_foreground);

      if (after) {
        // Found a color sequence - calculate brightness for this position
        float brightness = get_rain_brightness(rain, col, row, sim_time);
        float brightness_below = get_rain_brightness(rain, col, row + 1, sim_time);
        bool is_cursor = brightness > brightness_below;

        // Blend with previous brightness for smooth transitions
        if (!rain->first_frame && row < rain->num_rows && col < rain->num_columns) {
          size_t idx = (size_t)row * (size_t)rain->num_columns + (size_t)col;
          float prev_brightness = rain->previous_brightness[idx];
          brightness = prev_brightness + (brightness - prev_brightness) * rain->brightness_decay;
          rain->previous_brightness[idx] = brightness;
        } else if (row < rain->num_rows && col < rain->num_columns) {
          size_t idx = (size_t)row * (size_t)rain->num_columns + (size_t)col;
          rain->previous_brightness[idx] = brightness;
        }

        // Generate modulated color
        char ansi_buf[32];
        int ansi_len =
            generate_modulated_color(ansi_buf, sizeof(ansi_buf), r, g, b, brightness, is_foreground, is_cursor);

        if ((size_t)ansi_len < remaining) {
          memcpy(dst, ansi_buf, (size_t)ansi_len);
          dst += ansi_len;
          remaining -= (size_t)ansi_len;
        }

        src = after;
        continue;
      } else {
        // Not a color sequence - copy as-is
        const char *after_skip = skip_ansi_sequence(src);
        size_t ansi_len = (size_t)(after_skip - src);
        if (ansi_len < remaining) {
          memcpy(dst, src, ansi_len);
          dst += ansi_len;
          remaining -= ansi_len;
          src = after_skip;
        } else {
          break;
        }
        continue;
      }
    }

    // Handle newline
    if (*src == '\n') {
      *dst++ = *src++;
      remaining--;
      row++;
      col = 0;
      continue;
    }

    // Copy regular characters - inject color for non-colored characters
    if (remaining > 0) {
      // Calculate brightness for this position
      float brightness = get_rain_brightness(rain, col, row, sim_time);
      float brightness_below = get_rain_brightness(rain, col, row + 1, sim_time);
      bool is_cursor = brightness > brightness_below;

      // Blend with previous brightness
      if (!rain->first_frame && row < rain->num_rows && col < rain->num_columns) {
        size_t idx = (size_t)row * (size_t)rain->num_columns + (size_t)col;
        float prev_brightness = rain->previous_brightness[idx];
        brightness = prev_brightness + (brightness - prev_brightness) * rain->brightness_decay;
        rain->previous_brightness[idx] = brightness;
      } else if (row < rain->num_rows && col < rain->num_columns) {
        size_t idx = (size_t)row * (size_t)rain->num_columns + (size_t)col;
        rain->previous_brightness[idx] = brightness;
      }

      // For characters without explicit color codes, use the rain's default color
      char ansi_buf[32];
      int ansi_len = generate_modulated_color(ansi_buf, sizeof(ansi_buf), rain->color_r, rain->color_g, rain->color_b,
                                              brightness, true, is_cursor);

      if ((size_t)ansi_len < remaining) {
        memcpy(dst, ansi_buf, (size_t)ansi_len);
        dst += ansi_len;
        remaining -= (size_t)ansi_len;
      }

      // Decode UTF-8 character to get byte length
      uint32_t codepoint;
      int utf8_len = utf8_decode((const uint8_t *)src, &codepoint);
      if (utf8_len < 0) {
        // Invalid UTF-8, treat as single byte
        utf8_len = 1;
      }

      // Copy all bytes of the UTF-8 character
      for (int i = 0; i < utf8_len && *src && remaining > 0; i++) {
        *dst++ = *src++;
        remaining--;
      }

      // Only increment column once per character (not per byte)
      col++;
    } else {
      break;
    }
  }

  // Null-terminate
  if (remaining > 0) {
    *dst = '\0';
  } else {
    output[output_capacity - 1] = '\0';
  }

  rain->first_frame = false;
  return output;
}
