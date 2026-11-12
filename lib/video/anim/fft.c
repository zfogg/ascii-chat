#include <ascii-chat/video/anim/fft.h>

#include "visualization_internal.h"

#include <ascii-chat/options/options.h>
#include <ascii-chat/common/sync.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/os/memory.h>
#include <ascii-chat/util/time.h>

#include <math.h>
#include <string.h>
#include <fftw3.h>

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

static static_mutex_t fftw_mutex = STATIC_MUTEX_INIT;
static double fftw_input[FFT_SIZE];
static fftw_complex fftw_output[FFT_SIZE / 2 + 1];
static fftw_plan fftw_forward;
extern static_mutex_t audio_fftw_planner_lock;

static bool parse_fft_scale_value(const char *arg, fft_scale_t *parsed) {
  parsed->mode = FFT_SCALE_DB;
  parsed->min_db = -62.0f;
  parsed->max_db = -6.0f;
  if (!arg || !*arg)
    return false;
  if (!strcmp(arg, "linear")) {
    parsed->mode = FFT_SCALE_LINEAR;
    return true;
  }
  if (!strcmp(arg, "log")) {
    parsed->mode = FFT_SCALE_LOG;
    return true;
  }
  char *end = NULL;
  parsed->min_db = strtof(arg, &end);
  if (end == arg || *end != ':')
    return false;
  char *max_end = NULL;
  parsed->max_db = strtof(end + 1, &max_end);
  return max_end != end + 1 && *max_end == '\0' && isfinite(parsed->min_db) && isfinite(parsed->max_db) && parsed->min_db >= -120.0f && parsed->max_db <= 0.0f &&
         parsed->max_db - parsed->min_db >= 12.0f && parsed->max_db > parsed->min_db;
}

asciichat_error_t audio_visualization_fft_init(void) {
  (void)NAMED_REGISTER_MUTEX(&fft_cache_mutex, "audio.visualization.fft_cache", NULL);
  (void)NAMED_REGISTER_MUTEX(&fftw_mutex, "audio.visualization.fftw", NULL);
  (void)NAMED_REGISTER_MUTEX(&audio_fftw_planner_lock, "audio.fftw_planner", NULL);
  static_mutex_lock(&fftw_mutex);
  if (!fftw_forward) {
    static_mutex_lock(&audio_fftw_planner_lock);
    fftw_forward = fftw_plan_dft_r2c_1d(FFT_SIZE, fftw_input, fftw_output, FFTW_ESTIMATE);
    static_mutex_unlock(&audio_fftw_planner_lock);
  }
  bool ready = fftw_forward != NULL;
  static_mutex_unlock(&fftw_mutex);
  return ready ? ASCIICHAT_OK : SET_ERRNO(ERROR_MEMORY, "Cannot create FFTW visualization plan");
}

static bool visualization_fft(const float *input, float *magnitude) {
  static_mutex_lock(&fftw_mutex);
  if (!fftw_forward) {
    static_mutex_unlock(&fftw_mutex);
    return false;
  }
  for (size_t i = 0; i < FFT_SIZE; i++)
    fftw_input[i] = input[i];
  fftw_execute(fftw_forward);
  for (size_t i = 0; i <= FFT_SIZE / 2; i++)
    magnitude[i] = (float)hypot(fftw_output[i][0], fftw_output[i][1]) / (FFT_SIZE * 0.25f);
  static_mutex_unlock(&fftw_mutex);
  return true;
}

