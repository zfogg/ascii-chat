#include <ascii-chat/video/webcam/ios/camera.h>

#include <ascii-chat/video/rgba/image.h>
#include <ascii-chat/video/webcam/webcam.h>

#include <stdint.h>

static webcam_context_t *g_camera;
static unsigned short g_camera_index;

asciichat_error_t asciichat_ios_camera_start(unsigned short device_index) {
  if (g_camera) {
    if (g_camera_index == device_index)
      return ASCIICHAT_OK;
    return SET_ERRNO(ERROR_INVALID_STATE, "iOS camera is already using device %hu", g_camera_index);
  }
  asciichat_error_t result = webcam_init_context(&g_camera, device_index);
  if (result == ASCIICHAT_OK)
    g_camera_index = device_index;
  return result;
}

int asciichat_ios_camera_read(uint32_t *pixels, size_t capacity, size_t *width, size_t *height) {
  if (!g_camera || !pixels || !width || !height)
    return 0;

  image_t *frame = webcam_read_context(g_camera);
  if (!frame || frame->w <= 0 || frame->h <= 0)
    return 0;

  if ((size_t)frame->w > SIZE_MAX / (size_t)frame->h)
    return 0;
  const size_t pixel_count = (size_t)frame->w * (size_t)frame->h;
  if (capacity < pixel_count)
    return 0;

  for (size_t i = 0; i < pixel_count; ++i) {
    const rgb_pixel_t pixel = frame->pixels[i];
    pixels[i] = 0xFF000000U | ((uint32_t)pixel.r << 16) | ((uint32_t)pixel.g << 8) | pixel.b;
  }

  *width = (size_t)frame->w;
  *height = (size_t)frame->h;
  return 1;
}

void asciichat_ios_camera_stop(void) {
  if (!g_camera)
    return;
  webcam_cleanup_context(g_camera);
  g_camera = NULL;
  g_camera_index = 0;
}
