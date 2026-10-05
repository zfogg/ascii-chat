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

typedef struct {
  float frequency;
  unsigned char red;
  unsigned char green;
  unsigned char blue;
} waveform_color_stop_t;

static float history[AUDIO_VISUALIZATION_SOURCE_COUNT][VISUALIZATION_HISTORY];
static size_t write_position[AUDIO_VISUALIZATION_SOURCE_COUNT];
static static_mutex_t history_mutex = STATIC_MUTEX_INIT;

static void waveform_color_for_centroid(float centroid_hz, unsigned char *red, unsigned char *green,
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

static float waveform_spectral_centroid(const float *samples, size_t sample_count, size_t center) {
  static const float frequencies[] = {55.0f, 110.0f, 220.0f, 440.0f, 880.0f, 1760.0f, 3520.0f, 7040.0f, 14080.0f};
  const size_t window_size = 1024;
  const size_t half_window = window_size / 2;
  size_t start = center > half_window ? center - half_window : 0;
  if (start + window_size > sample_count)
    start = sample_count - window_size;

  float weighted_log_frequency = 0.0f;
  float total_weight = 0.0f;
  for (size_t frequency_index = 0; frequency_index < sizeof(frequencies) / sizeof(frequencies[0]); frequency_index++) {
    float coefficient = 2.0f * cosf(6.28318530718f * frequencies[frequency_index] / AUDIO_VISUALIZATION_SAMPLE_RATE);
    float q1 = 0.0f, q2 = 0.0f;
    for (size_t i = 0; i < window_size; i++) {
      float position = (float)i / (float)(window_size - 1);
      float taper = 1.0f - fabsf(2.0f * position - 1.0f);
      float q0 = coefficient * q1 - q2 + samples[start + i] * taper;
      q2 = q1;
      q1 = q0;
    }
    float power = fmaxf(q1 * q1 + q2 * q2 - coefficient * q1 * q2, 0.0f);
    float magnitude = sqrtf(power);
    weighted_log_frequency += magnitude * logf(frequencies[frequency_index]);
    total_weight += magnitude;
  }
  return total_weight > 0.0001f ? expf(weighted_log_frequency / total_weight) : 900.0f;
}

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

char *audio_visualization_render_waveform(unsigned int width, unsigned int height, audio_visualization_source_t source,
                                          bool use_color) {
  if (width < 8 || height < 4 || width > MAX_TERMINAL_WIDTH || height > MAX_TERMINAL_HEIGHT ||
      source < AUDIO_VISUALIZATION_SOURCE_MIC || source > AUDIO_VISUALIZATION_SOURCE_MIX)
    return NULL;

  /* Keep a fixed one-second view so new audio enters on the right smoothly. */
  const size_t sample_count = VISUALIZATION_HISTORY;
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
  unsigned char palette[MAX_TERMINAL_WIDTH][3] = {{0}};

  float peak = 0.0f;
  size_t x = 0;

  for (size_t i = 0; i < sample_count; i++) {
    float sample = samples[i];
    peak = fmaxf(peak, fabsf(sample));

    size_t next_bucket = ((i + 1) * width) / sample_count;
    if (next_bucket != x || i + 1 == sample_count) {
      size_t begin = x * sample_count / width;
      size_t end = (x + 1) * sample_count / width;
      size_t bucket_center = begin + (end - begin) / 2;
      float centroid = waveform_spectral_centroid(samples, sample_count, bucket_center);
      unsigned char red, green, blue;
      waveform_color_for_centroid(centroid, &red, &green, &blue);

      /* A gentle square-root scale keeps speech visible without flattening loud peaks. */
      float level = sqrtf(fminf(peak, 1.0f));
      float brightness = 0.5f + 0.5f * level;
      palette[x][0] = (unsigned char)lrintf((float)red * brightness);
      palette[x][1] = (unsigned char)lrintf((float)green * brightness);
      palette[x][2] = (unsigned char)lrintf((float)blue * brightness);
      unsigned int amplitude = (unsigned int)lrintf(level * (float)half_height);
      if (amplitude > half_height)
        amplitude = half_height;
      unsigned int top = center - amplitude;
      unsigned int bottom = center + amplitude;
      for (unsigned int y = top; y <= bottom; y++) {
        char ch = (y == top || y == bottom) ? '+' : '#';
        cells[(size_t)y * width + x] = ch;
      }

      /* Advance to the next time slice in the left-to-right history. */
      peak = 0.0f;
      x = next_bucket;
    }
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
  for (unsigned int y = 0; y < grid_height; y++) {
    for (unsigned int x = 0; x < width; x++) {
      char ch = cells[(size_t)y * width + x];
      if (use_color && ch != ' ') {
        used += (size_t)snprintf(frame + used, capacity - used, "\033[38;2;%u;%u;%um%c\033[0m", palette[x][0],
                                 palette[x][1], palette[x][2], ch);
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
