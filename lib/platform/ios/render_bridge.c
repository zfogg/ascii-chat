#include <ascii-chat/platform/ios/render_bridge.h>

#include <ascii-chat/media/render/renderer.h>
#include <ascii-chat/font.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/log/log.h>

#include <string.h>
#include <stdatomic.h>
#include <unistd.h>

static terminal_renderer_t *g_renderer;
static mutex_t g_renderer_mutex;
static bool g_renderer_mutex_initialized;
static bool g_renderer_initialized;
static atomic_uint_fast64_t g_render_sequence;
#define IOS_RENDER_PENDING_CAPACITY (256U * 1024U)
static char g_pending_frame[IOS_RENDER_PENDING_CAPACITY];
static size_t g_pending_frame_length;
// The native app must configure the bridge from the actual drawable before
// starting a C mode. There is no valid fallback grid for a device display.
static int g_requested_cols;
static int g_requested_rows;
static double g_requested_font_size_pt = 6.0;

static bool ios_render_bridge_init(void) {
  if (!g_renderer_mutex_initialized) {
    if (mutex_init(&g_renderer_mutex, "ios_render_bridge") != 0)
      return false;
    g_renderer_mutex_initialized = true;
  }
  if (g_renderer_initialized)
    return g_renderer != NULL;

  term_renderer_config_t config = {
      // The bridge can initialize before the options TLS context exists. The
      // iOS dispatcher passes the same compact dimensions to the C pipeline.
      .cols = g_requested_cols,
      .rows = g_requested_rows,
      .font_size_pt = g_requested_font_size_pt,
      // SwiftUI presents the raster on a light surface; use the matching
      // terminal palette so default glyphs remain visible in the native view.
      .theme = TERM_RENDERER_THEME_LIGHT,
      .font_data = g_font_default,
      .font_data_size = g_font_default_size,
  };
  g_renderer_initialized = true;
  if (term_renderer_create(&config, &g_renderer) != ASCIICHAT_OK)
    g_renderer = NULL;
  return g_renderer != NULL;
}

void ios_render_bridge_configure(int cols, int rows, double font_size_pt) {
  if (cols <= 0 || rows <= 0 || font_size_pt <= 0.0)
    return;
  if (!g_renderer_mutex_initialized) {
    if (mutex_init(&g_renderer_mutex, "ios_render_bridge") != 0)
      return;
    g_renderer_mutex_initialized = true;
  }
  mutex_lock(&g_renderer_mutex);
  if (g_requested_cols != cols || g_requested_rows != rows || g_requested_font_size_pt != font_size_pt) {
    g_requested_cols = cols;
    g_requested_rows = rows;
    g_requested_font_size_pt = font_size_pt;
    if (g_renderer)
      term_renderer_destroy(g_renderer);
    g_renderer = NULL;
    g_renderer_initialized = false;
    g_pending_frame_length = 0;
    atomic_store_explicit(&g_render_sequence, 0, memory_order_release);
  }
  mutex_unlock(&g_renderer_mutex);
  (void)ios_render_bridge_init();
}

void ios_render_bridge_get_dimensions(int *cols, int *rows) {
  if (cols)
    *cols = g_requested_cols;
  if (rows)
    *rows = g_requested_rows;
}

ssize_t ios_render_bridge_write(int fd, const void *data, size_t length) {
  if (!data || length == 0)
    return 0;
  if (fd != STDOUT_FILENO && fd != STDERR_FILENO)
    return write(fd, data, length);

  if (!ios_render_bridge_init())
    return (ssize_t)length;

  mutex_lock(&g_renderer_mutex);
  if (length > sizeof(g_pending_frame) - g_pending_frame_length) {
    log_error("iOS render bridge frame exceeds %zu-byte pending buffer", sizeof(g_pending_frame));
    g_pending_frame_length = 0;
    mutex_unlock(&g_renderer_mutex);
    return -1;
  }
  memcpy(g_pending_frame + g_pending_frame_length, data, length);
  g_pending_frame_length += length;
  mutex_unlock(&g_renderer_mutex);
  return (ssize_t)length;
}

void ios_render_bridge_flush(void) {
  if (!ios_render_bridge_init())
    return;
  mutex_lock(&g_renderer_mutex);
  if (g_pending_frame_length > 0) {
    asciichat_error_t result = term_renderer_feed(g_renderer, g_pending_frame, g_pending_frame_length);
    if (result != ASCIICHAT_OK)
      log_error("iOS render bridge rejected %zu-byte frame: %s", g_pending_frame_length,
                asciichat_error_format(NULL, result));
    g_pending_frame_length = 0;
  }
  atomic_fetch_add_explicit(&g_render_sequence, 1, memory_order_release);
  mutex_unlock(&g_renderer_mutex);
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
  const uint8_t *source = term_renderer_pixels(g_renderer);
  for (int row = 0; row < frame_height; row++) {
    const uint8_t *source_row = source + (size_t)row * (size_t)frame_pitch;
    uint8_t *destination_row = pixels + (size_t)row * (size_t)frame_pitch;
    for (int column = 0; column < frame_width; column++) {
      destination_row[column * 4] = source_row[column * 4 + 2];
      destination_row[column * 4 + 1] = source_row[column * 4 + 1];
      destination_row[column * 4 + 2] = source_row[column * 4];
      destination_row[column * 4 + 3] = 255;
    }
  }
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
  g_pending_frame_length = 0;
  atomic_store_explicit(&g_render_sequence, 0, memory_order_release);
  mutex_unlock(&g_renderer_mutex);
}

uint64_t ios_render_bridge_sequence(void) {
  return atomic_load_explicit(&g_render_sequence, memory_order_acquire);
}
