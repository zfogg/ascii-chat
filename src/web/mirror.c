/**
 * @file mirror_wasm.c
 * @brief WASM entry point for ascii-chat mirror mode
 */

#include <emscripten.h>
#include <stdlib.h>
#include <string.h>

// Include shared logging macros (uses console.log by default)
#include "common/wasm_log.h"
#include "common/init.h"

#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/video/ascii.h>
#include <ascii-chat/video/color_filter.h>
#include <ascii-chat/video/image.h>
#include <ascii-chat/video/palette.h>
#include <ascii-chat/video/ansi_fast.h>
#include <ascii-chat/video/digital_rain.h>
#include <ascii-chat/common.h>

// Global digital rain effect context
static digital_rain_t *g_digital_rain = NULL;
static char *g_last_rain_output = NULL;
static double g_last_rain_update_time = 0.0;
#define RAIN_UPDATE_INTERVAL_MS 100.0 // Update every 100ms for smooth animation

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
  WASM_LOG("mirror_init_with_args: START");
  WASM_LOG("mirror_init_with_args: After START log");

  // Initialize platform layer
  WASM_LOG("Calling platform_init...");
  WASM_LOG("mirror_init_with_args: About to call platform_init");
  asciichat_error_t err = platform_init();
  WASM_LOG("mirror_init_with_args: platform_init returned");
  if (err != ASCIICHAT_OK) {
    WASM_ERROR("platform_init FAILED");
    return -1;
  }
  WASM_LOG("platform_init OK");

  // Initialize logging to stderr (console.error in browser)
  WASM_LOG("Calling log_init...");
  log_init(NULL, LOG_DEBUG, true, false);
  WASM_LOG("log_init OK");

  // Parse space-separated arguments
  WASM_LOG("Parsing arguments...");
  char *args_copy = NULL;
  char *argv[64] = {NULL};
  int argc = wasm_parse_args(args_json, argv, 64, &args_copy);
  if (argc < 0) {
    WASM_ERROR("strdup FAILED");
    return -1;
  }
  WASM_LOG_INT("Parsed arguments, argc", argc);

  // Initialize options (sets up RCU, defaults, etc.)
  WASM_LOG("Calling options_init...");
  err = options_init(argc, argv);
  free(args_copy);

  if (err != ASCIICHAT_OK) {
    WASM_LOG_INT("options_init FAILED", err);
    return -1;
  }
  WASM_LOG("options_init OK");

  // Initialize ANSI color code generation (dec3 cache for RGB values)
  WASM_LOG("Calling ansi_fast_init...");
  ansi_fast_init();
  WASM_LOG("ansi_fast_init OK");

  WASM_LOG("mirror_init_with_args: COMPLETE");
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

  // Get current settings from options
  int dst_width = GET_OPTION(width);
  int dst_height = GET_OPTION(height);
  color_filter_t filter = (color_filter_t)GET_OPTION(color_filter);
  terminal_color_mode_t color_mode = (terminal_color_mode_t)GET_OPTION(color_mode);
  palette_type_t palette_type = (palette_type_t)GET_OPTION(palette_type);
  bool aspect_ratio = true; // Preserve webcam aspect ratio
  bool stretch = false;     // Don't stretch - maintain proportions

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
  caps.wants_background = false;
  caps.palette_type = palette_type;
  caps.desired_fps = 60;
  caps.color_filter = filter;

  // Convert RGBA to RGB (strip alpha channel) and optionally flip horizontally
  rgb_pixel_t *rgb_pixels = SAFE_MALLOC(src_width * src_height * sizeof(rgb_pixel_t), rgb_pixel_t *);
  if (!rgb_pixels) {
    return NULL;
  }

  bool flip = GET_OPTION(flip_x);
  for (int y = 0; y < src_height; y++) {
    for (int x = 0; x < src_width; x++) {
      int src_x = flip ? (src_width - 1 - x) : x;
      int src_idx = (y * src_width + src_x) * 4;
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
    float time_seconds = (float)(emscripten_get_now() / 1000.0); // Convert ms to seconds
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
  char *ascii_output =
      ascii_convert_with_capabilities(&img, dst_width, dst_height, &caps, aspect_ratio, stretch, palette_chars);

  // Clean up
  SAFE_FREE(rgb_pixels);

  if (!ascii_output) {
    log_error("ascii_convert_with_capabilities returned NULL");
    return NULL;
  }

  // Apply rainbow color filter to ANSI output by replacing RGB values
  // This preserves character selection while applying rainbow colors
  if (filter == COLOR_FILTER_RAINBOW) {
    float time_seconds = (float)(emscripten_get_now() / 1000.0); // Convert ms to seconds
    char *rainbow_output = rainbow_replace_ansi_colors(ascii_output, time_seconds);
    if (rainbow_output) {
      SAFE_FREE(ascii_output);
      ascii_output = rainbow_output;
    }
  }

  // Apply digital rain effect if enabled
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
