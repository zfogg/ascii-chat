/**
 * @file mirror_wasm.c
 * @brief WASM entry point for ascii-chat mirror mode
 */

#include <emscripten.h>
#include <stdlib.h>
#include <string.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>

// Forward declare parser
bool parse_palette_type(const char *arg, void *dest, char **error_msg);
#include <ascii-chat/platform/init.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/video/ascii.h>
#include <ascii-chat/video/color_filter.h>
#include <ascii-chat/video/image.h>
#include <ascii-chat/video/palette.h>
#include <ascii-chat/video/ansi_fast.h>
#include <ascii-chat/common.h>

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

  // Parse JSON array into argc/argv
  // For simplicity, we'll accept a space-separated string instead
  // JS can pass: "mirror --width 80 --height 40 --color-filter grayscale"
  char *args_copy = strdup(args_json);
  if (!args_copy) {
    return -1;
  }

  // Count arguments
  int argc = 0;
  char *argv[64] = {NULL}; // Max 64 arguments
  char *token = strtok(args_copy, " ");
  while (token != NULL && argc < 63) {
    argv[argc++] = token;
    token = strtok(NULL, " ");
  }
  argv[argc] = NULL;

  // Initialize options (sets up RCU, defaults, etc.)
  err = options_init(argc, argv);
  free(args_copy);

  if (err != ASCIICHAT_OK) {
    return -1;
  }

  // Initialize ANSI color code generation (dec3 cache for RGB values)
  ansi_fast_init();

  return 0;
}

EMSCRIPTEN_KEEPALIVE
void mirror_cleanup(void) {
  options_state_destroy();
  platform_destroy();
}

// ============================================================================
// Settings API - Dimension Getters/Setters
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int mirror_set_width(int width) {
  if (width <= 0 || width > 1000) {
    return -1;
  }
  asciichat_error_t err = options_set_int("width", width);
  return (err == ASCIICHAT_OK) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int mirror_set_height(int height) {
  if (height <= 0 || height > 1000) {
    return -1;
  }
  asciichat_error_t err = options_set_int("height", height);
  return (err == ASCIICHAT_OK) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int mirror_get_width(void) {
  return GET_OPTION(width);
}

EMSCRIPTEN_KEEPALIVE
int mirror_get_height(void) {
  return GET_OPTION(height);
}

// ============================================================================
// Settings API - Render Mode
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int mirror_set_render_mode(int mode) {
  if (mode < RENDER_MODE_FOREGROUND || mode > RENDER_MODE_HALF_BLOCK) {
    return -1;
  }
  asciichat_error_t err = options_set_int("render_mode", mode);
  return (err == ASCIICHAT_OK) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int mirror_get_render_mode(void) {
  return GET_OPTION(render_mode);
}

// ============================================================================
// Settings API - Color Mode
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int mirror_set_color_mode(int mode) {
  if (mode < TERM_COLOR_AUTO || mode > TERM_COLOR_TRUECOLOR) {
    return -1;
  }
  asciichat_error_t err = options_set_int("color_mode", mode);
  return (err == ASCIICHAT_OK) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int mirror_get_color_mode(void) {
  return GET_OPTION(color_mode);
}

// ============================================================================
// Settings API - Palette
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int mirror_set_palette(const char *palette_name) {
  if (!palette_name) {
    return -1;
  }

  // Parse the palette string using the same parser as CLI
  palette_type_t palette_value;
  char *error_msg = NULL;

  if (!parse_palette_type(palette_name, &palette_value, &error_msg)) {
    if (error_msg) {
      log_error("Failed to parse palette '%s': %s", palette_name, error_msg);
      free(error_msg);
    }
    return -1;
  }

  // Use standard RCU setter for palette_type field
  asciichat_error_t err = options_set_int("palette_type", (int)palette_value);
  return (err == ASCIICHAT_OK) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int mirror_get_palette(void) {
  return GET_OPTION(palette_type);
}

// ============================================================================
// Settings API - Color Filter
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int mirror_set_color_filter(int filter) {
  if (filter < COLOR_FILTER_NONE || filter >= COLOR_FILTER_COUNT) {
    return -1;
  }
  asciichat_error_t err = options_set_int("color_filter", filter);
  return (err == ASCIICHAT_OK) ? 0 : -1;
}

EMSCRIPTEN_KEEPALIVE
int mirror_get_color_filter(void) {
  return GET_OPTION(color_filter);
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

  // Debug: log what we're actually using
  log_info("WASM: src=%dx%d, dst=%dx%d, color_mode=%d, filter=%d", src_width, src_height, dst_width, dst_height,
           color_mode, filter);

  // Convert RGBA to RGB (strip alpha channel)
  rgb_pixel_t *rgb_pixels = SAFE_MALLOC(src_width * src_height * sizeof(rgb_pixel_t), rgb_pixel_t *);
  if (!rgb_pixels) {
    return NULL;
  }

  for (int i = 0; i < src_width * src_height; i++) {
    rgb_pixels[i].r = rgba_data[i * 4 + 0];
    rgb_pixels[i].g = rgba_data[i * 4 + 1];
    rgb_pixels[i].b = rgba_data[i * 4 + 2];
    // Alpha (rgba_data[i * 4 + 3]) is discarded
  }

  // Apply color filter if needed
  if (filter != COLOR_FILTER_NONE) {
    // color_filter operates on packed RGB24 format
    uint8_t *rgb24 = (uint8_t *)rgb_pixels;
    int stride = src_width * 3;
    apply_color_filter(rgb24, src_width, src_height, stride, filter);
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

  return ascii_output;
}

EMSCRIPTEN_KEEPALIVE
void mirror_free_string(char *ptr) {
  SAFE_FREE(ptr);
}
