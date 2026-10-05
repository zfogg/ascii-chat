#include <ascii-chat/audio/visualization.h>

#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/platform/memory.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define VISUALIZATION_HISTORY 48000
#define MAX_TERMINAL_WIDTH 512
#define MAX_TERMINAL_HEIGHT 256

static float history[AUDIO_VISUALIZATION_SOURCE_COUNT][VISUALIZATION_HISTORY];
static size_t write_position[AUDIO_VISUALIZATION_SOURCE_COUNT];
static static_mutex_t history_mutex = STATIC_MUTEX_INIT;

void audio_visualization_submit(audio_visualization_source_t source, const float *samples, size_t count) {
  if (!GET_OPTION(waveform) || !samples || source < AUDIO_VISUALIZATION_SOURCE_MIC ||
      source > AUDIO_VISUALIZATION_SOURCE_REMOTE)
    return;
  static_mutex_lock(&history_mutex);
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
  static_mutex_lock(&history_mutex);
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

char *audio_visualization_render_waveform(unsigned int width, unsigned int height,
                                          audio_visualization_source_t source, bool use_color) {
  if (width < 8 || height < 4 || width > MAX_TERMINAL_WIDTH || height > MAX_TERMINAL_HEIGHT ||
      source < AUDIO_VISUALIZATION_SOURCE_MIC || source > AUDIO_VISUALIZATION_SOURCE_MIX)
    return NULL;

  size_t sample_count = (size_t)width * 12;
  float *samples = SAFE_MALLOC(sample_count * sizeof(float), float *);
  if (!samples)
    return NULL;
  audio_visualization_read(source, samples, sample_count);

  /* Reserve the first row for a quiet source label; the remaining grid is the waveform. */
  const unsigned int grid_height = height - 1;
  const unsigned int center = grid_height / 2;
  const unsigned int half_height = grid_height - 1 - center;
  const size_t cell_count = (size_t)width * grid_height;
  char *cells = SAFE_MALLOC(cell_count, char *);
  if (!cells) {
    SAFE_FREE(samples);
    return NULL;
  }
  memset(cells, ' ', cell_count);
  for (unsigned int x = 0; x < width; x++)
    cells[(size_t)center * width + x] = '-';

  for (unsigned int x = 0; x < width; x++) {
    size_t begin = (size_t)x * sample_count / width;
    size_t end = (size_t)(x + 1) * sample_count / width;
    float low = 0.0f, high = 0.0f;
    for (size_t i = begin; i < end; i++) {
      low = fminf(low, samples[i]);
      high = fmaxf(high, samples[i]);
    }
    unsigned int top = center - (unsigned int)lrintf(high * (float)half_height);
    unsigned int bottom = center - (unsigned int)lrintf(low * (float)half_height);
    for (unsigned int y = top; y <= bottom; y++)
      cells[(size_t)y * width + x] = '#';
  }

  static const char *source_names[] = {"MICROPHONE", "MEDIA", "REMOTE", "MIC + MEDIA", "ALL INPUTS"};
  size_t capacity = cell_count * (use_color ? 24U : 1U) + (size_t)height * 2 + 96;
  char *frame = SAFE_MALLOC(capacity, char *);
  if (!frame) {
    SAFE_FREE(cells);
    SAFE_FREE(samples);
    return NULL;
  }
  size_t used = (size_t)snprintf(frame, capacity, "AUDIO WAVEFORM  |  %s\n", source_names[source]);
  static const unsigned char colors[][3] = {{65, 145, 255}, {65, 220, 190}, {180, 230, 80}, {255, 215, 70},
                                             {255, 105, 80}, {255, 90, 175}};
  for (unsigned int y = 0; y < grid_height; y++) {
    for (unsigned int x = 0; x < width; x++) {
      char ch = cells[(size_t)y * width + x];
      if (use_color && ch == '#') {
        size_t begin = (size_t)x * sample_count / width;
        size_t end = (size_t)(x + 1) * sample_count / width;
        float peak = 0.0f;
        for (size_t i = begin; i < end; i++)
          peak = fmaxf(peak, fabsf(samples[i]));
        size_t color = (size_t)(peak * 5.99f);
        if (color > 5)
          color = 5;
        used += (size_t)snprintf(frame + used, capacity - used, "\033[38;2;%u;%u;%um#\033[0m",
                                 colors[color][0], colors[color][1], colors[color][2]);
      } else {
        frame[used++] = ch;
      }
    }
    frame[used++] = '\n';
  }
  frame[used] = '\0';
  SAFE_FREE(cells);
  SAFE_FREE(samples);
  return frame;
}
