#include <ascii-chat/audio/visualization.h>

#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/platform/memory.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../video/anim/visualization_internal.h"

typedef struct {
  float frequency;
  unsigned char red;
  unsigned char green;
  unsigned char blue;
} waveform_color_stop_t;

static float history[AUDIO_VISUALIZATION_SOURCE_COUNT][VISUALIZATION_HISTORY];
static size_t write_position[AUDIO_VISUALIZATION_SOURCE_COUNT];
static static_mutex_t history_mutex = STATIC_MUTEX_INIT;

static void visualization_color_for_frequency(float centroid_hz, unsigned char *red, unsigned char *green,
                                              unsigned char *blue) {
  static const waveform_color_stop_t stops[] = {
      {45.0f, 255, 72, 176},    {100.0f, 255, 54, 106},    {220.0f, 255, 104, 72},  {350.0f, 190, 86, 220},
      {700.0f, 74, 112, 255},   {1400.0f, 55, 190, 255},   {2800.0f, 55, 226, 180}, {5200.0f, 150, 244, 92},
      {10000.0f, 255, 224, 76}, {18000.0f, 255, 246, 150},
  };
  const size_t stop_count = sizeof(stops) / sizeof(stops[0]);
  if (centroid_hz <= stops[0].frequency) {
    *red = stops[0].red;
    *green = stops[0].green;
    *blue = stops[0].blue;
    return;
  }
  for (size_t i = 1; i < stop_count; i++) {
    if (centroid_hz <= stops[i].frequency) {
      float low = logf(stops[i - 1].frequency);
      float high = logf(stops[i].frequency);
      float amount = (logf(centroid_hz) - low) / (high - low);
      *red = (unsigned char)lrintf((float)stops[i - 1].red + amount * ((float)stops[i].red - stops[i - 1].red));
      *green = (unsigned char)lrintf((float)stops[i - 1].green + amount * ((float)stops[i].green - stops[i - 1].green));
      *blue = (unsigned char)lrintf((float)stops[i - 1].blue + amount * ((float)stops[i].blue - stops[i - 1].blue));
      return;
    }
  }
  *red = stops[stop_count - 1].red;
  *green = stops[stop_count - 1].green;
  *blue = stops[stop_count - 1].blue;
}

void visualization_color_for_level(float frequency_hz, float level, int color_mode, unsigned char *red,
                                          unsigned char *green, unsigned char *blue) {
  visualization_color_for_frequency(frequency_hz, red, green, blue);
  float brightness = 0.5f + 0.5f * level;
  float white_mix = 0.0f;
  if (color_mode == AUDIO_VISUALIZATION_COLOR_DARKER) {
    brightness = 0.24f + 0.76f * level;
  } else if (color_mode == AUDIO_VISUALIZATION_COLOR_BRIGHTER) {
    brightness = 0.62f + 0.38f * level;
    white_mix = 0.10f;
  }
  *red = (unsigned char)lrintf(fminf(255.0f, *red * brightness + 255.0f * white_mix));
  *green = (unsigned char)lrintf(fminf(255.0f, *green * brightness + 255.0f * white_mix));
  *blue = (unsigned char)lrintf(fminf(255.0f, *blue * brightness + 255.0f * white_mix));
}

void audio_visualization_submit_stereo(audio_visualization_source_t source, const float *samples, size_t count) {
  if (!samples || count % 2)
    return;
  float mono[480];
  for (size_t offset = 0; offset < count;) {
    size_t frames = (count - offset) / 2;
    if (frames > 480)
      frames = 480;
    for (size_t i = 0; i < frames; i++)
      mono[i] = samples[offset + i * 2] * 0.5f + samples[offset + i * 2 + 1] * 0.5f;
    audio_visualization_submit(source, mono, frames);
    offset += frames * 2;
  }
}

void audio_visualization_submit(audio_visualization_source_t source, const float *samples, size_t count) {
  if ((!GET_OPTION(waveform) && !GET_OPTION(fft)) || !samples || source < AUDIO_VISUALIZATION_SOURCE_MIC ||
      source > AUDIO_VISUALIZATION_SOURCE_REMOTE)
    return;
  static_mutex_lock_named(&history_mutex, "audio_visualization_history");
  size_t position = write_position[source];
  for (size_t i = 0; i < count; i++) {
    float sample = samples[i];
    history[source][position] = isfinite(sample) ? fmaxf(-1.0f, fminf(1.0f, sample)) : 0.0f;
    position = (position + 1) % VISUALIZATION_HISTORY;
  }
  write_position[source] = position;
  static_mutex_unlock(&history_mutex);
}

