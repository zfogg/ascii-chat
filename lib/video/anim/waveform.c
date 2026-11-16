#include <ascii-chat/video/anim/waveform.h>

#include "visualization_internal.h"

#include <options/options.h>
#include <libplatform/memory.h>

#include <math.h>
#include <string.h>

static float waveform_spectral_centroid(const float *samples, size_t sample_count, size_t center) {
  const size_t window_size = 1024;
  const size_t half_window = window_size / 2;
  size_t start = center > half_window ? center - half_window : 0;
  if (start + window_size > sample_count)
    start = sample_count - window_size;

  /* A zero-crossing estimate is sufficient for the decorative color ramp
   * and is linear in the window size. The previous nine resonator passes
   * made every waveform column do substantially more work than necessary. */
  size_t crossings = 0;
  for (size_t i = 1; i < window_size; i++) {
    float previous = samples[start + i - 1];
    float current = samples[start + i];
    if ((previous < 0.0f && current >= 0.0f) || (previous >= 0.0f && current < 0.0f))
      crossings++;
  }
  return 22.0f * (float)crossings;
}

char *audio_visualization_render_waveform(unsigned int width, unsigned int height, audio_visualization_source_t source, bool use_color, int color_mode, bool flip_x, bool flip_y) {
  if (width < 8 || height < 4 || width > MAX_TERMINAL_WIDTH || height > MAX_TERMINAL_HEIGHT || source < AUDIO_VISUALIZATION_SOURCE_MIC || source > AUDIO_VISUALIZATION_SOURCE_MIX)
    return NULL;

  /* Keep a fixed one-second view so new audio enters on the right smoothly. */
  const size_t sample_count = VISUALIZATION_HISTORY;
  float *samples = SAFE_MALLOC(sample_count * sizeof(float), float *);
  if (!samples)
    return NULL;
  audio_visualization_read(source, samples, sample_count);

  const unsigned int grid_height = GET_OPTION(render_mode) == RENDER_MODE_HALF_BLOCK ? height * 2 : height;
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

  float positive_peak = 0.0f;
  float negative_peak = 0.0f;
  size_t x = 0;

  for (size_t i = 0; i < sample_count; i++) {
    float sample = samples[i];
    positive_peak = fmaxf(positive_peak, sample);
    negative_peak = fmaxf(negative_peak, -sample);

    size_t next_bucket = ((i + 1) * width) / sample_count;
    if (next_bucket != x || i + 1 == sample_count) {
      size_t begin = x * sample_count / width;
      size_t end = (x + 1) * sample_count / width;
      size_t bucket_center = begin + (end - begin) / 2;
      float centroid = waveform_spectral_centroid(samples, sample_count, bucket_center);
      /* A gentle square-root scale keeps speech visible without flattening loud peaks. */
      float level = sqrtf(fminf(fmaxf(positive_peak, negative_peak), 1.0f));
      visualization_color_for_level(centroid, level, color_mode, &palette[x][0], &palette[x][1], &palette[x][2]);
      unsigned int positive_amplitude = (unsigned int)lrintf(sqrtf(fminf(positive_peak, 1.0f)) * (float)half_height);
      unsigned int negative_amplitude = (unsigned int)lrintf(sqrtf(fminf(negative_peak, 1.0f)) * (float)half_height);
      if (positive_amplitude > half_height)
        positive_amplitude = half_height;
      if (negative_amplitude > half_height)
        negative_amplitude = half_height;
      unsigned int top = center - positive_amplitude;
      unsigned int bottom = center + negative_amplitude;
      for (unsigned int y = top; y <= bottom; y++) {
        char ch = (y == top || y == bottom) ? '+' : '#';
        cells[(size_t)y * width + x] = ch;
      }

      /* Advance to the next time slice in the left-to-right history. */
      positive_peak = 0.0f;
      negative_peak = 0.0f;
      x = next_bucket;
    }
  }

  char *frame = visualization_encode(cells, &palette[0][0], true, width, grid_height, use_color, flip_x, flip_y);
  SAFE_FREE(cells);
  SAFE_FREE(samples);
  return frame;
}
