#include <ascii-chat/platform/ios/render_bridge.h>

#include <ascii-chat/media/render/renderer.h>
#include <ascii-chat/font.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/asciichat_errno.h>

#include <string.h>
#include <unistd.h>

static terminal_renderer_t *g_renderer;
static mutex_t g_renderer_mutex;
static bool g_renderer_mutex_initialized;
static bool g_renderer_initialized;

static bool ios_render_bridge_init(void) {
  if (!g_renderer_mutex_initialized) {
    if (mutex_init(&g_renderer_mutex, "ios_render_bridge") != 0)
      return false;
    g_renderer_mutex_initialized = true;
  }
  if (g_renderer_initialized)
    return g_renderer != NULL;

  term_renderer_config_t config = {
      .cols = 100,
      .rows = 40,
      .font_size_pt = 12.0,
      .theme = TERM_RENDERER_THEME_DARK,
      .font_data = g_font_default,
      .font_data_size = g_font_default_size,
  };
  g_renderer_initialized = true;
  if (term_renderer_create(&config, &g_renderer) != ASCIICHAT_OK)
    g_renderer = NULL;
  return g_renderer != NULL;
}

ssize_t ios_render_bridge_write(int fd, const void *data, size_t length) {
  if (!data || length == 0)
    return 0;
  if (fd != STDOUT_FILENO && fd != STDERR_FILENO)
    return write(fd, data, length);

  if (!ios_render_bridge_init())
    return (ssize_t)length;

  mutex_lock(&g_renderer_mutex);
  (void)term_renderer_feed(g_renderer, (const char *)data, length);
  mutex_unlock(&g_renderer_mutex);
  return (ssize_t)length;
}

int ios_render_bridge_read_frame(uint8_t *pixels, size_t capacity, int *width, int *height, int *pitch) {
  if (!pixels || !width || !height || !pitch || !ios_render_bridge_init())
    return 0;
  mutex_lock(&g_renderer_mutex);
  const int frame_width = term_renderer_width_px(g_renderer);
  const int frame_height = term_renderer_height_px(g_renderer);
  const int frame_pitch = term_renderer_pitch(g_renderer);
  const size_t frame_size = (size_t)frame_pitch * (size_t)frame_height;
  if (frame_width <= 0 || frame_height <= 0 || frame_pitch <= 0 || frame_size > capacity) {
    mutex_unlock(&g_renderer_mutex);
    return 0;
  }
  memcpy(pixels, term_renderer_pixels(g_renderer), frame_size);
  *width = frame_width;
  *height = frame_height;
  *pitch = frame_pitch;
  mutex_unlock(&g_renderer_mutex);
  return 1;
}

void ios_render_bridge_reset(void) {
  if (!g_renderer_mutex_initialized)
    return;
  mutex_lock(&g_renderer_mutex);
  if (g_renderer) {
    term_renderer_destroy(g_renderer);
    g_renderer = NULL;
  }
  g_renderer_initialized = false;
  mutex_unlock(&g_renderer_mutex);
}
