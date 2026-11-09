#include "common/font.h"
#include "mirror.h"
/**
 * @file mirror_wasm.c
 * @brief WASM entry point for ascii-chat mirror mode
 */

#include <emscripten.h>
#include <stdlib.h>
#include <string.h>

#include "session/frame_validation.h"
#include <ascii-chat/util/utf8.h>

#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/video/ascii/ascii.h>
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/rgba/color_filter.h>
#include <ascii-chat/video/rgba/image.h>
#include <ascii-chat/video/ascii/palette.h>
#include <ascii-chat/video/terminal/ansi.h>
#include <ascii-chat/video/anim/digital_rain.h>
#include <ascii-chat/util/aspect_ratio.h>
#include <ascii-chat/common.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/audio/visualization.h>
#include <ascii-chat/video/anim/waveform.h>
#include <ascii-chat/video/anim/fft.h>

// Global digital rain effect context
mirror_render_context_t *mirror_render_context_create(void) {
  return SAFE_CALLOC(1, sizeof(mirror_render_context_t), mirror_render_context_t *);
}
void mirror_render_context_reset(mirror_render_context_t *context) {
  if (!context) return;
  ascii_stabilizer_reset(&context->stabilizer);
  if (context->digital_rain) {
    digital_rain_destroy(context->digital_rain);
    context->digital_rain = NULL;
  }
  context->last_rain_update_time = 0.0;
  context->frame_count = 0;
}
void mirror_render_context_destroy(mirror_render_context_t *context) {
  if (!context) return;
  mirror_render_context_reset(context);
  SAFE_FREE(context);
}

// ============================================================================
// Initialization
// ============================================================================

/**
 * Initialize mirror mode with native defaults.
 * @return 0 on success, -1 on error
 */
int mirror_init(void) {
  // Initialize platform layer
  asciichat_error_t err = platform_init();
  if (err != ASCIICHAT_OK) {
    return -1;
  }

  char *argv[] = {"mirror", NULL};
  err = options_init(1, argv);

  if (err != ASCIICHAT_OK) {
    return -1;
  }

  // Honor parsed verbosity and let INFO use stdout (console.log).
  log_init(NULL, GET_OPTION(log_level), false, false);
  // Use debug-style format in WASM so browser console shows full log headers.
  log_set_format(OPT_LOG_TEMPLATE_DEFAULT_DEBUG, false);

  // Log option values after initialization
  log_info("Mirror options initialized");

  // Initialize ANSI color code generation (dec3 cache for RGB values)
  ansi_fast_init();

  return 0;
}

void mirror_cleanup(void) {
  errno_destroy();
  options_state_destroy();
  buffer_system_cleanup();
  platform_destroy();
}

// ============================================================================
// Frame Conversion API
// ============================================================================

