#pragma once

#include <ascii-chat/audio/visualization.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Build a terminal-ready waveform frame. The caller owns the returned buffer. */
char *audio_visualization_render_waveform(unsigned int width, unsigned int height, audio_visualization_source_t source,
                                          bool use_color, int color_mode, bool flip_x, bool flip_y);

#ifdef __cplusplus
}
#endif
