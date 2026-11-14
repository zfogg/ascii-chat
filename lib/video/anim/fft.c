#include <ascii-chat/video/anim/fft.h>

#include "visualization_internal.h"

#include <ascii-chat/options/options.h>
#include <ascii-chat/common/sync.h>
#include <ascii-chat/debug/named.h>
#include <libplatform/memory.h>
#include <ascii-chat/util/time.h>

#include <math.h>
#include <string.h>
#include <fftw3.h>

#define FFT_SIZE 1024
#define FFT_MAX_COLUMNS MAX_TERMINAL_WIDTH
#define FFT_CACHE_CELLS (MAX_TERMINAL_WIDTH * MAX_TERMINAL_HEIGHT * 2)
#define FFT_REFRESH_NS (NS_PER_SEC_INT / 60)

static static_mutex_t fft_cache_mutex = STATIC_MUTEX_INIT;
static struct {
  char cells[FFT_CACHE_CELLS];
  unsigned char colors[FFT_CACHE_CELLS * 3];
  unsigned int width, height;
  audio_visualization_source_t source;
  int color_mode;
  terminal_render_mode_t render_mode;
  uint64_t updated_ns;
  bool valid;
} fft_cache;

static static_mutex_t fftw_mutex = STATIC_MUTEX_INIT;
static float fftwf_input[FFT_MAX_COLUMNS * FFT_SIZE];
static fftwf_complex fftwf_output[FFT_MAX_COLUMNS * (FFT_SIZE / 2 + 1)];
static fftwf_plan fftwf_forward;
extern static_mutex_t audio_fftw_planner_lock;
static float fft_window[FFT_SIZE];
static bool fft_window_ready;
static static_mutex_t fft_scale_mutex = STATIC_MUTEX_INIT;
static char fft_scale_text[32];
static fft_scale_t fft_scale_cached;
static bool fft_scale_cached_valid;

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

static fft_scale_t get_fft_scale(void) {
  const char *text = GET_OPTION(fft_scale);
  static_mutex_lock(&fft_scale_mutex);
  if (!fft_scale_cached_valid || strcmp(fft_scale_text, text ? text : "") != 0) {
    if (!parse_fft_scale_value(text, &fft_scale_cached))
      fft_scale_cached = (fft_scale_t){.mode = FFT_SCALE_DB, .min_db = -62.0f, .max_db = -6.0f};
    SAFE_STRNCPY(fft_scale_text, text ? text : "", sizeof(fft_scale_text));
    fft_scale_cached_valid = true;
  }
  fft_scale_t result = fft_scale_cached;
  static_mutex_unlock(&fft_scale_mutex);
  return result;
}

asciichat_error_t audio_visualization_fft_init(void) {
  (void)NAMED_REGISTER_MUTEX(&fft_cache_mutex, "audio.visualization.fft_cache", NULL);
  (void)NAMED_REGISTER_MUTEX(&fftw_mutex, "audio.visualization.fftw", NULL);
  (void)NAMED_REGISTER_MUTEX(&audio_fftw_planner_lock, "audio.fftw_planner", NULL);
  static_mutex_lock(&fftw_mutex);
  if (!fft_window_ready) {
    for (size_t i = 0; i < FFT_SIZE; i++)
      fft_window[i] = 0.5f - 0.5f * cosf(6.28318530718f * (float)i / (float)(FFT_SIZE - 1));
    fft_window_ready = true;
  }
  if (!fftwf_forward) {
    int n[] = {FFT_SIZE};
    int howmany = FFT_MAX_COLUMNS;
    int inembed[] = {FFT_SIZE};
    int onembed[] = {FFT_SIZE / 2 + 1};
    static_mutex_lock(&audio_fftw_planner_lock);
    fftwf_forward = fftwf_plan_many_dft_r2c(1, n, howmany, fftwf_input, inembed, 1, FFT_SIZE, fftwf_output, onembed, 1, FFT_SIZE / 2 + 1, FFTW_ESTIMATE);
    static_mutex_unlock(&audio_fftw_planner_lock);
  }
  bool ready = fftwf_forward != NULL;
  static_mutex_unlock(&fftw_mutex);
  return ready ? ASCIICHAT_OK : SET_ERRNO(ERROR_MEMORY, "Cannot create FFTW frequency display plan");
}

