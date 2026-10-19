/**
 * @file video/anim/digital_rain.c
 * @ingroup video
 * @brief Matrix-style digital rain effect implementation
 */

#include "backends.h"
#include <ascii-chat/video/anim/digital_rain.h>
#include <ascii-chat/video/rgba/color_filter.h>
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
static float get_rain_brightness(digital_rain_t *rain, int col, int row, float sim_time, float *head) {
  *head = 0.0f;
  if (col < 0 || col >= rain->num_columns)
    return 0.0f;
  const digital_rain_column_t *column = &rain->columns[col];
  float length = rain->raindrop_length * (0.75f + 0.5f * random_float((float)col + 0.3f, 0));
  float gap = length * (0.25f + 0.65f * random_float((float)col + 0.4f, 0));
  float period = length + gap;
  float travel = column->time_offset + sim_time * rain->fall_speed * column->speed_multiplier;
  travel += 0.35f * sinf(sim_time * 0.7f + column->phase_offset);
  float distance = fract((travel - (float)row) / period) * period;
  float head_distance = fminf(distance, period - distance);
  *head = expf(-2.5f * head_distance * head_distance);
  float tail = powf(fmaxf(0.0f, 1.0f - distance / length), 0.65f);
  // Keep the source readable between streams; the head and tail add contrast.
  return 0.45f + 0.55f * tail;
}

// Ordered by approximate ink coverage in a monospace terminal. Keeping mutations
// within a group preserves the source image's shading and leaves spaces untouched.
static const char *glyph_groups[] = {".,'`",           ":;-_",        "!|/\\()[]{}", "iltfrxnvczXYUJLC",
                                     "oahkbdpqwmZO0Q", "*+?%&$#@MWB8"};

int digital_rain_glyph_group(uint32_t codepoint) {
  if (codepoint > 126 || codepoint <= 32)
    return -1;
  for (size_t i = 0; i < sizeof(glyph_groups) / sizeof(glyph_groups[0]); i++)
    if (strchr(glyph_groups[i], (int)codepoint))
      return (int)i;
  return -1;
}

static uint32_t mutate_glyph(uint32_t codepoint, int col, int row, float time) {
  int group = digital_rain_glyph_group(codepoint);
  if (group < 0)
    return codepoint;
  // Independent periods and phase offsets avoid synchronized full-grid flicker.
  float seed = random_float((float)col + 1.3f, (float)row + 2.7f);
  unsigned int cycle = (unsigned int)floorf(time * (1.2f + 2.0f * seed) + seed * 13.0f);
  uint32_t hash = (uint32_t)(col + 1) * 73856093u ^ (uint32_t)(row + 1) * 19349663u ^ cycle * 83492791u;
  hash ^= hash >> 16;
  hash *= 0x7feb352du;
  hash ^= hash >> 15;
  const char *choices = glyph_groups[group];
  return (uint8_t)choices[hash % strlen(choices)];
}

/* ============================================================================
 * Initialization and Cleanup
 * ============================================================================ */

