#ifdef WIN32

#include "webcam.h"
#include "webcam_platform.h"
#include "common.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

// Windows webcam implementation stub
// TODO: Implement proper Media Foundation support

struct webcam_context_t {
  int width;
  int height;
  int frame_counter;
  uint8_t *dummy_frame;
};

int webcam_platform_init(webcam_context_t **ctx, unsigned short int device_index) {
  log_info("Opening Windows webcam (stub) with device index %d", device_index);

  webcam_context_t *cam;
  SAFE_MALLOC(cam, sizeof(webcam_context_t), webcam_context_t *);
  
  // Default webcam resolution for testing
  cam->width = 640;
  cam->height = 480;
  cam->frame_counter = 0;

  // Allocate a dummy frame for testing (RGB format)
  size_t frame_size = cam->width * cam->height * 3;
  SAFE_MALLOC(cam->dummy_frame, frame_size, uint8_t *);

  // Fill with a test pattern (gradient)
  for (int y = 0; y < cam->height; y++) {
    for (int x = 0; x < cam->width; x++) {
      int idx = (y * cam->width + x) * 3;
      cam->dummy_frame[idx + 0] = (x * 255) / cam->width;      // R
      cam->dummy_frame[idx + 1] = (y * 255) / cam->height;     // G
      cam->dummy_frame[idx + 2] = 128;                         // B
    }
  }

  *ctx = cam;
  log_info("Windows webcam stub initialized (test pattern mode)");
  return 0;
}

void webcam_platform_cleanup(webcam_context_t *ctx) {
  if (ctx) {
    SAFE_FREE(ctx->dummy_frame);
    SAFE_FREE(ctx);
    log_info("Windows webcam stub closed");
  }
}

image_t *webcam_platform_read(webcam_context_t *ctx) {
  if (!ctx) return NULL;

  // Simulate frame capture with slight variation
  ctx->frame_counter++;

  // Add some animation to the test pattern
  int offset = (ctx->frame_counter * 2) % 256;
  for (int i = 0; i < ctx->width * ctx->height; i++) {
    ctx->dummy_frame[i * 3 + 2] = (128 + offset) % 256;
  }

  // Convert to image_t format
  image_t *img;
  SAFE_MALLOC(img, sizeof(image_t), image_t *);
  
  img->w = ctx->width;
  img->h = ctx->height;
  SAFE_MALLOC(img->pixels, ctx->width * ctx->height * sizeof(rgb_t), rgb_t *);

  // Convert RGB buffer to rgb_t array
  for (int i = 0; i < ctx->width * ctx->height; i++) {
    img->pixels[i].r = ctx->dummy_frame[i * 3 + 0];
    img->pixels[i].g = ctx->dummy_frame[i * 3 + 1];
    img->pixels[i].b = ctx->dummy_frame[i * 3 + 2];
  }

  return img;
}

int webcam_platform_get_dimensions(webcam_context_t *ctx, int *width, int *height) {
  if (!ctx || !width || !height)
    return -1;

  *width = ctx->width;
  *height = ctx->height;
  return 0;
}

// Platform detection functions
webcam_platform_type_t webcam_get_platform(void) {
  return WEBCAM_PLATFORM_UNKNOWN; // Windows support is stub for now
}

const char *webcam_platform_name(webcam_platform_type_t platform) {
  switch (platform) {
  case WEBCAM_PLATFORM_V4L2:
    return "V4L2 (Linux)";
  case WEBCAM_PLATFORM_AVFOUNDATION:
    return "AVFoundation (macOS)";
  case WEBCAM_PLATFORM_UNKNOWN:
  default:
    return "Windows (stub)";
  }
}

#endif