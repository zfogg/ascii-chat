/**
 * @file mirror_wasm.c
 * @brief WASM entry point for ascii-chat mirror mode
 */

#include <emscripten.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/video/ascii.h>
#include <ascii-chat/video/color_filter.h>
#include <ascii-chat/video/framebuffer.h>
#include <ascii-chat/common.h>

// ============================================================================
// Initialization
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int mirror_init(int width, int height) {
  // Initialize platform layer
  asciichat_error_t err = platform_init();
  if (err != ASCIICHAT_OK) {
    return -1;
  }

  // Create minimal argc/argv for options system
  char *argv[] = {"mirror", NULL};
  int argc = 1;

  // Initialize options (sets up RCU, defaults, etc.)
  err = options_init(argc, argv);
  if (err != ASCIICHAT_OK) {
    return -1;
  }

  // Override dimensions with actual values from xterm.js
  options_set_int("width", width);
  options_set_int("height", height);

  return 0;
}

EMSCRIPTEN_KEEPALIVE
void mirror_cleanup(void) {
  options_cleanup();
  platform_cleanup();
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
  // mode: 0=foreground, 1=background, 2=half-block
  if (mode < 0 || mode > 2) {
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
  // mode: 0=auto, 1=none, 2=16, 3=256, 4=truecolor
  if (mode < 0 || mode > 4) {
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
// Settings API - Color Filter
// ============================================================================

EMSCRIPTEN_KEEPALIVE
int mirror_set_color_filter(int filter) {
  // filter: 0=none, 1=black, 2=white, 3=green, 4=magenta, etc.
  if (filter < 0 || filter > 11) {
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
  render_mode_t render_mode = (render_mode_t)GET_OPTION(render_mode);

  // Apply color filter to RGBA data if needed
  if (filter != COLOR_FILTER_NONE) {
    // Convert RGBA to RGB (color_filter expects RGB24)
    int stride = src_width * 3;
    uint8_t *rgb_data = SAFE_MALLOC(src_width * src_height * 3, uint8_t *);
    if (!rgb_data) {
      return NULL;
    }

    // Extract RGB from RGBA
    for (int i = 0; i < src_width * src_height; i++) {
      rgb_data[i * 3 + 0] = rgba_data[i * 4 + 0]; // R
      rgb_data[i * 3 + 1] = rgba_data[i * 4 + 1]; // G
      rgb_data[i * 3 + 2] = rgba_data[i * 4 + 2]; // B
    }

    // Apply filter (modifies rgb_data in-place)
    apply_color_filter(rgb_data, src_width, src_height, stride, filter);

    // Copy filtered RGB back to RGBA
    for (int i = 0; i < src_width * src_height; i++) {
      rgba_data[i * 4 + 0] = rgb_data[i * 3 + 0];
      rgba_data[i * 4 + 1] = rgb_data[i * 3 + 1];
      rgba_data[i * 4 + 2] = rgb_data[i * 3 + 2];
      // Alpha unchanged
    }

    SAFE_FREE(rgb_data);
  }

  // Create framebuffer for ASCII output
  framebuffer_t fb;
  if (framebuffer_init(&fb, dst_width, dst_height) != ASCIICHAT_OK) {
    return NULL;
  }

  // Convert to ASCII using full library rendering
  asciichat_error_t err = convert_image_to_ascii(rgba_data, src_width, src_height, &fb, render_mode);

  if (err != ASCIICHAT_OK) {
    framebuffer_cleanup(&fb);
    return NULL;
  }

  // Generate ANSI escape sequences from framebuffer
  char *output = generate_ansi_output(&fb, render_mode);

  framebuffer_cleanup(&fb);
  return output;
}

EMSCRIPTEN_KEEPALIVE
void mirror_free_string(char *ptr) {
  SAFE_FREE(ptr);
}
