#include <ascii-chat/video/face_follow_image.h>
#include <ascii-chat/video/face_follow.h>
#include <ascii-chat/asciichat_errno.h>
#include <math.h>

struct face_follow_image {
  face_follow_state_t state;
  unsigned char bgr[320 * 240 * 3], results[0x9000];
  double last_detection;
  int source_width, source_height;
  image_t *output;
};

void face_follow_image_destroy(face_follow_image_t *context) {
  if (!context)
    return;
  image_destroy(context->output);
  context->output = NULL;
  SAFE_FREE(context);
}

image_t *face_follow_image_process(face_follow_image_t **context, image_t *source, double now, bool enabled, bool debug,
                                   int max_width, int max_height) {
  if (!source || source->w <= 0 || source->h <= 0)
    return source;
  if (!*context && !enabled && !debug)
    return source;
  if (!*context) {
    *context = SAFE_CALLOC(1, sizeof(face_follow_image_t), face_follow_image_t *);
    if (!*context)
      return source;
    face_follow_init(&(*context)->state);
  }
  face_follow_image_t *ctx = *context;
  if (source->w != ctx->source_width || source->h != ctx->source_height) {
    face_follow_init(&ctx->state);
    ctx->last_detection = -1;
    ctx->source_width = source->w;
    ctx->source_height = source->h;
  }
  if ((enabled || debug) && now - ctx->last_detection >= 0.1) {
    float scale = fminf(320.0f / source->w, 240.0f / source->h);
    int width = (int)fmaxf(1, source->w * scale), height = (int)fmaxf(1, source->h * scale);
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        rgb_pixel_t p = source->pixels[(size_t)(y * source->h / height) * source->w + x * source->w / width];
        size_t offset = ((size_t)y * width + x) * 3;
        ctx->bgr[offset] = p.b;
        ctx->bgr[offset + 1] = p.g;
        ctx->bgr[offset + 2] = p.r;
      }
    }
    face_follow_detection_t faces[32];
    size_t count;
    face_follow_detect(ctx->results, ctx->bgr, width, height, faces, 32, &count);
    face_follow_observe(&ctx->state, faces, count, now);
    ctx->last_detection = now;
  }
  face_follow_tick(&ctx->state, now, enabled);
  if (!enabled && !debug && ctx->state.crop.w == 1)
    return source;
  // Keep output geometry stable while bounding the cost of high-resolution cameras.
  float output_scale = fminf(1, fminf((float)max_width / source->w, (float)max_height / source->h));
  int output_width = (int)fmaxf(1, source->w * output_scale);
  int output_height = (int)fmaxf(1, source->h * output_scale);
  if (!ctx->output || ctx->output->w != output_width || ctx->output->h != output_height) {
    image_destroy(ctx->output);
    ctx->output = image_new(output_width, output_height);
    if (!ctx->output)
      return source;
  }
  face_follow_rect_t crop = ctx->state.crop;
  face_follow_rect_t face = ctx->state.face;
  float side = fmaxf(face.w * source->w, face.h * source->h);
  float fx = (face.x + face.w * 0.5f) * source->w, fy = (face.y + face.h * 0.5f) * source->h;
  float thickness = 2 * fmaxf(3, source->w * crop.w / 90);
  for (int y = 0; y < output_height; y++) {
    int sy = (int)fminf(source->h - 1, (crop.y + (float)y / output_height * crop.h) * source->h);
    for (int x = 0; x < output_width; x++) {
      int sx = (int)fminf(source->w - 1, (crop.x + (float)x / output_width * crop.w) * source->w);
      rgb_pixel_t p = source->pixels[(size_t)sy * source->w + sx];
      if (debug && ctx->state.detected && now - ctx->state.last_seen < 0.3) {
        float dx = fabsf(sx - fx), dy = fabsf(sy - fy);
        if ((fabsf(dx - side * 0.5f) < thickness && dy <= side * 0.5f + thickness) ||
            (fabsf(dy - side * 0.5f) < thickness && dx <= side * 0.5f + thickness))
          p = (rgb_pixel_t){255, 0, 0};
      }
      ctx->output->pixels[(size_t)y * output_width + x] = p;
    }
  }
  return ctx->output;
}
