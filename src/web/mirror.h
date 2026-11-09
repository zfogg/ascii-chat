/** Browser-facing C interface implemented in mirror.c. */
#pragma once

#include <stdint.h>
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/anim/digital_rain.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mirror_render_context {
  digital_rain_t *digital_rain;
  double last_rain_update_time;
  uint32_t frame_count;
  ascii_stabilizer_t stabilizer;
} mirror_render_context_t;

mirror_render_context_t *mirror_render_context_create(void);
void mirror_render_context_destroy(mirror_render_context_t *context);
void mirror_render_context_reset(mirror_render_context_t *context);
int mirror_init(void);
char *mirror_convert_frame(mirror_render_context_t *context, uint8_t *rgba_data, int src_width, int src_height, int contain);
void mirror_free_string(char *ptr);
void mirror_submit_audio_samples(mirror_render_context_t *context, const float *samples, int count, int source);
char *mirror_render_audio_visualization(mirror_render_context_t *context, unsigned int width, unsigned int height, int source);

void mirror_cleanup(void);


#ifdef __cplusplus
}
#endif
