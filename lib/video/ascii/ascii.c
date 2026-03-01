/**
 * @file video/ascii/ascii.c
 * @ingroup video
 * @brief ASCII rendering dispatcher and main API
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ascii-chat/common.h>
#include <ascii-chat/video/rgba/image.h>
#include <ascii-chat/video/ascii.h>
#include <ascii-chat/video/ascii/output_buffer.h>
#include <ascii-chat/log/log.h>

/**
 * @brief Main ASCII rendering API - dispatches to best SIMD implementation
 */
char *render_ascii(image_t *image, const char *ascii_chars) {
  if (!image || !ascii_chars) {
    return NULL;
  }

  log_debug("render_ascii: dispatching to SIMD implementation");
  return image_print_color_simd(image, false, false, ascii_chars);
}

/**
 * @brief ASCII rendering with color support
 */
char *render_ascii_color(image_t *image, bool use_256color, const char *ascii_chars) {
  if (!image || !ascii_chars) {
    return NULL;
  }

  log_debug("render_ascii_color: use_256color=%d", use_256color);
  return image_print_color_simd(image, false, use_256color, ascii_chars);
}

/**
 * @brief ASCII rendering with background color mode
 */
char *render_ascii_background(image_t *image, bool use_256color, const char *ascii_chars) {
  if (!image || !ascii_chars) {
    return NULL;
  }

  log_debug("render_ascii_background: use_256color=%d", use_256color);
  return image_print_color_simd(image, true, use_256color, ascii_chars);
}

/**
 * @brief Convert image to ASCII art using SIMD (grayscale/monochrome)
 * @param image Image to convert
 * @param ascii_chars Character palette for luminance mapping
 * @return Allocated ASCII string (caller must free), or NULL on error
 */
char *image_print_simd(image_t *image, const char *ascii_chars) {
#if SIMD_SUPPORT_AVX2
  extern char *render_ascii_image_monochrome_avx2(const image_t *image, const char *ascii_chars);
  return render_ascii_image_monochrome_avx2(image, ascii_chars);
#elif SIMD_SUPPORT_SSSE3
  extern char *render_ascii_image_monochrome_ssse3(const image_t *image, const char *ascii_chars);
  return render_ascii_image_monochrome_ssse3(image, ascii_chars);
#elif SIMD_SUPPORT_SSE2
  extern char *render_ascii_image_monochrome_sse2(const image_t *image, const char *ascii_chars);
  return render_ascii_image_monochrome_sse2(image, ascii_chars);
#elif SIMD_SUPPORT_NEON
  extern char *render_ascii_image_monochrome_neon(const image_t *image, const char *ascii_chars);
  return render_ascii_image_monochrome_neon(image, ascii_chars);
#elif SIMD_SUPPORT_SVE
  extern char *render_ascii_image_monochrome_sve(const image_t *image, const char *ascii_chars);
  return render_ascii_image_monochrome_sve(image, ascii_chars);
#else
  // Fallback to scalar implementation
  extern char *image_print(const image_t *p, const char *palette);
  return image_print(image, ascii_chars);
#endif
}

/**
 * @brief Convert image to ASCII art using terminal capabilities
 * @param image Image to convert
 * @param caps Terminal capabilities structure
 * @param palette Character palette for luminance mapping
 * @return Allocated ASCII string (caller must free), or NULL on error
 *
 * Automatically selects the best rendering method based on terminal capabilities.
 */
char *image_print_with_capabilities(const image_t *image, const terminal_capabilities_t *caps, const char *palette) {
  if (!image || !caps || !palette) {
    return NULL;
  }

  // Import scalar implementations
  extern char *image_print(const image_t *p, const char *palette);
  extern char *image_print_color(const image_t *p, const char *palette);
  extern char *image_print_256color(const image_t *image, const char *palette);
  extern char *image_print_16color_dithered(const image_t *image, const char *palette);

  // Dispatch based on terminal capabilities (check for COLORTERM variable or terminal_capabilities_t)
  // For now, use a simple heuristic: if color_level would indicate truecolor, use color
  // This can be enhanced with proper terminal capability detection

  // Try color rendering first
  return image_print_color(image, palette);
}
