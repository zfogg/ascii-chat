/** Browser-facing C interface implemented in mirror.c. */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int mirror_init_with_args(const char *args_json);
char *mirror_convert_frame(uint8_t *rgba_data, int src_width, int src_height, int contain);
void mirror_free_string(char *ptr);
void mirror_submit_audio_samples(const float *samples, int count, int source);
char *mirror_render_audio_visualization(unsigned int width, unsigned int height, int source);

void mirror_cleanup(void);

void session_render_reset_stabilizer(void);
void session_render_cleanup(void);

#ifdef __cplusplus
}
#endif
