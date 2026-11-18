#include <ascii-chat/os/ios/render_bridge.h>

#include <ascii-chat/common.h>
#include <ascii-chat/media/render/renderer.h>
#include <ascii-chat/font.h>
#include <ascii-chat/platform/font.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/log/log.h>
#include <libplatform/frame_flush.h>

#include <string.h>
#include <stdatomic.h>
#include <unistd.h>

static terminal_renderer_t *g_renderer;
static mutex_t g_renderer_mutex;
static bool g_renderer_mutex_initialized;
static bool g_renderer_initialized;
static atomic_uint_fast64_t g_render_sequence;
#define IOS_RENDER_PENDING_CAPACITY (256U * 1024U)
static char *g_pending_frame;
static size_t g_pending_frame_capacity;
static size_t g_pending_frame_length;
void ios_render_bridge_flush(void);

static void ios_render_bridge_flush_callback(void) {
  ios_render_bridge_flush();
}
// The native app must configure the bridge from the actual drawable before
// starting a C mode. There is no valid fallback grid for a device display.
static int g_requested_cols;
static int g_requested_rows;
static double g_requested_font_size_pt = 112.0;
static char g_requested_font_spec[1024];

static bool ios_render_bridge_init(void) {
  if (!g_renderer_mutex_initialized) {
    if (mutex_init(&g_renderer_mutex, "ios_render_bridge") != 0)
      return false;
    g_renderer_mutex_initialized = true;
  }
  if (g_renderer_initialized)
    return g_renderer != NULL;

  if (!g_pending_frame) {
    g_pending_frame = SAFE_MALLOC(IOS_RENDER_PENDING_CAPACITY, char *);
    g_pending_frame_capacity = IOS_RENDER_PENDING_CAPACITY;
  }

  char resolved_font[sizeof(g_requested_font_spec)] = {0};
  bool font_is_path = false;
  const uint8_t *font_data = NULL;
  size_t font_data_size = 0;
  if (platform_font_resolve(g_requested_font_spec, resolved_font, sizeof(resolved_font), &font_is_path, &font_data,
                            &font_data_size) != ASCIICHAT_OK) {
    strlcpy(resolved_font, "default", sizeof(resolved_font));
    font_data = g_font_default;
    font_data_size = g_font_default_size;
  }
  term_renderer_config_t config = {
      // The bridge can initialize before the options TLS context exists. The
      // iOS dispatcher passes the same compact dimensions to the C pipeline.
      .cols = g_requested_cols,
      .rows = g_requested_rows,
      .font_size_pt = g_requested_font_size_pt,
      // The Metal drawable is cleared to black. Use the dark terminal palette
      // so shared TUI text keeps a readable light default foreground while
      // preserving the same background as the media renderer.
      .theme = TERM_RENDERER_THEME_DARK,
      .font_data = font_data,
      .font_data_size = font_data_size,
  };
  strlcpy(config.font_spec, resolved_font, sizeof(config.font_spec));
  config.font_is_path = font_is_path;
  g_renderer_initialized = true;
  asciichat_error_t create_result = term_renderer_create(&config, &g_renderer);
  if (create_result != ASCIICHAT_OK && font_data != g_font_default) {
    // A user-selected font must never turn a live native session into a black
    // surface. Keep the option persisted, but recover through the same
    // bundled default used by the rest of the shared renderer.
    log_warn("iOS render font '%s' could not be initialized; falling back to bundled default: %s", resolved_font,
             asciichat_error_format(NULL, create_result));
    config.font_data = g_font_default;
    config.font_data_size = g_font_default_size;
    config.font_is_path = false;
    strlcpy(config.font_spec, "default", sizeof(config.font_spec));
    create_result = term_renderer_create(&config, &g_renderer);
  }
  if (create_result != ASCIICHAT_OK)
    g_renderer = NULL;
  return g_renderer != NULL;
}

void ios_render_bridge_configure(int cols, int rows, double font_size_pt) {
  ios_render_bridge_configure_font(cols, rows, font_size_pt, NULL);
}

void ios_render_bridge_configure_font(int cols, int rows, double font_size_pt, const char *font_spec) {
  if (cols <= 0 || rows <= 0 || font_size_pt <= 0.0)
    return;
  platform_frame_flush_set_callback(ios_render_bridge_flush_callback);
  if (!g_renderer_mutex_initialized) {
    if (mutex_init(&g_renderer_mutex, "ios_render_bridge") != 0)
      return;
    g_renderer_mutex_initialized = true;
  }
  mutex_lock(&g_renderer_mutex);
  const char *requested_font = font_spec ? font_spec : "";
  if (g_requested_cols != cols || g_requested_rows != rows || g_requested_font_size_pt != font_size_pt ||
      strncmp(g_requested_font_spec, requested_font, sizeof(g_requested_font_spec)) != 0) {
    g_requested_cols = cols;
    g_requested_rows = rows;
    g_requested_font_size_pt = font_size_pt;
    strlcpy(g_requested_font_spec, requested_font, sizeof(g_requested_font_spec));
    if (g_renderer)
      term_renderer_destroy(g_renderer);
    g_renderer = NULL;
    g_renderer_initialized = false;
    g_pending_frame_length = 0;
    atomic_store_explicit(&g_render_sequence, 0, memory_order_release);
  }
  mutex_unlock(&g_renderer_mutex);
  log_info("iOS bridge configured cols=%d rows=%d font=%.1f spec=%s", cols, rows, font_size_pt,
           requested_font[0] ? requested_font : "default");
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
  if (length > SIZE_MAX - g_pending_frame_length) {
    mutex_unlock(&g_renderer_mutex);
    return -1;
  }
  const size_t required = g_pending_frame_length + length;
  if (required > g_pending_frame_capacity) {
    size_t next_capacity = g_pending_frame_capacity > 0 ? g_pending_frame_capacity : IOS_RENDER_PENDING_CAPACITY;
    while (next_capacity < required) {
      if (next_capacity > SIZE_MAX / 2) {
        next_capacity = required;
        break;
      }
      next_capacity *= 2;
    }
    g_pending_frame = SAFE_REALLOC(g_pending_frame, next_capacity, char *);
    g_pending_frame_capacity = next_capacity;
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
  size_t flushed_length = g_pending_frame_length;
  if (g_pending_frame_length > 0) {
    asciichat_error_t result = term_renderer_feed(g_renderer, g_pending_frame, g_pending_frame_length);
    if (result != ASCIICHAT_OK)
      log_error("iOS render bridge rejected %zu-byte frame: %s", g_pending_frame_length,
                asciichat_error_format(NULL, result));
    g_pending_frame_length = 0;
  }
  // The Swift display link also polls this function. Do not publish a new
  // frame for an empty poll: client-mode presentations are queued on the
  // shared UI controller and may arrive between display-link ticks. The
  // sequence is a data-availability signal, not a heartbeat.
  if (flushed_length > 0) {
    atomic_fetch_add_explicit(&g_render_sequence, 1, memory_order_release);
    static uint64_t flush_count;
    flush_count++;
    if ((flush_count % 60) == 0)
      log_debug("iOS bridge flush count=%llu bytes=%zu", (unsigned long long)flush_count, flushed_length);
  }
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
  SAFE_FREE(g_pending_frame);
  g_pending_frame_capacity = 0;
  g_renderer_initialized = false;
  g_pending_frame_length = 0;
  atomic_store_explicit(&g_render_sequence, 0, memory_order_release);
  mutex_unlock(&g_renderer_mutex);
}

uint64_t ios_render_bridge_sequence(void) {
  return atomic_load_explicit(&g_render_sequence, memory_order_acquire);
}