static bool visualization_fft_batch(const float *const input, unsigned int columns, float *power_spectrum) {
  static_mutex_lock(&fftw_mutex);
  if (!fftwf_forward) {
    static_mutex_unlock(&fftw_mutex);
    return false;
  }
  for (size_t i = 0; i < (size_t)columns * FFT_SIZE; i++)
    fftwf_input[i] = input[i];
  fftwf_execute(fftwf_forward);
  const float normalization = (float)FFT_SIZE * 0.25f;
  for (size_t column = 0; column < columns; column++)
    for (size_t i = 0; i <= FFT_SIZE / 2; i++) {
      size_t output = column * (FFT_SIZE / 2 + 1) + i;
      power_spectrum[output] = (fftwf_output[output][0] * fftwf_output[output][0] + fftwf_output[output][1] * fftwf_output[output][1]) / (normalization * normalization);
    }
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
  terminal_render_mode_t render_mode = (terminal_render_mode_t)GET_OPTION(render_mode);
  if (fft_cache.valid && fft_cache.width == width && fft_cache.height == height && fft_cache.source == source && fft_cache.color_mode == color_mode && fft_cache.render_mode == render_mode && now >= fft_cache.updated_ns &&
      now - fft_cache.updated_ns < FFT_REFRESH_NS) {
    size_t cached_cells = (size_t)width * height;
    char *cached = SAFE_MALLOC(cached_cells, char *);
    unsigned char *cached_colors = SAFE_MALLOC(cached_cells * 3, unsigned char *);
    if (!cached || !cached_colors) {
      SAFE_FREE(cached);
      SAFE_FREE(cached_colors);
      static_mutex_unlock(&fft_cache_mutex);
      return NULL;
    }
    memcpy(cached, fft_cache.cells, cached_cells);
    memcpy(cached_colors, fft_cache.colors, cached_cells * 3);
    static_mutex_unlock(&fft_cache_mutex);
    char *frame = visualization_encode(cached, cached_colors, false, width, height, use_color, flip_x, flip_y);
    SAFE_FREE(cached);
    SAFE_FREE(cached_colors);
    return frame;
  }
  static_mutex_unlock(&fft_cache_mutex);
  float *samples = SAFE_MALLOC(VISUALIZATION_HISTORY * sizeof(float), float *);
  if (!samples)
    return NULL;
  audio_visualization_read(source, samples, VISUALIZATION_HISTORY);
  size_t cell_count = (size_t)width * height;
  char *cells = SAFE_MALLOC(cell_count, char *);
  unsigned char *colors = SAFE_MALLOC(cell_count * 3, unsigned char *);
  if (!cells || !colors) {
    SAFE_FREE(cells);
    SAFE_FREE(colors);
    SAFE_FREE(samples);
    return NULL;
  }
  memset(cells, ' ', cell_count);
  memset(colors, 0, cell_count * 3);

  /* Preserve the time-history layout: each column is a spectrum from a different
   * overlapping window in the captured audio history. */
  float *windowed = SAFE_MALLOC((size_t)width * FFT_SIZE * sizeof(*windowed), float *);
  float *power_spectrum = SAFE_MALLOC((size_t)width * (FFT_SIZE / 2 + 1) * sizeof(*power_spectrum), float *);
  if (!windowed || !power_spectrum) {
    SAFE_FREE(windowed);
    SAFE_FREE(power_spectrum);
    SAFE_FREE(samples);
    SAFE_FREE(cells);
    SAFE_FREE(colors);
    return NULL;
  }
  fft_scale_t scale = get_fft_scale();
  static const char levels[] = " .:-=+*#%@";
  const unsigned int spectrum_height = height;
  size_t low_bins[MAX_TERMINAL_HEIGHT * 2], high_bins[MAX_TERMINAL_HEIGHT * 2], band_counts[MAX_TERMINAL_HEIGHT * 2];
  float band_hz[MAX_TERMINAL_HEIGHT * 2];
  for (unsigned int row = 0; row < spectrum_height; row++) {
    float top_fraction = (float)(spectrum_height - row - 1) / (float)spectrum_height;
    float bottom_fraction = (float)(spectrum_height - row) / (float)spectrum_height;
    float low_hz = 35.0f * powf(18000.0f / 35.0f, top_fraction);
    float high_hz = 35.0f * powf(18000.0f / 35.0f, bottom_fraction);
    size_t low_bin = (size_t)fmaxf(1.0f, floorf(low_hz * FFT_SIZE / AUDIO_VISUALIZATION_SAMPLE_RATE));
    size_t high_bin = (size_t)fmaxf((float)(low_bin + 1), ceilf(high_hz * FFT_SIZE / AUDIO_VISUALIZATION_SAMPLE_RATE));
    if (high_bin > FFT_SIZE / 2)
      high_bin = FFT_SIZE / 2;
    low_bins[row] = low_bin;
    high_bins[row] = high_bin;
    band_counts[row] = high_bin - low_bin;
    band_hz[row] = sqrtf(low_hz * high_hz);
  }
  for (unsigned int x = 0; x < width; x++) {
    size_t center = (size_t)x * (VISUALIZATION_HISTORY - 1) / (width - 1);
    size_t start = center > FFT_SIZE / 2 ? center - FFT_SIZE / 2 : 0;
    if (start + FFT_SIZE > VISUALIZATION_HISTORY)
      start = VISUALIZATION_HISTORY - FFT_SIZE;
    for (size_t i = 0; i < FFT_SIZE; i++)
      windowed[(size_t)x * FFT_SIZE + i] = samples[start + i] * fft_window[i];
  }
  if (!visualization_fft_batch(windowed, width, power_spectrum)) {
    SAFE_FREE(windowed);
    SAFE_FREE(power_spectrum);
    SAFE_FREE(samples);
    SAFE_FREE(cells);
    SAFE_FREE(colors);
    return NULL;
  }

  for (unsigned int x = 0; x < width; x++) {
    for (unsigned int row = 0; row < spectrum_height; row++) {
      float power = 0.0f;
      for (size_t bin = low_bins[row]; bin < high_bins[row]; bin++)
        power += power_spectrum[(size_t)x * (FFT_SIZE / 2 + 1) + bin];
      /* The default dB range leaves room for quiet speech while keeping loud music detailed. */
      float amplitude = band_counts[row] ? sqrtf(power / (float)band_counts[row]) : 0.0f;
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
      visualization_color_for_level(band_hz[row], intensity, color_mode, &colors[cell * 3], &colors[cell * 3 + 1], &colors[cell * 3 + 2]);
    }
  }

  static_mutex_lock(&fft_cache_mutex);
  memcpy(fft_cache.cells, cells, cell_count);
  memcpy(fft_cache.colors, colors, cell_count * 3);
  fft_cache.width = width;
  fft_cache.height = height;
  fft_cache.source = source;
  fft_cache.color_mode = color_mode;
  fft_cache.render_mode = render_mode;
  fft_cache.updated_ns = time_get_ns();
  fft_cache.valid = true;
  static_mutex_unlock(&fft_cache_mutex);
  char *frame = visualization_encode(cells, colors, false, width, height, use_color, flip_x, flip_y);
  SAFE_FREE(windowed);
  SAFE_FREE(power_spectrum);
  SAFE_FREE(cells);
  SAFE_FREE(colors);
  SAFE_FREE(samples);
  return frame;
}
