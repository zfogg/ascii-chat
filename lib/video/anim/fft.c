#include <ascii-chat/video/anim/fft.h>

#include "visualization_internal.h"

#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/util/time.h>

#include <math.h>
#include <string.h>

#define FFT_SIZE 1024
#define FFT_CACHE_CELLS (MAX_TERMINAL_WIDTH * MAX_TERMINAL_HEIGHT * 2)
#define FFT_REFRESH_NS (100 * NS_PER_MS_INT)

static static_mutex_t fft_cache_mutex = STATIC_MUTEX_INIT;
static struct {
  char cells[FFT_CACHE_CELLS];
  unsigned char colors[FFT_CACHE_CELLS * 3];
  unsigned int width, height;
  audio_visualization_source_t source;
  int color_mode;
  uint64_t updated_ns;
  bool valid;
} fft_cache;

static void visualization_fft(float *real, float *imaginary) {
  for (size_t i = 1, j = 0; i < FFT_SIZE; i++) {
    size_t bit = FFT_SIZE >> 1;
    for (; j & bit; bit >>= 1)
      j ^= bit;
    j ^= bit;
    if (i < j) {
      float swap = real[i];
      real[i] = real[j];
      real[j] = swap;
      swap = imaginary[i];
      imaginary[i] = imaginary[j];
      imaginary[j] = swap;
    }
  }
  for (size_t length = 2; length <= FFT_SIZE; length <<= 1) {
    float angle = -6.28318530718f / (float)length;
    float step_real = cosf(angle), step_imaginary = sinf(angle);
    for (size_t base = 0; base < FFT_SIZE; base += length) {
      float twiddle_real = 1.0f, twiddle_imaginary = 0.0f;
      for (size_t offset = 0; offset < length / 2; offset++) {
        size_t even = base + offset, odd = even + length / 2;
        float odd_real = real[odd] * twiddle_real - imaginary[odd] * twiddle_imaginary;
        float odd_imaginary = real[odd] * twiddle_imaginary + imaginary[odd] * twiddle_real;
        real[odd] = real[even] - odd_real;
        imaginary[odd] = imaginary[even] - odd_imaginary;
        real[even] += odd_real;
        imaginary[even] += odd_imaginary;
        float next_real = twiddle_real * step_real - twiddle_imaginary * step_imaginary;
        twiddle_imaginary = twiddle_real * step_imaginary + twiddle_imaginary * step_real;
        twiddle_real = next_real;
      }
    }
  }
}

char *audio_visualization_render_fft(unsigned int width, unsigned int height, audio_visualization_source_t source,
                                     bool use_color, int color_mode, bool flip_x, bool flip_y) {
  if (width < 16 || height < 6 || width > MAX_TERMINAL_WIDTH || height > MAX_TERMINAL_HEIGHT ||
      source < AUDIO_VISUALIZATION_SOURCE_MIC || source > AUDIO_VISUALIZATION_SOURCE_MIX)
    return NULL;

  if (GET_OPTION(render_mode) == RENDER_MODE_HALF_BLOCK)
    height *= 2;
  static_mutex_lock_named(&fft_cache_mutex, "audio_visualization_fft_cache");
  uint64_t now = time_get_ns();
  if (fft_cache.valid && fft_cache.width == width && fft_cache.height == height && fft_cache.source == source &&
      fft_cache.color_mode == color_mode && now >= fft_cache.updated_ns &&
      now - fft_cache.updated_ns < FFT_REFRESH_NS) {
    char *frame =
        visualization_encode(fft_cache.cells, fft_cache.colors, false, width, height, use_color, flip_x, flip_y);
    static_mutex_unlock(&fft_cache_mutex);
    return frame;
  }
  float *samples = SAFE_MALLOC(VISUALIZATION_HISTORY * sizeof(float), float *);
  if (!samples) {
    static_mutex_unlock(&fft_cache_mutex);
    return NULL;
  }
  audio_visualization_read(source, samples, VISUALIZATION_HISTORY);
  size_t cell_count = (size_t)width * height;
  char *cells = fft_cache.cells;
  unsigned char *colors = fft_cache.colors;
  memset(cells, ' ', cell_count);
  memset(colors, 0, cell_count * 3);

  /* Each column is a 21 ms Hann-windowed spectrum; adjacent columns overlap heavily. */
  float real[FFT_SIZE], imaginary[FFT_SIZE];
  static const char levels[] = " .:-=+*#%@";
  const unsigned int spectrum_height = height;
  for (unsigned int x = 0; x < width; x++) {
    size_t center = (size_t)x * (VISUALIZATION_HISTORY - 1) / (width - 1);
    size_t start = center > FFT_SIZE / 2 ? center - FFT_SIZE / 2 : 0;
    if (start + FFT_SIZE > VISUALIZATION_HISTORY)
      start = VISUALIZATION_HISTORY - FFT_SIZE;
    for (size_t i = 0; i < FFT_SIZE; i++) {
      float window = 0.5f - 0.5f * cosf(6.28318530718f * (float)i / (float)(FFT_SIZE - 1));
      real[i] = samples[start + i] * window;
      imaginary[i] = 0.0f;
    }
    visualization_fft(real, imaginary);

    for (unsigned int row = 0; row < spectrum_height; row++) {
      float top_fraction = (float)(spectrum_height - row - 1) / (float)spectrum_height;
      float bottom_fraction = (float)(spectrum_height - row) / (float)spectrum_height;
      float low_hz = 35.0f * powf(18000.0f / 35.0f, top_fraction);
      float high_hz = 35.0f * powf(18000.0f / 35.0f, bottom_fraction);
      size_t low_bin = (size_t)fmaxf(1.0f, floorf(low_hz * FFT_SIZE / AUDIO_VISUALIZATION_SAMPLE_RATE));
      size_t high_bin =
          (size_t)fmaxf((float)(low_bin + 1), ceilf(high_hz * FFT_SIZE / AUDIO_VISUALIZATION_SAMPLE_RATE));
      if (high_bin > FFT_SIZE / 2)
        high_bin = FFT_SIZE / 2;
      float power = 0.0f;
      size_t bins = 0;
      for (size_t bin = low_bin; bin < high_bin; bin++) {
        float magnitude = hypotf(real[bin], imaginary[bin]) / (FFT_SIZE * 0.25f);
        power += magnitude * magnitude;
        bins++;
      }
      float magnitude = bins ? sqrtf(power / (float)bins) : 0.0f;
      /* dB scaling leaves room for quiet speech while keeping loud music detailed. */
      float db = 20.0f * log10f(fmaxf(magnitude, 0.00001f));
      float intensity = fmaxf(0.0f, fminf(1.0f, (db + 58.0f) / 52.0f));
      int level = (int)lrintf(intensity * (float)(sizeof(levels) - 2));
      size_t cell = (size_t)row * width + x;
      cells[cell] = levels[level];
      float band_hz = sqrtf(low_hz * high_hz);
      visualization_color_for_level(band_hz, intensity, color_mode, &colors[cell * 3], &colors[cell * 3 + 1],
                                    &colors[cell * 3 + 2]);
    }
  }

  fft_cache.width = width;
  fft_cache.height = height;
  fft_cache.source = source;
  fft_cache.color_mode = color_mode;
  fft_cache.updated_ns = time_get_ns();
  fft_cache.valid = true;
  char *frame = visualization_encode(cells, colors, false, width, height, use_color, flip_x, flip_y);
  SAFE_FREE(samples);
  static_mutex_unlock(&fft_cache_mutex);
  return frame;
}
