/**
 * @file video/ascii/neon/halfblock.c
 * @ingroup video
 * @brief ARM NEON-accelerated ASCII half-block rendering
 *
 * Gives double vertical resolution in terminal rendering.
 * Requires UTF-8 support from the terminal.
 */

#if SIMD_SUPPORT_NEON
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <assert.h>
#include <ascii-chat/atomic.h>
#include <math.h>

#include <arm_neon.h>

#include <ascii-chat/common.h>
#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/video/ascii/neon.h>
#include <ascii-chat/video/rgba/image.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/ascii/output_buffer.h>
#include <ascii-chat/video/ascii/ansi_fast.h>
#include <ascii-chat/util/overflow.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/log/log.h>

//=============================================================================
// Optimized NEON Half-block renderer (based on ChatGPT reference)
//=============================================================================
char *rgb_to_truecolor_halfblocks_neon(const uint8_t *rgb, int width, int height, int stride_bytes) {
  /* Main: half-block renderer. Returns NUL-terminated malloc'd string; caller free(). */
  if (width <= 0 || height <= 0)
    return platform_strdup("");
  if (stride_bytes <= 0)
    stride_bytes = width * 3;

  outbuf_t ob = {0};
  // generous guess: per cell ~ 10–14 bytes avg; half the rows + newlines
  size_t est_cells = (size_t)width * ((size_t)(height + 1) / 2);
  ob.cap = est_cells * 14u + (size_t)((height + 1) / 2) * 8u + 64u;
  ob.buf = SAFE_MALLOC(ob.cap ? ob.cap : 1, char *);
  if (!ob.buf)
    return NULL;

  // current SGR state; -1 means unknown
  int cur_fr = -1, cur_fg = -1, cur_fb = -1;
  int cur_br = -1, cur_bg = -1, cur_bb = -1;

  // process two source rows per emitted line
  for (int y = 0; y < height; y += 2) {
    const uint8_t *rowT = rgb + (size_t)y * (size_t)stride_bytes;
    const uint8_t *rowB = (y + 1 < height) ? rowT + (size_t)stride_bytes : NULL;

    int x = 0;
    while (x + 16 <= width) {
      // Load 16 top and bottom pixels (RGB interleaved)
      const uint8_t *pT = rowT + (size_t)x * 3u;
      uint8x16x3_t top = vld3q_u8(pT);

      uint8x16x3_t bot;
      if (rowB) {
        const uint8_t *pB = rowB + (size_t)x * 3u;
        bot = vld3q_u8(pB);
      } else {
        // synthesize bottom = top for odd-height last row
        bot.val[0] = top.val[0];
        bot.val[1] = top.val[1];
        bot.val[2] = top.val[2];
      }

      // Spill to small arrays (cheap; enables simple scalar RLE over 16)
      uint8_t Rt[16], Gt[16], Bt[16], Rb[16], Gb[16], Bb[16];
      vst1q_u8(Rt, top.val[0]);
      vst1q_u8(Gt, top.val[1]);
      vst1q_u8(Bt, top.val[2]);
      vst1q_u8(Rb, bot.val[0]);
      vst1q_u8(Gb, bot.val[1]);
      vst1q_u8(Bb, bot.val[2]);

      // RLE over the 16 cells
      for (int i = 0; i < 16;) {
        uint8_t rT = Rt[i], gT = Gt[i], bT = Bt[i];
        uint8_t rB = Rb[i], gB = Gb[i], bB = Bb[i];

        // Always half-block: U+2580 "▀" (upper half)
        const uint8_t glyph_utf8[3] = {0xE2, 0x96, 0x80};

        // Extend run while next cell has same top+bottom colors
        int j = i + 1;
        for (; j < 16; ++j) {
          if (!(Rt[j] == rT && Gt[j] == gT && Bt[j] == bT && Rb[j] == rB && Gb[j] == gB && Bb[j] == bB))
            break;
        }
        uint32_t run = (uint32_t)(j - i);

        // Check if this is a transparent area (black pixels = padding/background)
        bool is_transparent = (rT == 0 && gT == 0 && bT == 0 && rB == 0 && gB == 0 && bB == 0);

        if (is_transparent) {
          // Reset colors before transparent areas to prevent color bleeding
          if (cur_fr != -1 || cur_fg != -1 || cur_fb != -1 || cur_br != -1 || cur_bg != -1 || cur_bb != -1) {
            emit_reset(&ob);
            cur_fr = cur_fg = cur_fb = -1;
            cur_br = cur_bg = cur_bb = -1;
          }
          // Emit spaces for transparent area (no RLE for padding)
          for (uint32_t k = 0; k < run; ++k) {
            ob_write(&ob, " ", 1);
          }
        } else {
          // Normal colored half-blocks - set fg to TOP, bg to BOTTOM if changed
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

          // Emit glyph once, then REP or literals
          ob_write(&ob, (const char *)glyph_utf8, 3);
          if (rep_is_profitable(run)) {
            emit_rep(&ob, run - 1);
          } else {
            for (uint32_t k = 1; k < run; ++k) {
              ob_write(&ob, (const char *)glyph_utf8, 3);
            }
          }
        }

        i = j;
      }
      x += 16;
    }

    // Scalar tail (or full row if no NEON)
    for (; x < width;) {
      const uint8_t *pT = rowT + (size_t)x * 3u;
      const uint8_t *pB = rowB ? rowB + (size_t)x * 3u : NULL;

      uint8_t rT = pT[0], gT = pT[1], bT = pT[2];
      uint8_t rB = rT, gB = gT, bB = bT;
      if (pB) {
        rB = pB[0];
        gB = pB[1];
        bB = pB[2];
      }

      // Extend run while top and bottom colors match exactly
      int j = x + 1;
      for (; j < width; ++j) {
        const uint8_t *qT = rowT + (size_t)j * 3u;
        const uint8_t *qB = rowB ? rowB + (size_t)j * 3u : NULL;
        uint8_t rT2 = qT[0], gT2 = qT[1], bT2 = qT[2];
        uint8_t rB2 = qB ? qB[0] : rT2, gB2 = qB ? qB[1] : gT2, bB2 = qB ? qB[2] : bT2;
        if (!((rT2 == rT && gT2 == gT && bT2 == bT) && (rB2 == rB && gB2 == gB && bB2 == bB)))
          break;
      }
      uint32_t run = (uint32_t)(j - x);

      // Check if this is a transparent area (black pixels = padding/background)
      bool is_transparent = (rT == 0 && gT == 0 && bT == 0 && rB == 0 && gB == 0 && bB == 0);

      if (is_transparent) {
        // Reset colors before transparent areas to prevent color bleeding
        if (cur_fr != -1 || cur_fg != -1 || cur_fb != -1 || cur_br != -1 || cur_bg != -1 || cur_bb != -1) {
          emit_reset(&ob);
          cur_fr = cur_fg = cur_fb = -1;
          cur_br = cur_bg = cur_bb = -1;
        }
        // Emit spaces for transparent area (no RLE for padding)
        for (uint32_t k = 0; k < run; ++k) {
          ob_write(&ob, " ", 1);
        }
      } else {
        // SGR: fg = TOP, bg = BOTTOM for colored areas
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

        // Always the upper half block "▀" (U+2580)
        static const char HB[3] = {(char)0xE2, (char)0x96, (char)0x80};
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

    // End emitted line: reset and newline (only for non-final lines)
    emit_reset(&ob);
    // Check if this is the last output line (since we process 2 pixel rows per output line)
    if (y + 2 < height) { // Only add newline if not the last output line
      ob_putc(&ob, '\n');
    }
    cur_fr = cur_fg = cur_fb = -1;
    cur_br = cur_bg = cur_bb = -1;
  }

  ob_term(&ob);
  return ob.buf;
}

#endif
