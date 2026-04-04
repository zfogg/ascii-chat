/**
 * @file mirror_wasm.c
 * @brief WASM entry point for ascii-chat mirror mode
 */

#include <emscripten.h>
#include <stdlib.h>
#include <string.h>

#include "common/init.h"

#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/video/ascii/ascii.h>
#include <ascii-chat/video/rgba/color_filter.h>
#include <ascii-chat/video/rgba/image.h>
#include <ascii-chat/video/ascii/palette.h>
#include <ascii-chat/video/terminal/ansi.h>
#include <ascii-chat/video/anim/digital_rain.h>
#include <ascii-chat/util/aspect_ratio.h>
#include <ascii-chat/common.h>
#include <ascii-chat/media/render/renderer.h>

// Global digital rain effect context
static digital_rain_t *g_digital_rain = NULL;
static char *g_last_rain_output = NULL;
static double g_last_rain_update_time = 0.0;
#define RAIN_UPDATE_INTERVAL_MS 100.0 // Update every 100ms for smooth animation

// Frame counter for time-based effects (emscripten_get_now is unreliable in pthreads builds)
static uint32_t g_frame_count = 0;

// ============================================================================
// Initialization
// ============================================================================

/**
 * Initialize mirror mode with command-line style arguments
 * @param args_json JSON array of argument strings, e.g. ["mirror", "--width", "80", "--height", "40"]
 * @return 0 on success, -1 on error
 */
EMSCRIPTEN_KEEPALIVE
int mirror_init_with_args(const char *args_json) {
  // Initialize platform layer
  asciichat_error_t err = platform_init();
  if (err != ASCIICHAT_OK) {
    return -1;
  }

  // Initialize logging to stderr (console.error in browser)
  log_init(NULL, LOG_DEBUG, true, false);
  // Use debug-style format in WASM so browser console shows full log headers
  log_set_format(OPT_LOG_TEMPLATE_DEFAULT_DEBUG, false);

  // Parse space-separated arguments
  char *args_copy = NULL;
  char *argv[64] = {NULL};
  int argc = wasm_parse_args(args_json, argv, 64, &args_copy);
  if (argc < 0) {
    return -1;
  }

  // Initialize options (sets up RCU, defaults, etc.)
  err = options_init(argc, argv);
  free(args_copy);

  if (err != ASCIICHAT_OK) {
    return -1;
  }

  // Log option values after initialization
  log_info("[mirror_init_with_args] After options_init: argc=%d, matrix_rain=%d", argc, GET_OPTION(matrix_rain));

  // Initialize ANSI color code generation (dec3 cache for RGB values)
  ansi_fast_init();

  return 0;
}

EMSCRIPTEN_KEEPALIVE
void mirror_cleanup(void) {
  if (g_digital_rain) {
    digital_rain_destroy(g_digital_rain);
    g_digital_rain = NULL;
  }
  if (g_last_rain_output) {
    SAFE_FREE(g_last_rain_output);
    g_last_rain_output = NULL;
  }
  term_renderer_glyph_cache_cleanup();
  options_state_destroy();
  platform_destroy();
}

// ============================================================================
// Frame Conversion API
// ============================================================================

