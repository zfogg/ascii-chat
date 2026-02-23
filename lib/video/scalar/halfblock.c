/**
 * @file video/scalar/halfblock.c
 * @ingroup video
 * @brief 🎨 Scalar truecolor halfblock renderer
 *
 * Scalar (non-SIMD) implementation of truecolor halfblock rendering.
 * Processes 2 rows of source pixels per 1 output line using upper half-block
 * character with foreground color from top row and background color from bottom row.
 *
 * Uses run-length encoding (RLE) for efficient output compression and tracks
 * color state to minimize ANSI escape sequence emission.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/common.h>
#include <ascii-chat/video/output_buffer.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/video/ansi_fast.h>

/* ============================================================================
 * Scalar Halfblock Rendering
 * ============================================================================
 */

/**
 * @brief Scalar truecolor halfblock renderer
 * @param rgb Input RGB image data (RGB24, 3 bytes per pixel)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param stride_bytes Bytes per row (if 0, calculated as width * 3)
 * @return Malloc'd NUL-terminated ANSI escape string (caller must free)
 *
 * Renders an RGB image using halfblock characters with truecolor foreground
 * and background colors. Processes 2 source rows per output line:
 * - Top pixel: foreground color for halfblock
 * - Bottom pixel: background color for halfblock
 * - Last row (odd height): duplicates top row as bottom
 *
 * Uses RLE optimization for repeated halfblocks and detects transparent areas
 * (fully black pixels) which are rendered as spaces instead of halfblocks.
 */
char *rgb_to_truecolor_halfblocks_scalar(const uint8_t *rgb, int width, int height, int stride_bytes) {
  if (width <= 0 || height <= 0)
    return platform_strdup("");
  if (stride_bytes <= 0)
    stride_bytes = width * 3;

  outbuf_t ob = {0};
  // Estimate: per cell ~ 10-14 bytes (ANSI sequences); half the rows + newlines
  size_t est_cells = (size_t)width * ((size_t)(height + 1) / 2);
  ob.cap = est_cells * 14u + (size_t)((height + 1) / 2) * 8u + 64u;
  ob.buf = SAFE_MALLOC(ob.cap ? ob.cap : 1, char *);
  if (!ob.buf)
    return NULL;

  // Track SGR state (use -1 to indicate "not set")
  int cur_fr = -1, cur_fg = -1, cur_fb = -1;
  int cur_br = -1, cur_bg = -1, cur_bb = -1;

  // Upper half-block character: U+2580 = UTF-8 0xE2 0x96 0x80
  static const char HB[3] = {(char)0xE2, (char)0x96, (char)0x80};

  // Process 2 source rows per output line
  for (int y = 0; y < height; y += 2) {
    const uint8_t *rowT = rgb + (size_t)y * (size_t)stride_bytes;
    const uint8_t *rowB = (y + 1 < height) ? rowT + (size_t)stride_bytes : NULL;

    int x = 0;
    while (x < width) {
      // Read top pixel
      const uint8_t *pT = rowT + (size_t)x * 3u;
      uint8_t rT = pT[0], gT = pT[1], bT = pT[2];

      // Read bottom pixel (or duplicate top if no bottom row)
      uint8_t rB = rT, gB = gT, bB = bT;
      if (rowB) {
        const uint8_t *pB = rowB + (size_t)x * 3u;
        rB = pB[0];
        gB = pB[1];
        bB = pB[2];
      }

      // Extend run while top and bottom colors match exactly
      int j = x + 1;
      for (; j < width; ++j) {
        const uint8_t *qT = rowT + (size_t)j * 3u;
        uint8_t rT2 = qT[0], gT2 = qT[1], bT2 = qT[2];

        uint8_t rB2 = rT2, gB2 = gT2, bB2 = bT2;
        if (rowB) {
          const uint8_t *qB = rowB + (size_t)j * 3u;
          rB2 = qB[0];
          gB2 = qB[1];
          bB2 = qB[2];
        }

        // Check if colors match
        if (!((rT2 == rT && gT2 == gT && bT2 == bT) && (rB2 == rB && gB2 == gB && bB2 == bB)))
          break;
      }
      uint32_t run = (uint32_t)(j - x);

      // Check if transparent (all black = padding)
      bool is_transparent = (rT == 0 && gT == 0 && bT == 0 && rB == 0 && gB == 0 && bB == 0);

      if (is_transparent) {
        // Reset colors before transparent areas to prevent color bleeding
        if (cur_fr != -1 || cur_fg != -1 || cur_fb != -1 || cur_br != -1 || cur_bg != -1 || cur_bb != -1) {
          emit_reset(&ob);
          cur_fr = cur_fg = cur_fb = -1;
          cur_br = cur_bg = cur_bb = -1;
        }
        // Emit spaces for transparent area
        ob_write(&ob, " ", 1);
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; ++k) {
            ob_write(&ob, " ", 1);
          }
        }
      } else {
        // Normal colored halfblocks - set fg to TOP, bg to BOTTOM if changed
        if (cur_fr != rT || cur_fg != gT || cur_fb != bT) {
          emit_set_fg(&ob, rT, gT, bT);
          cur_fr = rT;
          cur_fg = gT;
          cur_fb = bT;
        }
        if (cur_br != rB || cur_bg != gB || cur_bb != bB) {
          emit_set_bg(&ob, rB, gB, bB);
          cur_br = rB;
          cur_bg = gB;
          cur_bb = bB;
        }

        // Emit halfblock once, then RLE or literals
        ob_write(&ob, HB, 3);
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; ++k) {
            ob_write(&ob, HB, 3);
          }
        }
      }

      x = j;
    }

    // End of line: reset and newline (except for last output line)
    emit_reset(&ob);
    if (y + 2 < height) {
      ob_putc(&ob, '\n');
    }
    cur_fr = cur_fg = cur_fb = -1;
    cur_br = cur_bg = cur_bb = -1;
  }

  // Ensure null terminator
  ob_term(&ob);
  return ob.buf;
}