void audio_visualization_read(audio_visualization_source_t source, float *samples, size_t count) {
  if (!samples)
    return;
  if (source < AUDIO_VISUALIZATION_SOURCE_MIC || source >= AUDIO_VISUALIZATION_SOURCE_COUNT) {
    memset(samples, 0, count * sizeof(float));
    return;
  }
  static_mutex_lock_named(&history_mutex, "audio_visualization_history");
  size_t available_count = count < VISUALIZATION_HISTORY ? count : VISUALIZATION_HISTORY;
  size_t output_offset = count - available_count;
  for (size_t i = 0; i < count; i++)
    samples[i] = 0.0f;
  size_t first_source = source;
  size_t last_source = source;
  if (source == AUDIO_VISUALIZATION_SOURCE_MIX) {
    first_source = AUDIO_VISUALIZATION_SOURCE_MIC;
    last_source = AUDIO_VISUALIZATION_SOURCE_REMOTE;
  } else if (source == AUDIO_VISUALIZATION_SOURCE_LOCAL_MIX) {
    first_source = AUDIO_VISUALIZATION_SOURCE_MIC;
    last_source = AUDIO_VISUALIZATION_SOURCE_MEDIA;
  }
  for (size_t current = first_source; current <= last_source; current++) {
    size_t start = (write_position[current] + VISUALIZATION_HISTORY - available_count) % VISUALIZATION_HISTORY;
    for (size_t i = 0; i < available_count; i++)
      samples[output_offset + i] += history[current][(start + i) % VISUALIZATION_HISTORY];
  }
  static_mutex_unlock(&history_mutex);
  for (size_t i = 0; i < count; i++)
    samples[i] = fmaxf(-1.0f, fminf(1.0f, samples[i]));
}

/* Encode the same visualization grid for native terminals and browser rasterization.
 * Half-block grids have two vertical samples per terminal row. */
char *visualization_encode(const char *cells, const unsigned char *colors, bool column_colors,
                                  unsigned int width, unsigned int height, bool use_color, bool flip_x, bool flip_y) {
  int mode = GET_OPTION(render_mode);
  bool half = mode == RENDER_MODE_HALF_BLOCK;
  unsigned int rows = half ? height / 2 : height;
  size_t capacity = (size_t)width * rows * 64 + rows + 1;
  char *frame = SAFE_MALLOC(capacity, char *);
  if (!frame)
    return NULL;
  size_t used = 0;
  for (unsigned int y = 0; y < rows; y++) {
    for (unsigned int x = 0; x < width; x++) {
      unsigned int sx = flip_x ? width - x - 1 : x;
      unsigned int sy = half ? y * 2 : y;
      unsigned int top = flip_y ? height - sy - 1 : sy;
      size_t cell = (size_t)top * width + sx;
      const unsigned char *color = colors + (column_colors ? sx : cell) * 3;
      char ch = cells[cell];
      if (half) {
        unsigned int bottom = flip_y ? height - sy - 2 : sy + 1;
        size_t lower = (size_t)bottom * width + sx;
        const unsigned char *lower_color = colors + (column_colors ? sx : lower) * 3;
        bool upper_on = ch != ' ', lower_on = cells[lower] != ' ';
        if (use_color) {
          used += (size_t)snprintf(frame + used, capacity - used, "\033[38;2;%u;%u;%um\033[48;2;%u;%u;%um▀\033[0m",
                                   upper_on ? color[0] : 0, upper_on ? color[1] : 0, upper_on ? color[2] : 0,
                                   lower_on ? lower_color[0] : 0, lower_on ? lower_color[1] : 0,
                                   lower_on ? lower_color[2] : 0);
        } else {
          const char *glyph = upper_on ? (lower_on ? "█" : "▀") : (lower_on ? "▄" : " ");
          size_t length = strlen(glyph);
          memcpy(frame + used, glyph, length);
          used += length;
        }
      } else if (use_color && ch != ' ') {
        used += (size_t)snprintf(frame + used, capacity - used, "\033[%d;2;%u;%u;%um%c\033[0m",
                                 mode == RENDER_MODE_BACKGROUND ? 48 : 38, color[0], color[1], color[2],
                                 mode == RENDER_MODE_BACKGROUND ? ' ' : ch);
      } else {
        frame[used++] = ch;
      }
    }
    frame[used++] = '\n';
  }
  frame[used] = '\0';
  return frame;
}