digital_rain_t *digital_rain_init(int num_columns, int num_rows) {
  if (num_columns <= 0 || num_rows <= 0) {
    log_error("invalid dimensions %dx%d", num_columns, num_rows);
    return NULL;
  }

  digital_rain_t *rain = SAFE_CALLOC(1, sizeof(digital_rain_t), digital_rain_t *);
  if (!rain) {
    log_error("failed to allocate context");
    return NULL;
  }

  rain->num_columns = num_columns;
  rain->num_rows = num_rows;

  // Allocate column state array
  rain->columns = SAFE_CALLOC((size_t)num_columns, sizeof(digital_rain_column_t), digital_rain_column_t *);
  if (!rain->columns) {
    log_error("failed to allocate column array");
    SAFE_FREE(rain);
    return NULL;
  }

  // Allocate previous brightness array
  size_t grid_size = (size_t)num_columns * (size_t)num_rows;
  rain->previous_brightness = SAFE_CALLOC(grid_size, sizeof(float), float *);
  if (!rain->previous_brightness) {
    log_error("failed to allocate brightness array");
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

  animation_init(
      &rain->animation, "digital_rain",
      (animation_config_t){
          .type = ANIMATION_DIGITAL_RAIN, .fps = 60, .speed = 1, .hidden_policy = ANIMATION_HIDDEN_CONTINUE});
  animation_sample_t initial;
  animation_update(&rain->animation, 0, &initial);
  log_info("Digital rain initialized: %dx%d grid", num_columns, num_rows);
  return rain;
}

void digital_rain_destroy(digital_rain_t *rain) {
  if (!rain) {
    return;
  }

  animation_destroy(&rain->animation);
  SAFE_FREE(rain->columns);
  SAFE_FREE(rain->previous_brightness);
  SAFE_FREE(rain);
}

void digital_rain_reset(digital_rain_t *rain) {
  if (!rain) {
    return;
  }

  animation_reset(&rain->animation);
  rain->animation_timestamp_ns = 0;
  animation_sample_t initial;
  animation_update(&rain->animation, 0, &initial);
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

static int generate_rain_color(char *buf, size_t size, digital_rain_t *rain, float brightness, float head,
                               float luminance, bool foreground) {
  // Source luminance shapes both the saturated body and the pale leading glint.
  float light = foreground ? 0.80f + 0.20f * sqrtf(luminance) : luminance;
  // Limit whitening so even boosted heads retain the selected rain hue.
  float white = fminf(0.40f, head * rain->cursor_brightness * 0.18f);
  float level = fminf(1.0f, brightness + head * 0.25f) * light;
  int r = (int)((rain->color_r + (255 - rain->color_r) * white) * level);
  int g = (int)((rain->color_g + (255 - rain->color_g) * white) * level);
  int b = (int)((rain->color_b + (255 - rain->color_b) * white) * level);
  return snprintf(buf, size, "\033[%d;2;%d;%d;%dm", foreground ? 38 : 48, r, g, b);
}

char *digital_rain_apply(digital_rain_t *rain, const char *frame, float delta_time) {
  if (!rain || !frame || !isfinite(delta_time) || delta_time < 0 ||
      (double)delta_time * 1e9 >= (double)(UINT64_MAX - rain->animation_timestamp_ns))
    return NULL;
  if (animation_set_speed(&rain->animation, rain->animation_speed) != ASCIICHAT_OK)
    return NULL;
  uint64_t now = rain->animation_timestamp_ns + (uint64_t)((double)delta_time * 1e9);
  animation_sample_t sample;
  if (animation_update(&rain->animation, now, &sample) != ASCIICHAT_OK)
    return NULL;
  rain->animation_timestamp_ns = now;
  char *output = NULL;
  animation_target_t target = {.type = ANIMATION_TARGET_ANSI, .ansi = {.input = frame, .out = &output, .rain = rain}};
  if (animation_apply(&sample, &target) != ASCIICHAT_OK)
    return NULL;
  return output;
}

char *digital_rain_render_at(digital_rain_t *rain, const char *frame, float seconds) {
  if (!rain || !frame) {
    log_error("NULL parameter");
    return NULL;
  }

  if (!isfinite(seconds) || seconds < 0)
    return NULL;
  float elapsed = fmaxf(0.0f, seconds - rain->time);
  rain->time = seconds;
  float sim_time = seconds;
  // brightness_decay retains its meaning as a blend factor at 60 Hz.
  float decay = fminf(1.0f, fmaxf(0.0f, rain->brightness_decay));
  float alpha = elapsed > 0 ? 1.0f - powf(1.0f - decay, elapsed * 60.0f) : 0.0f;

  if (rain->rainbow_mode)
    color_filter_calculate_rainbow(sim_time, &rain->color_r, &rain->color_g, &rain->color_b);

  size_t input_len = strlen(frame);
  if (input_len > (SIZE_MAX - 1024) / 48)
    return NULL;
  size_t output_capacity = input_len * 48 + 1024;
  char *output = SAFE_MALLOC(output_capacity, char *);
  if (!output)
    return NULL;

  const char *src = frame;
  char *dst = output;
  size_t remaining = output_capacity;
  int col = 0, row = 0;
  float foreground_luminance = 1.0f, background_luminance = 0.0f;
  bool has_background = false;

  while (*src && remaining > 100) {
    if (*src == '\033') {
      int r, g, b;
      bool foreground;
      const char *after = parse_ansi_color(src, &r, &g, &b, &foreground);
      if (after) {
        float luminance = fminf(1.0f, fmaxf(0.0f, (0.2126f * r + 0.7152f * g + 0.0722f * b) / 255.0f));
        if (foreground)
          foreground_luminance = luminance;
        else {
          background_luminance = luminance;
          has_background = true;
        }
        src = after;
      } else {
        after = skip_ansi_sequence(src);
        size_t count = (size_t)(after - src);
        if ((count == 4 && memcmp(src, "\033[0m", 4) == 0) || (count == 3 && memcmp(src, "\033[m", 3) == 0)) {
          foreground_luminance = 1.0f;
          has_background = false;
        }
        memcpy(dst, src, count);
        dst += count;
        remaining -= count;
        src = after;
      }
      continue;
    }
    if (*src == '\n' || *src == '\r') {
      if (*src == '\n')
        row++;
      col = 0;
      *dst++ = *src++;
      remaining--;
      continue;
    }

    float head;
    float brightness = get_rain_brightness(rain, col, row, sim_time, &head);
    // Update once per cell, independently of how many ANSI sequences precede it.
    if (row < rain->num_rows && col < rain->num_columns) {
      size_t idx = (size_t)row * (size_t)rain->num_columns + (size_t)col;
      if (!rain->first_frame)
        brightness = rain->previous_brightness[idx] + (brightness - rain->previous_brightness[idx]) * alpha;
      rain->previous_brightness[idx] = brightness;
    }
    int count = generate_rain_color(dst, remaining, rain, brightness, head, foreground_luminance, true);
    dst += count;
    remaining -= (size_t)count;
    if (has_background) {
      count = generate_rain_color(dst, remaining, rain, brightness, head, background_luminance, false);
      dst += count;
      remaining -= (size_t)count;
    }
    uint32_t codepoint;
    int bytes = utf8_decode((const uint8_t *)src, &codepoint);
    if (bytes <= 0) {
      bytes = 1;
      codepoint = (uint8_t)*src;
    }
    if (bytes == 1 && codepoint > 32 && codepoint < 127) {
      *dst++ = (char)mutate_glyph(codepoint, col, row, sim_time);
      src++;
      remaining--;
    } else {
      memcpy(dst, src, (size_t)bytes);
      dst += bytes;
      src += bytes;
      remaining -= (size_t)bytes;
    }
    col++;
  }
  *dst = '\0';
  rain->first_frame = false;
  return output;
}