char *audio_visualization_render_fft(unsigned int width, unsigned int height, audio_visualization_source_t source, bool use_color, int color_mode, bool flip_x, bool flip_y) {
  if (width < 16 || height < 6 || width > MAX_TERMINAL_WIDTH || height > MAX_TERMINAL_HEIGHT || source < AUDIO_VISUALIZATION_SOURCE_MIC || source > AUDIO_VISUALIZATION_SOURCE_MIX)
    return NULL;

  if (GET_OPTION(render_mode) == RENDER_MODE_HALF_BLOCK)
    height *= 2;
  static_mutex_lock(&fft_cache_mutex);
  uint64_t now = time_get_ns();
  if (fft_cache.valid && fft_cache.width == width && fft_cache.height == height && fft_cache.source == source && fft_cache.color_mode == color_mode && now >= fft_cache.updated_ns &&
      now - fft_cache.updated_ns < FFT_REFRESH_NS) {
    char *frame = visualization_encode(fft_cache.cells, fft_cache.colors, false, width, height, use_color, flip_x, flip_y);
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
  float windowed[FFT_SIZE], magnitude[FFT_SIZE / 2 + 1];
  fft_scale_t scale;
  if (!parse_fft_scale_value(GET_OPTION(fft_scale), &scale))
    scale = (fft_scale_t){.mode = FFT_SCALE_DB, .min_db = -62.0f, .max_db = -6.0f};
  static const char levels[] = " .:-=+*#%@";
  const unsigned int spectrum_height = height;
  for (unsigned int x = 0; x < width; x++) {
    size_t center = (size_t)x * (VISUALIZATION_HISTORY - 1) / (width - 1);
    size_t start = center > FFT_SIZE / 2 ? center - FFT_SIZE / 2 : 0;
    if (start + FFT_SIZE > VISUALIZATION_HISTORY)
      start = VISUALIZATION_HISTORY - FFT_SIZE;
    for (size_t i = 0; i < FFT_SIZE; i++) {
      float window = 0.5f - 0.5f * cosf(6.28318530718f * (float)i / (float)(FFT_SIZE - 1));
      windowed[i] = samples[start + i] * window;
    }
    if (!visualization_fft(windowed, magnitude)) {
      SAFE_FREE(samples);
      static_mutex_unlock(&fft_cache_mutex);
      return NULL;
    }

    for (unsigned int row = 0; row < spectrum_height; row++) {
      float top_fraction = (float)(spectrum_height - row - 1) / (float)spectrum_height;
      float bottom_fraction = (float)(spectrum_height - row) / (float)spectrum_height;
      float low_hz = 35.0f * powf(18000.0f / 35.0f, top_fraction);
      float high_hz = 35.0f * powf(18000.0f / 35.0f, bottom_fraction);
      size_t low_bin = (size_t)fmaxf(1.0f, floorf(low_hz * FFT_SIZE / AUDIO_VISUALIZATION_SAMPLE_RATE));
      size_t high_bin = (size_t)fmaxf((float)(low_bin + 1), ceilf(high_hz * FFT_SIZE / AUDIO_VISUALIZATION_SAMPLE_RATE));
      if (high_bin > FFT_SIZE / 2)
        high_bin = FFT_SIZE / 2;
      float power = 0.0f;
      size_t bins = 0;
      for (size_t bin = low_bin; bin < high_bin; bin++) {
        power += magnitude[bin] * magnitude[bin];
        bins++;
      }
      /* The default dB range leaves room for quiet speech while keeping loud music detailed. */
      float amplitude = bins ? sqrtf(power / (float)bins) : 0.0f;
      float intensity;
      if (scale.mode == FFT_SCALE_LINEAR)
        intensity = fminf(1.0f, amplitude * 4.0f);
      else if (scale.mode == FFT_SCALE_LOG)
        intensity = fminf(1.0f, log1pf(amplitude * 31.0f) / log1pf(31.0f));
      else {
        float db = 20.0f * log10f(fmaxf(amplitude, 0.00001f));
        intensity = fmaxf(0.0f, fminf(1.0f, (db - scale.min_db) / (scale.max_db - scale.min_db)));
      }
      int level = (int)lrintf(intensity * (float)(sizeof(levels) - 2));
      size_t cell = (size_t)row * width + x;
      cells[cell] = levels[level];
      float band_hz = sqrtf(low_hz * high_hz);
      visualization_color_for_level(band_hz, intensity, color_mode, &colors[cell * 3], &colors[cell * 3 + 1], &colors[cell * 3 + 2]);
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