char *mirror_convert_frame(mirror_render_context_t *context, uint8_t *rgba_data, int src_width, int src_height, int contain) {
  if (!context) {
    SET_ERRNO(ERROR_INVALID_STATE, "Mirror render context is closed");
    return NULL;
  }
  if (!rgba_data || src_width <= 0 || src_height <= 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid browser frame pixels or dimensions");
    return NULL;
  }

  context->frame_count++;
  // Derive a time value from frame count using actual target FPS from options
  int target_fps = (int)GET_OPTION(fps);
  if (target_fps <= 0) {
    target_fps = 60; // Default to 60 FPS if not set
  }
  float time_seconds = (float)context->frame_count / (float)target_fps;

  // Get current settings from options or terminal detection
  int dst_width = (int)terminal_get_effective_width();
  int dst_height = (int)terminal_get_effective_height();
  color_filter_t filter = (color_filter_t)GET_OPTION(color_filter);
  terminal_color_mode_t color_mode = (terminal_color_mode_t)GET_OPTION(color_mode);
  if (context->frame_count <= 3 || context->frame_count % 120 == 0) {
    log_debug("frame=%u color_filter=%d color_mode=%d", context->frame_count, (int)filter, (int)color_mode);
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
  caps.text_renderer = GET_OPTION(renderer);
  caps.stabilize = GET_OPTION(stabilize);
  caps.palette_explicit = GET_OPTION(palette_explicit);
  caps.wants_background = (caps.render_mode == RENDER_MODE_BACKGROUND);
  caps.wants_padding = true;
  caps.palette_type = palette_type;
  caps.desired_fps = 60;
  caps.color_filter = filter;

  // Convert RGBA to RGB (strip alpha channel) and optionally flip horizontally
  rgb_pixel_t *rgb_pixels = SAFE_MALLOC(src_width * src_height * sizeof(rgb_pixel_t), rgb_pixel_t *);
  if (!rgb_pixels) {
    SET_ERRNO(ERROR_MEMORY, "Cannot allocate browser frame pixels");
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
  image_t img = {.w = src_width, .h = src_height, .pixels = rgb_pixels};

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
  char *ascii_output = ascii_convert_with_buffers_stabilized(&img, dst_width, dst_height, &caps, preserve_aspect_ratio,
                                                             stretch, palette_chars, contain != 0, NULL, NULL,
                                                             caps.stabilize ? &context->stabilizer : NULL);
  if (!caps.stabilize)
    ascii_stabilizer_reset(&context->stabilizer);

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
    if (!context->digital_rain || context->digital_rain->num_columns != dst_width || context->digital_rain->num_rows != dst_height) {
      if (context->digital_rain) {
        digital_rain_destroy(context->digital_rain);
      }
      context->digital_rain = digital_rain_init(dst_width, dst_height);
      if (!context->digital_rain) {
        log_error("Failed to initialize digital rain effect");
        goto validate_output;
      }
      context->last_rain_update_time = emscripten_get_now();
    }

    // Update color from active filter (allows filter changes after initialization)
    digital_rain_set_color_from_filter(context->digital_rain, filter);

    // Always apply rain to the newest input. Caching an entire ANSI frame freezes
    // both the animation and webcam while the browser keeps counting presentations.
    double current_time = emscripten_get_now();
    float delta_time = (float)((current_time - context->last_rain_update_time) / 1000.0);
    context->last_rain_update_time = current_time;
    char *rain_output = digital_rain_apply(context->digital_rain, ascii_output, delta_time);
    if (rain_output) {
      SAFE_FREE(ascii_output);
      ascii_output = rain_output;
    }
  } else {
    // Clean up digital rain context if it exists but effect is disabled
    if (context->digital_rain) {
      digital_rain_destroy(context->digital_rain);
      context->digital_rain = NULL;
    }
    context->last_rain_update_time = 0.0;
  }

validate_output:
  if (ascii_output) {
    bool glyph_match = caps.text_renderer == ASCII_RENDERER_GLYPH && caps.render_mode != RENDER_MODE_HALF_BLOCK &&
                       dst_width > 0 && dst_height > 0 && dst_width <= IMAGE_MAX_WIDTH / 8 &&
                       dst_height <= IMAGE_MAX_HEIGHT / 8 &&
                       (!caps.palette_explicit || ascii_glyph_palette_supported(palette_chars));
    if (!glyph_match && (size_t)utf8_display_width(palette_chars) != utf8_char_count(palette_chars)) {
      ascii_frame_source_t source = {.frame_data = ascii_output, .frame_size = strlen(ascii_output)};
      size_t normalized_size;
      char *rectangular = ascii_create_grid(&source, 1, dst_width, dst_height, &normalized_size);
      SAFE_FREE(ascii_output);
      ascii_output = rectangular;
    }
    if (!ascii_output)
      return NULL;
    acip_render_settings_t rendering = {.caps = caps};
    const char *contract_palette =
        glyph_match && !caps.palette_explicit
            ? " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"
            : palette_chars;
    SAFE_STRNCPY(rendering.palette, contract_palette, sizeof(rendering.palette));
    uint32_t effects = matrix_rain ? FRAME_RENDER_CONTRACT_RAIN : 0;
    if (session_frame_validate_contract(ascii_output, strlen(ascii_output), dst_width, dst_height, &rendering,
                                        effects) != ASCIICHAT_OK) {
      SAFE_FREE(ascii_output);
      return NULL;
    }
  }
  return ascii_output;
}

void mirror_submit_audio_samples(mirror_render_context_t *context, const float *samples, int count, int source) {
  if (!context) return;
  if (!samples || count <= 0 || count > 8192)
    return;
  audio_visualization_submit(source == AUDIO_VISUALIZATION_SOURCE_MIC ? AUDIO_VISUALIZATION_SOURCE_MIC
                                                                      : AUDIO_VISUALIZATION_SOURCE_MEDIA,
                             samples, (size_t)count);
}

char *mirror_render_audio_visualization(mirror_render_context_t *context, unsigned int width, unsigned int height, int source) {
  if (!context) return NULL;
  audio_visualization_source_t audio_source =
      source == AUDIO_VISUALIZATION_SOURCE_MIC ? AUDIO_VISUALIZATION_SOURCE_MIC : AUDIO_VISUALIZATION_SOURCE_MEDIA;
  char *text =
      GET_OPTION(fft)
          ? audio_visualization_render_fft(width, height, audio_source, true, AUDIO_VISUALIZATION_COLOR_BRIGHTER,
                                           GET_OPTION(flip_x), GET_OPTION(flip_y))
          : audio_visualization_render_waveform(width, height, audio_source, true, AUDIO_VISUALIZATION_COLOR_BRIGHTER,
                                                GET_OPTION(flip_x), GET_OPTION(flip_y));
  if (text && session_frame_validate(text, strlen(text), width, height) != ASCIICHAT_OK) {
    SAFE_FREE(text);
    return NULL;
  }
  return text;
}

void mirror_free_string(char *ptr) {
  SAFE_FREE(ptr);
}