/* ============================================================================
 * Monochrome Halfblock Rendering
 * ============================================================================
 */

/**
 * @brief Scalar monochrome halfblock renderer (no color codes)
 * @param rgb Input RGB image data (RGB24, 3 bytes per pixel)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param stride_bytes Bytes per row (if 0, calculated as width * 3)
 * @param palette Character palette (unused, for API consistency)
 * @return Malloc'd NUL-terminated ASCII string (caller must free)
 *
 * Renders an RGB image using halfblock characters with no color codes.
 * Simple monochrome rendering without any ANSI escape sequences.
 */
char *rgb_to_halfblocks_scalar(const uint8_t *rgb, int width, int height, int stride_bytes, const char *palette) {
  (void)palette; // Unused for monochrome mode
  if (width <= 0 || height <= 0)
    return platform_strdup("");
  if (stride_bytes <= 0)
    stride_bytes = width * 3;

  outbuf_t ob = {0};
  // Estimate: per cell ~ 3 bytes (UTF-8 halfblock); half the rows + newlines
  size_t est_cells = (size_t)width * ((size_t)(height + 1) / 2);
  ob.cap = est_cells * 3u + (size_t)((height + 1) / 2) * 2u + 64u;
  ob.buf = SAFE_MALLOC(ob.cap ? ob.cap : 1, char *);
  if (!ob.buf)
    return NULL;

  // Upper half-block character: U+2580 = UTF-8 0xE2 0x96 0x80
  static const char HB[3] = {(char)0xE2, (char)0x96, (char)0x80};

  // Process 2 source rows per output line
  for (int y = 0; y < height; y += 2) {
    const uint8_t *rowT = rgb + (size_t)y * (size_t)stride_bytes;
    const uint8_t *rowB = (y + 1 < height) ? rowT + (size_t)stride_bytes : NULL;

    int x = 0;
    while (x < width) {
      // Read top pixel
      const uint8_t *pT = rowT + (size_t)x * 3u;
      uint8_t rT = pT[0], gT = pT[1], bT = pT[2];

      // Read bottom pixel (or duplicate top if no bottom row)
      uint8_t rB = rT, gB = gT, bB = bT;
      if (rowB) {
        const uint8_t *pB = rowB + (size_t)x * 3u;
        rB = pB[0];
        gB = pB[1];
        bB = pB[2];
      }

      // Extend run while colors match exactly (treating as monochrome)
      int j = x + 1;
      for (; j < width; ++j) {
        const uint8_t *qT = rowT + (size_t)j * 3u;
        uint8_t rT2 = qT[0], gT2 = qT[1], bT2 = qT[2];

        uint8_t rB2 = rT2, gB2 = gT2, bB2 = bT2;
        if (rowB) {
          const uint8_t *qB = rowB + (size_t)j * 3u;
          rB2 = qB[0];
          gB2 = qB[1];
          bB2 = qB[2];
        }

        if (!((rT2 == rT && gT2 == gT && bT2 == bT) && (rB2 == rB && gB2 == gB && bB2 == bB)))
          break;
      }
      uint32_t run = (uint32_t)(j - x);

      // Check if transparent (all black = padding)
      bool is_transparent = (rT == 0 && gT == 0 && bT == 0 && rB == 0 && gB == 0 && bB == 0);

      if (is_transparent) {
        // Emit spaces for transparent area
        ob_write(&ob, " ", 1);
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; ++k) {
            ob_write(&ob, " ", 1);
          }
        }
      } else {
        // Emit halfblock with no color codes
        ob_write(&ob, HB, 3);
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; ++k) {
            ob_write(&ob, HB, 3);
          }
        }
      }

      x = j;
    }

    // End of line: newline (except for last output line)
    if (y + 2 < height) {
      ob_putc(&ob, '\n');
    }
  }

  ob_term(&ob);
  return ob.buf;
}