EMSCRIPTEN_KEEPALIVE
char *mirror_convert_frame(uint8_t *rgba_data, int src_width, int src_height) {
  if (!rgba_data || src_width <= 0 || src_height <= 0) {
    return NULL;
  }

  g_frame_count++;
  // Derive a time value from frame count using actual target FPS from options
  int target_fps = (int)GET_OPTION(fps);
  if (target_fps <= 0) {
    target_fps = 60; // Default to 60 FPS if not set
  }
  float time_seconds = (float)g_frame_count / (float)target_fps;

  // Get current settings from options or terminal detection
  int dst_width = (int)terminal_get_effective_width();
  int dst_height = (int)terminal_get_effective_height();
  color_filter_t filter = (color_filter_t)GET_OPTION(color_filter);
  terminal_color_mode_t color_mode = (terminal_color_mode_t)GET_OPTION(color_mode);
  if (g_frame_count <= 3 || g_frame_count % 120 == 0) {
    log_debug("frame=%u color_filter=%d color_mode=%d", g_frame_count, (int)filter, (int)color_mode);
  }
  if (color_mode == TERM_COLOR_AUTO) {
    color_mode = TERM_COLOR_TRUECOLOR; // xterm.js supports truecolor
  }
  palette_type_t palette_type = (palette_type_t)GET_OPTION(palette_type);
  bool preserve_aspect_ratio = true; // Preserve webcam aspect ratio
  bool stretch = false;              // Don't stretch - maintain proportions

  // Build terminal capabilities structure with user's color mode
  terminal_capabilities_t caps = {0}; // Zero-initialize all fields first
  caps.color_level = color_mode;
  caps.capabilities = 0;
  caps.color_count = (color_mode == TERM_COLOR_NONE)  ? 0
                     : (color_mode == TERM_COLOR_16)  ? 16
                     : (color_mode == TERM_COLOR_256) ? 256
                                                      : 16777216;
  caps.utf8_support = true;
  caps.detection_reliable = true;
  caps.render_mode = (render_mode_t)GET_OPTION(render_mode);
  caps.wants_background = (caps.render_mode == RENDER_MODE_BACKGROUND);
  caps.wants_padding = true;
  caps.palette_type = palette_type;
  caps.desired_fps = 60;
  caps.color_filter = filter;

  // Convert RGBA to RGB (strip alpha channel) and optionally flip horizontally
  rgb_pixel_t *rgb_pixels = SAFE_MALLOC(src_width * src_height * sizeof(rgb_pixel_t), rgb_pixel_t *);
  if (!rgb_pixels) {
    return NULL;
  }

  bool flip_x = GET_OPTION(flip_x);
  bool flip_y = GET_OPTION(flip_y);
  for (int y = 0; y < src_height; y++) {
    for (int x = 0; x < src_width; x++) {
      int src_x = flip_x ? (src_width - 1 - x) : x;
      int src_y = flip_y ? (src_height - 1 - y) : y;
      int src_idx = (src_y * src_width + src_x) * 4;
      int dst_idx = y * src_width + x;

      rgb_pixels[dst_idx].r = rgba_data[src_idx + 0];
      rgb_pixels[dst_idx].g = rgba_data[src_idx + 1];
      rgb_pixels[dst_idx].b = rgba_data[src_idx + 2];
      // Alpha (rgba_data[src_idx + 3]) is discarded
    }
  }

  // Apply color filter to pixels if needed (except rainbow - handled in ANSI stage)
  if (filter != COLOR_FILTER_NONE && filter != COLOR_FILTER_RAINBOW) {
    // color_filter operates on packed RGB24 format
    uint8_t *rgb24 = (uint8_t *)rgb_pixels;
    int stride = src_width * 3;
    apply_color_filter(rgb24, src_width, src_height, stride, filter, time_seconds);
  }

  // Create image structure
  image_t img = {.w = src_width, .h = src_height, .pixels = rgb_pixels, .alloc_method = IMAGE_ALLOC_SIMD};

  // Get palette characters based on selected palette type
  const char *palette_chars;
  switch (palette_type) {
  case PALETTE_BLOCKS:
    palette_chars = PALETTE_CHARS_BLOCKS;
    break;
  case PALETTE_DIGITAL:
    palette_chars = PALETTE_CHARS_DIGITAL;
    break;
  case PALETTE_MINIMAL:
    palette_chars = PALETTE_CHARS_MINIMAL;
    break;
  case PALETTE_COOL:
    palette_chars = PALETTE_CHARS_COOL;
    break;
  case PALETTE_CUSTOM:
    // Custom palette uses user-provided characters from options
    palette_chars = GET_OPTION(palette_custom);
    if (!palette_chars || palette_chars[0] == '\0') {
      palette_chars = PALETTE_CHARS_STANDARD; // Fallback
    }
    break;
  case PALETTE_STANDARD:
  default:
    palette_chars = PALETTE_CHARS_STANDARD;
    break;
  }

  // Convert to ASCII using capability-aware function
  char *ascii_output = ascii_convert_with_capabilities(&img, dst_width, dst_height, &caps, preserve_aspect_ratio,
                                                       stretch, palette_chars);

  // Clean up
  SAFE_FREE(rgb_pixels);

  if (!ascii_output) {
    log_error("ascii_convert_with_capabilities returned NULL");
    return NULL;
  }

  // Apply rainbow color filter to ANSI output by replacing RGB values
  // This preserves character selection while applying rainbow colors
  if (filter == COLOR_FILTER_RAINBOW) {
    char *rainbow_output = rainbow_replace_ansi_colors(ascii_output, time_seconds);
    if (rainbow_output) {
      SAFE_FREE(ascii_output);
      ascii_output = rainbow_output;
    }
  }

  bool matrix_rain = GET_OPTION(matrix_rain);
  if (matrix_rain) {
    // Initialize digital rain context if needed or dimensions changed
    if (!g_digital_rain || g_digital_rain->num_columns != dst_width || g_digital_rain->num_rows != dst_height) {
      if (g_digital_rain) {
        digital_rain_destroy(g_digital_rain);
      }
      if (g_last_rain_output) {
        SAFE_FREE(g_last_rain_output);
        g_last_rain_output = NULL;
      }
      g_digital_rain = digital_rain_init(dst_width, dst_height);
      if (!g_digital_rain) {
        log_error("Failed to initialize digital rain effect");
        return ascii_output; // Return without effect on error
      }
      g_last_rain_update_time = emscripten_get_now();
    }

    // Update color from active filter (allows filter changes after initialization)
    digital_rain_set_color_from_filter(g_digital_rain, filter);

    // Time-based updates: only update effect every RAIN_UPDATE_INTERVAL_MS
    double current_time = emscripten_get_now();
    double elapsed_ms = current_time - g_last_rain_update_time;

    if (elapsed_ms >= RAIN_UPDATE_INTERVAL_MS) {
      // Calculate actual delta time in seconds
      float delta_time = (float)(elapsed_ms / 1000.0);
      g_last_rain_update_time = current_time;

      // Apply effect and cache a copy
      char *rain_output = digital_rain_apply(g_digital_rain, ascii_output, delta_time);
      if (rain_output) {
        // Free old cached output
        if (g_last_rain_output) {
          SAFE_FREE(g_last_rain_output);
        }
        // Cache a copy (we'll return the original rain_output)
        g_last_rain_output = strdup(rain_output);
        // Return the rain output (caller will free it)
        SAFE_FREE(ascii_output);
        ascii_output = rain_output;
      }
    } else if (g_last_rain_output) {
      // Use cached output
      SAFE_FREE(ascii_output);
      ascii_output = strdup(g_last_rain_output);
    }
  } else {
    // Clean up digital rain context if it exists but effect is disabled
    if (g_digital_rain) {
      digital_rain_destroy(g_digital_rain);
      g_digital_rain = NULL;
    }
    if (g_last_rain_output) {
      SAFE_FREE(g_last_rain_output);
      g_last_rain_output = NULL;
    }
    g_last_rain_update_time = 0.0;
  }

  return ascii_output;
}

