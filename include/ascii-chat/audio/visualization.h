#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
  AUDIO_VISUALIZATION_SOURCE_MIC = 0,
  AUDIO_VISUALIZATION_SOURCE_MEDIA,
  AUDIO_VISUALIZATION_SOURCE_REMOTE,
  AUDIO_VISUALIZATION_SOURCE_LOCAL_MIX,
  AUDIO_VISUALIZATION_SOURCE_MIX,
  AUDIO_VISUALIZATION_SOURCE_COUNT
} audio_visualization_source_t;

#define AUDIO_VISUALIZATION_SAMPLE_RATE 48000

#define AUDIO_VISUALIZATION_COLOR_STANDARD 0
#define AUDIO_VISUALIZATION_COLOR_DARKER 1
#define AUDIO_VISUALIZATION_COLOR_BRIGHTER 2

/* Publish mono float PCM without taking ownership of the caller's buffer. */
void audio_visualization_submit(audio_visualization_source_t source, const float *samples, size_t count);

/* Read the latest samples from one source, or combine the source groups. */
void audio_visualization_read(audio_visualization_source_t source, float *samples, size_t count);

/* Build a terminal-ready waveform frame. The caller owns the returned buffer. */
char *audio_visualization_render_waveform(unsigned int width, unsigned int height, audio_visualization_source_t source,
                                          bool use_color, int color_mode, bool flip_x, bool flip_y);

/* Build a terminal-ready scrolling frequency display. The caller owns the returned buffer. */
char *audio_visualization_render_fft(unsigned int width, unsigned int height, audio_visualization_source_t source,
                                     bool use_color, int color_mode, bool flip_x, bool flip_y);