/* ============================================================================
 * 16-Color Halfblock Rendering
 * ============================================================================
 */

/**
 * @brief Scalar 16-color halfblock renderer
 * Uses 16-color ANSI codes for foreground (top) and implicit background (bottom).
 */
char *rgb_to_16color_halfblocks_scalar(const uint8_t *rgb, int width, int height, int stride_bytes, const char *palette) {
  (void)palette; // Unused for 16-color mode
  if (width <= 0 || height <= 0)
    return platform_strdup("");
  if (stride_bytes <= 0)
    stride_bytes = width * 3;

  // Initialize 16-color lookup table
  ansi_fast_init_16color();

  outbuf_t ob = {0};
  // Estimate: per cell ~ 12 bytes (16-color codes); half the rows + newlines + resets
  size_t est_cells = (size_t)width * ((size_t)(height + 1) / 2);
  ob.cap = est_cells * 12u + (size_t)((height + 1) / 2) * 8u + 64u;
  ob.buf = SAFE_MALLOC(ob.cap ? ob.cap : 1, char *);
  if (!ob.buf)
    return NULL;

  // Track 16-color state
  int cur_fg = -1, cur_bg = -1;
  static const char HB[3] = {(char)0xE2, (char)0x96, (char)0x80};

  for (int y = 0; y < height; y += 2) {
    const uint8_t *rowT = rgb + (size_t)y * (size_t)stride_bytes;
    const uint8_t *rowB = (y + 1 < height) ? rowT + (size_t)stride_bytes : NULL;

    int x = 0;
    while (x < width) {
      const uint8_t *pT = rowT + (size_t)x * 3u;
      uint8_t rT = pT[0], gT = pT[1], bT = pT[2];
      uint8_t color_fg = rgb_to_16color(rT, gT, bT);

      uint8_t rB = rT, gB = gT, bB = bT;
      if (rowB) {
        const uint8_t *pB = rowB + (size_t)x * 3u;
        rB = pB[0];
        gB = pB[1];
        bB = pB[2];
      }
      uint8_t color_bg = rgb_to_16color(rB, gB, bB);

      // Extend run while colors match
      int j = x + 1;
      for (; j < width; ++j) {
        const uint8_t *qT = rowT + (size_t)j * 3u;
        uint8_t color_fg2 = rgb_to_16color(qT[0], qT[1], qT[2]);

        uint8_t color_bg2 = color_fg2;
        if (rowB) {
          const uint8_t *qB = rowB + (size_t)j * 3u;
          color_bg2 = rgb_to_16color(qB[0], qB[1], qB[2]);
        }

        if (color_fg2 != color_fg || color_bg2 != color_bg)
          break;
      }
      uint32_t run = (uint32_t)(j - x);

      // Check if transparent
      bool is_transparent = (rT == 0 && gT == 0 && bT == 0 && rB == 0 && gB == 0 && bB == 0);

      if (is_transparent) {
        if (cur_fg != -1 || cur_bg != -1) {
          emit_reset(&ob);
          cur_fg = cur_bg = -1;
        }
        ob_write(&ob, " ", 1);
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; ++k) {
            ob_write(&ob, " ", 1);
          }
        }
      } else {
        // Emit 16-color codes if changed
        if (cur_fg != color_fg) {
          char buf[16];
          char *ptr = append_16color_fg(buf, color_fg);
          ob_write(&ob, buf, (size_t)(ptr - buf));
          cur_fg = color_fg;
        }
        if (cur_bg != color_bg) {
          char buf[16];
          char *ptr = append_16color_bg(buf, color_bg);
          ob_write(&ob, buf, (size_t)(ptr - buf));
          cur_bg = color_bg;
        }

        ob_write(&ob, HB, 3);
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; ++k) {
            ob_write(&ob, HB, 3);
          }
        }
      }

      x = j;
    }

    emit_reset(&ob);
    if (y + 2 < height) {
      ob_putc(&ob, '\n');
    }
    cur_fg = cur_bg = -1;
  }

  ob_term(&ob);
  return ob.buf;
}