EMSCRIPTEN_KEEPALIVE
void mirror_free_string(char *ptr) {
  SAFE_FREE(ptr);
}

// Embedded font data (defined in generated/data/fonts/default.c and matrix.c)
extern const unsigned char g_font_default[];
extern const size_t g_font_default_size;
extern const unsigned char g_font_matrix_resurrected[];
extern const size_t g_font_matrix_resurrected_size;

/**
 * Get the pointer to the embedded default font data
 * @return Pointer to font data
 */
EMSCRIPTEN_KEEPALIVE
const unsigned char *get_font_default_ptr(void) {
  // Auto-select matrix font when --matrix flag is set
  int matrix_rain_val = GET_OPTION(matrix_rain);
  log_info("[get_font_default_ptr] CALLED: matrix_rain=%d, g_font_matrix_resurrected=%p, g_font_default=%p",
           matrix_rain_val, (void *)g_font_matrix_resurrected, (void *)g_font_default);
  if (matrix_rain_val) {
    log_info("[get_font_default_ptr] RETURNING matrix font");
    return g_font_matrix_resurrected;
  }
  log_info("[get_font_default_ptr] RETURNING default font");
  return g_font_default;
}

/**
 * Get the size of the embedded default font
 * @return Font data size in bytes
 */
EMSCRIPTEN_KEEPALIVE
unsigned int get_font_default_size(void) {
  // Auto-select matrix font when --matrix flag is set
  if (GET_OPTION(matrix_rain)) {
    return (unsigned int)g_font_matrix_resurrected_size;
  }
  return (unsigned int)g_font_default_size;
}
