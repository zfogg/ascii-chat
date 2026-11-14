#pragma once

#include <ascii-chat/audio/visualization.h>
#include <ascii-chat/asciichat_errno.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Amplitude mapping used by the terminal FFT visualization. */
typedef enum { FFT_SCALE_LINEAR = 0, FFT_SCALE_LOG = 1, FFT_SCALE_DB = 2 } fft_scale_mode_t;

typedef struct {
  fft_scale_mode_t mode;
  float min_db;
  float max_db;
} fft_scale_t;

/** Create the shared FFTW visualization plan before rendering begins. */
asciichat_error_t audio_visualization_fft_init(void);

/* Build a terminal-ready scrolling frequency display frame. The caller owns the returned buffer. */
char *audio_visualization_render_fft(unsigned int width, unsigned int height, audio_visualization_source_t source,
                                     bool use_color, int color_mode, bool flip_x, bool flip_y);

#ifdef __cplusplus
}
#endif