/* ============================================================================
 * 256-Color Halfblock Rendering
 * ============================================================================
 */

/**
 * @brief Scalar 256-color halfblock renderer
 * Uses 256-color ANSI codes for foreground (top) and implicit background (bottom).
 */
char *rgb_to_256color_halfblocks_scalar(const uint8_t *rgb, int width, int height, int stride_bytes, const char *palette) {
  (void)palette; // Unused for 256-color mode
  if (width <= 0 || height <= 0)
    return platform_strdup("");
  if (stride_bytes <= 0)
    stride_bytes = width * 3;

  // Initialize 256-color lookup table
  ansi_fast_init_256color();

  outbuf_t ob = {0};
  // Estimate: per cell ~ 14 bytes (256-color codes); half the rows + newlines + resets
  size_t est_cells = (size_t)width * ((size_t)(height + 1) / 2);
  ob.cap = est_cells * 14u + (size_t)((height + 1) / 2) * 8u + 64u;
  ob.buf = SAFE_MALLOC(ob.cap ? ob.cap : 1, char *);
  if (!ob.buf)
    return NULL;

  // Track 256-color state
  int cur_fg = -1, cur_bg = -1;
  static const char HB[3] = {(char)0xE2, (char)0x96, (char)0x80};

  for (int y = 0; y < height; y += 2) {
    const uint8_t *rowT = rgb + (size_t)y * (size_t)stride_bytes;
    const uint8_t *rowB = (y + 1 < height) ? rowT + (size_t)stride_bytes : NULL;

    int x = 0;
    while (x < width) {
      const uint8_t *pT = rowT + (size_t)x * 3u;
      uint8_t rT = pT[0], gT = pT[1], bT = pT[2];
      uint8_t color_fg = rgb_to_256color(rT, gT, bT);

      uint8_t rB = rT, gB = gT, bB = bT;
      if (rowB) {
        const uint8_t *pB = rowB + (size_t)x * 3u;
        rB = pB[0];
        gB = pB[1];
        bB = pB[2];
      }
      uint8_t color_bg = rgb_to_256color(rB, gB, bB);

      // Extend run while colors match
      int j = x + 1;
      for (; j < width; ++j) {
        const uint8_t *qT = rowT + (size_t)j * 3u;
        uint8_t color_fg2 = rgb_to_256color(qT[0], qT[1], qT[2]);

        uint8_t color_bg2 = color_fg2;
        if (rowB) {
          const uint8_t *qB = rowB + (size_t)j * 3u;
          color_bg2 = rgb_to_256color(qB[0], qB[1], qB[2]);
        }

        if (color_fg2 != color_fg || color_bg2 != color_bg)
          break;
      }
      uint32_t run = (uint32_t)(j - x);

      // Check if transparent
      bool is_transparent = (rT == 0 && gT == 0 && bT == 0 && rB == 0 && gB == 0 && bB == 0);

      if (is_transparent) {
        if (cur_fg != -1 || cur_bg != -1) {
          emit_reset(&ob);
          cur_fg = cur_bg = -1;
        }
        ob_write(&ob, " ", 1);
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; ++k) {
            ob_write(&ob, " ", 1);
          }
        }
      } else {
        // Emit 256-color foreground code if changed (use top pixel color)
        if (cur_fg != color_fg) {
          char buf[16];
          char *ptr = append_256color_fg(buf, color_fg);
          ob_write(&ob, buf, (size_t)(ptr - buf));
          cur_fg = color_fg;
        }

        ob_write(&ob, HB, 3);
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; ++k) {
            ob_write(&ob, HB, 3);
          }
        }
      }

      x = j;
    }

    emit_reset(&ob);
    if (y + 2 < height) {
      ob_putc(&ob, '\n');
    }
    cur_fg = -1;
  }

  ob_term(&ob);
  return ob.buf;
}
