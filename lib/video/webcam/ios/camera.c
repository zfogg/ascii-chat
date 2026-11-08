#include <ascii-chat/ios/camera.h>

#include <ascii-chat/video/rgba/image.h>
#include <ascii-chat/video/webcam/webcam.h>

static webcam_context_t *g_camera;

int asciichat_ios_camera_start(unsigned short device_index) {
  if (g_camera)
    return 1;
  return webcam_init_context(&g_camera, device_index) == ASCIICHAT_OK;
}

int asciichat_ios_camera_read(uint32_t *pixels, size_t capacity, size_t *width, size_t *height) {
  if (!g_camera || !pixels || !width || !height)
    return 0;

  image_t *frame = webcam_read_context(g_camera);
  if (!frame || frame->w <= 0 || frame->h <= 0)
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

int asciichat_ios_camera_read_ascii(char *output, size_t capacity, size_t columns, size_t rows, size_t *length) {
  static const char palette[] = " .:-=+*#%@";
  if (!g_camera || !output || !length || columns == 0 || rows == 0)
    return 0;

  image_t *frame = webcam_read_context(g_camera);
  if (!frame || frame->w <= 0 || frame->h <= 0)
    return 0;

  const size_t required = rows * (columns + 1) + 1;
  if (capacity < required)
    return 0;

  size_t cursor = 0;
  for (size_t row = 0; row < rows; ++row) {
    const size_t y = row * (size_t)frame->h / rows;
    for (size_t column = 0; column < columns; ++column) {
      const size_t x = column * (size_t)frame->w / columns;
      const rgb_pixel_t pixel = frame->pixels[y * (size_t)frame->w + x];
      const unsigned int luminance = (77U * pixel.r + 150U * pixel.g + 29U * pixel.b) / 256U;
      output[cursor++] = palette[(luminance * (sizeof(palette) - 2)) / 255U];
    }
    output[cursor++] = '\n';
  }
  output[cursor] = '\0';
  *length = cursor;
  return 1;
}

void asciichat_ios_camera_stop(void) {
  if (!g_camera)
    return;
  webcam_cleanup_context(g_camera);
  g_camera = NULL;
}
