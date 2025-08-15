#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>
#include <math.h>
#include "ascii_simd_neon.h"
#include "ascii_simd.h"
#include "options.h"
#include "common.h"
#include "image.h"

#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#ifndef SIMD_SUPPORT_NEON
#define SIMD_SUPPORT_NEON 1
#endif
#endif

// Forward declarations
static inline size_t write_rgb_triplet(uint8_t value, char *dst);

void str_init(Str *s) {
  s->data = NULL;
  s->len = 0;
  s->cap = 0;
}
void str_free(Str *s) {
  free(s->data);
  s->data = NULL;
  s->len = s->cap = 0;
}
void str_reserve(Str *s, size_t need) {
  if (need <= s->cap)
    return;
  size_t ncap = s->cap ? s->cap : 4096;
  while (ncap < need)
    ncap = (ncap * 3) / 2 + 64;
  char *nd = (char *)realloc(s->data, ncap);
  if (!nd) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  s->data = nd;
  s->cap = ncap;
}
void str_append_bytes(Str *s, const void *src, size_t n) {
  str_reserve(s, s->len + n);
  memcpy(s->data + s->len, src, n);
  s->len += n;
}
void str_append_c(Str *s, char c) {
  str_reserve(s, s->len + 1);
  s->data[s->len++] = c;
}
void str_printf(Str *s, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char stackbuf[256];
  int n = vsnprintf(stackbuf, sizeof(stackbuf), fmt, ap);
  va_end(ap);
  if (n < 0)
    return;
  if ((size_t)n < sizeof(stackbuf)) {
    str_append_bytes(s, stackbuf, (size_t)n);
    return;
  }
  char *heap = (char *)malloc((size_t)n + 1);
  if (!heap) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  va_start(ap, fmt);
  vsnprintf(heap, (size_t)n + 1, fmt, ap);
  va_end(ap);
  str_append_bytes(s, heap, (size_t)n);
  free(heap);
}

#ifdef SIMD_SUPPORT_NEON

// Allocate a new image (RGB8), abort on OOM
ImageRGB alloc_image(int w, int h) {
  ImageRGB out;
  out.w = w;
  out.h = h;
  size_t n = (size_t)w * (size_t)h * 3u;
  out.pixels = (uint8_t *)malloc(n);
  if (!out.pixels) {
    fprintf(stderr, "OOM\n");
    exit(1);
  }
  return out;
}

// ===== add near top (config) =====
#ifndef BGASCII_LUMA_THRESHOLD
#define BGASCII_LUMA_THRESHOLD 128 // Y >= 128 -> black text; else white text
#endif

// ------------------------------------------------------------
// ASCII renderer config: 16 glyph ramp for SIMD nibble LUT
static const char *ascii_ramp16(void) {
  // 16 chars (light → dark). tweak to taste but keep length == 16.
  return " .:-=+*#%B8MW@$\xA7";
}

// Map luminance [0..255] → 4-bit index [0..15] using top nibble
static inline uint8x16_t luma_to_idx_nibble(uint8x16_t y) {
  return vshrq_n_u8(y, 4);
}

// Emit ANSI SGR for truecolor FG/BG - use fast manual builder
static inline void emit_sgr(Str *out, int fr, int fg, int fb, int br, int bg, int bb) {
  // Manual fast build: "\x1b[38;2;R;G;Bm\x1b[48;2;R;G;Bm"
  str_reserve(out, out->len + 40); // Max size for 2 SGR sequences
  char *p = out->data + out->len;

  // FG sequence: "\x1b[38;2;R;G;Bm"
  memcpy(p, "\x1b[38;2;", 7); p += 7;
  p += sprintf(p, "%d;%d;%dm", fr, fg, fb);

  // BG sequence: "\x1b[48;2;R;G;Bm"
  memcpy(p, "\x1b[48;2;", 7); p += 7;
  p += sprintf(p, "%d;%d;%dm", br, bg, bb);

  out->len = (size_t)(p - out->data);
}

// Emit reset SGR
static inline void emit_reset(Str *out) {
  str_append_bytes(out, "\x1b[0m", 4);
}

// Direct SGR string builders for our streaming function
static inline char *append_sgr_reset(char *dst) {
  static const char RESET[] = "\033[0m";
  memcpy(dst, RESET, sizeof(RESET) - 1);
  return dst + (sizeof(RESET) - 1);
}

// Fast 256-color SGR builders
static inline char *append_sgr256_fg(char *dst, uint8_t fg) {
  uint8_t len;
  char *sgr_str = get_sgr256_fg_string(fg, &len);
  memcpy(dst, sgr_str, len);
  return dst + len;
}

// REP compression helper for single-byte characters
static inline char *emit_run_rep(char *p, int run_len, char ch) {
    if (run_len >= 3) {
        // Use REP sequence: \x1b[<count>b<char>
        *p++ = '\x1b';
        *p++ = '[';
        if (run_len >= 100) {
            *p++ = '0' + (run_len / 100);
            run_len %= 100;
            *p++ = '0' + (run_len / 10);
            *p++ = '0' + (run_len % 10);
        } else if (run_len >= 10) {
            *p++ = '0' + (run_len / 10);
            *p++ = '0' + (run_len % 10);
        } else {
            *p++ = '0' + run_len;
        }
        *p++ = 'b';
        *p++ = ch;
    } else {
        // Short runs: direct repetition (avoids REP overhead)
        for (int r = 0; r < run_len; r++) {
            *p++ = ch;
        }
    }
    return p;
}

// REP compression helper for UTF-8 ▀ block characters
static inline char *emit_run_rep_utf8_block(char *p, int run_len) {
    if (run_len >= 3) {
        // Use REP sequence: \x1b[<count>b + single ▀
        *p++ = '\x1b';
        *p++ = '[';
        if (run_len >= 100) {
            *p++ = '0' + (run_len / 100);
            run_len %= 100;
            *p++ = '0' + (run_len / 10);
            *p++ = '0' + (run_len % 10);
        } else if (run_len >= 10) {
            *p++ = '0' + (run_len / 10);
            *p++ = '0' + (run_len % 10);
        } else {
            *p++ = '0' + run_len;
        }
        *p++ = 'b';
        *p++ = 0xE2;  // ▀ UTF-8 sequence
        *p++ = 0x96;
        *p++ = 0x80;
    } else {
        // Short runs: direct UTF-8 repetition
        for (int r = 0; r < run_len; r++) {
            *p++ = 0xE2;
            *p++ = 0x96;
            *p++ = 0x80;
        }
    }
    return p;
}

static inline char *append_sgr256_fg_bg(char *dst, uint8_t fg, uint8_t bg) {
  uint8_t len;
  char *sgr_str = get_sgr256_fg_bg_string(fg, bg, &len);
  memcpy(dst, sgr_str, len);
  return dst + len;
}

// RGB to ANSI 256-color conversion (scalar fallback)
static inline uint8_t rgb_to_ansi256(uint8_t r, uint8_t g, uint8_t b) {
  // Convert to 6-level cube coordinates (0-5)
  int cr = (r * 5 + 127) / 255;
  int cg = (g * 5 + 127) / 255;
  int cb = (b * 5 + 127) / 255;

  // Check if it's closer to a gray level (colors 232-255: 24 grays)
  int gray = (r + g + b) / 3;
  int closest_gray_idx = 232 + (gray * 23) / 255;

  // Calculate actual gray value for this index
  int gray_level = 8 + (closest_gray_idx - 232) * 10;
  int gray_dist = abs(gray - gray_level);

  // Calculate 6x6x6 cube color distance
  int cube_r = (cr * 255) / 5;
  int cube_g = (cg * 255) / 5;
  int cube_b = (cb * 255) / 5;
  int cube_dist = abs(r - cube_r) + abs(g - cube_g) + abs(b - cube_b);

  if (gray_dist < cube_dist) {
    return (uint8_t)closest_gray_idx;
  } else {
    return (uint8_t)(16 + cr * 36 + cg * 6 + cb);
  }
}

// Enhanced REP compression writer that supports both 256-color indices and truecolor RGB
// Signature matches your optimized examples: (fg_r, fg_g, fg_b, bg_r, bg_g, bg_b, fg_idx, bg_idx, ascii_chars, ...)
size_t write_row_rep_from_arrays_enhanced(
    const uint8_t *fg_r, const uint8_t *fg_g, const uint8_t *fg_b,  // Truecolor FG RGB (NULL if using indices)
    const uint8_t *bg_r, const uint8_t *bg_g, const uint8_t *bg_b,  // Truecolor BG RGB (NULL if using indices)
    const uint8_t *fg_idx, const uint8_t *bg_idx,                   // 256-color indices (NULL if using RGB)
    const char    *ascii_chars,                                      // ASCII chars (NULL if UTF-8 block mode)
    int width,
    char *dst,
    size_t cap,
    bool utf8_block_mode,
    bool use_256color,
    bool is_truecolor)
{
    char *p = dst;
    char *row_end = dst + cap - 32;  // safety margin
    bool have_color = false;
    uint8_t last_fg_r = 0, last_fg_g = 0, last_fg_b = 0;
    uint8_t last_bg_r = 0, last_bg_g = 0, last_bg_b = 0;
    uint8_t last_fg_idx = 0, last_bg_idx = 0;
    char last_char = 0;
    int run_len = 0;

    for (int x = 0; x < width; x++) {
        char ch = utf8_block_mode ? 0 : ascii_chars[x];
        bool color_changed = false;

        if (is_truecolor) {
            // Compare RGB values for truecolor mode
            uint8_t curr_fg_r = fg_r ? fg_r[x] : 0;
            uint8_t curr_fg_g = fg_g ? fg_g[x] : 0;
            uint8_t curr_fg_b = fg_b ? fg_b[x] : 0;
            uint8_t curr_bg_r = bg_r ? bg_r[x] : 0;
            uint8_t curr_bg_g = bg_g ? bg_g[x] : 0;
            uint8_t curr_bg_b = bg_b ? bg_b[x] : 0;

            color_changed = (!have_color ||
                            curr_fg_r != last_fg_r || curr_fg_g != last_fg_g || curr_fg_b != last_fg_b ||
                            (bg_r && (curr_bg_r != last_bg_r || curr_bg_g != last_bg_g || curr_bg_b != last_bg_b)));
        } else {
            // Compare indices for 256-color mode
            uint8_t fg = fg_idx[x];
            uint8_t bg = bg_idx ? bg_idx[x] : 0;
            color_changed = (!have_color || fg != last_fg_idx || (bg_idx && bg != last_bg_idx));
        }

        if (color_changed) {
            // Flush any pending run
            if (run_len > 0) {
                p = utf8_block_mode ? emit_run_rep_utf8_block(p, run_len)
                                    : emit_run_rep(p, run_len, last_char);
                run_len = 0;
            }

            // Emit new color sequence
            if (is_truecolor) {
                // Truecolor mode: use RGB values
                uint8_t fg_r_val = fg_r ? fg_r[x] : 0, fg_g_val = fg_g ? fg_g[x] : 0, fg_b_val = fg_b ? fg_b[x] : 0;
                uint8_t bg_r_val = bg_r ? bg_r[x] : 0, bg_g_val = bg_g ? bg_g[x] : 0, bg_b_val = bg_b ? bg_b[x] : 0;

                if (bg_r) {
                    // Background mode: FG+BG truecolor
                    memcpy(p, "\033[38;2;", 7); p += 7;
                    p += write_rgb_triplet(fg_r_val, p); *p++ = ';';
                    p += write_rgb_triplet(fg_g_val, p); *p++ = ';';
                    p += write_rgb_triplet(fg_b_val, p);
                    memcpy(p, ";48;2;", 6); p += 6;
                    p += write_rgb_triplet(bg_r_val, p); *p++ = ';';
                    p += write_rgb_triplet(bg_g_val, p); *p++ = ';';
                    p += write_rgb_triplet(bg_b_val, p); *p++ = 'm';
                } else {
                    // Foreground mode: FG truecolor only
                    memcpy(p, "\033[38;2;", 7); p += 7;
                    p += write_rgb_triplet(fg_r_val, p); *p++ = ';';
                    p += write_rgb_triplet(fg_g_val, p); *p++ = ';';
                    p += write_rgb_triplet(fg_b_val, p); *p++ = 'm';
                }

                last_fg_r = fg_r_val; last_fg_g = fg_g_val; last_fg_b = fg_b_val;
                last_bg_r = bg_r_val; last_bg_g = bg_g_val; last_bg_b = bg_b_val;
            } else {
                // 256-color mode: use existing logic
                uint8_t fg = fg_idx[x];
                uint8_t bg = bg_idx ? bg_idx[x] : 0;

                if (bg_idx) {
                    p = append_sgr256_fg_bg(p, fg, bg);
                } else {
                    p = append_sgr256_fg(p, fg);
                }

                last_fg_idx = fg; last_bg_idx = bg;
            }

            have_color = true;
            last_char = ch;
            run_len = 1;

        } else if (!utf8_block_mode && ch != last_char) {
            // ASCII mode: character changed, flush run
            if (run_len > 0) {
                p = emit_run_rep(p, run_len, last_char);
            }
            last_char = ch;
            run_len = 1;

        } else {
            run_len++;
        }

        if (p >= row_end - 16) break;  // safety check
    }

    // Flush final run
    if (run_len > 0) {
        p = utf8_block_mode ? emit_run_rep_utf8_block(p, run_len)
                            : emit_run_rep(p, run_len, last_char);
    }

    // Reset sequence
    memcpy(p, "\033[0m", 4); p += 4;
    return p - dst;
}

// Legacy wrapper for existing 256-color code
static size_t write_row_rep_from_arrays(
    const uint8_t *fg_idx,
    const uint8_t *bg_idx,   // NULL if FG-only
    const char    *ascii_chars, // NULL if UTF-8 block mode
    int width,
    char *dst,
    size_t cap,
    bool utf8_block_mode,
    bool use_256color)
{
    return write_row_rep_from_arrays_enhanced(
        NULL, NULL, NULL,      // No truecolor FG RGB
        NULL, NULL, NULL,      // No truecolor BG RGB
        fg_idx, bg_idx,        // 256-color indices
        ascii_chars,           // ASCII characters
        width, dst, cap,
        utf8_block_mode,
        use_256color,
        false);                // Not truecolor mode
}

// ------------------------------------------------------------
// SIMD luminance: Y = (77R + 150G + 29B) >> 8
static inline uint8x16_t simd_luma(uint8x16_t r, uint8x16_t g, uint8x16_t b) {
  uint16x8_t rl = vmovl_u8(vget_low_u8(r));
  uint16x8_t rh = vmovl_u8(vget_high_u8(r));
  uint16x8_t gl = vmovl_u8(vget_low_u8(g));
  uint16x8_t gh = vmovl_u8(vget_high_u8(g));
  uint16x8_t bl = vmovl_u8(vget_low_u8(b));
  uint16x8_t bh = vmovl_u8(vget_high_u8(b));

  uint32x4_t l0 = vmull_n_u16(vget_low_u16(rl), 77);
  uint32x4_t l1 = vmull_n_u16(vget_high_u16(rl), 77);
  l0 = vmlal_n_u16(l0, vget_low_u16(gl), 150);
  l1 = vmlal_n_u16(l1, vget_high_u16(gl), 150);
  l0 = vmlal_n_u16(l0, vget_low_u16(bl), 29);
  l1 = vmlal_n_u16(l1, vget_high_u16(bl), 29);

  uint32x4_t h0 = vmull_n_u16(vget_low_u16(rh), 77);
  uint32x4_t h1 = vmull_n_u16(vget_high_u16(rh), 77);
  h0 = vmlal_n_u16(h0, vget_low_u16(gh), 150);
  h1 = vmlal_n_u16(h1, vget_high_u16(gh), 150);
  h0 = vmlal_n_u16(h0, vget_low_u16(bh), 29);
  h1 = vmlal_n_u16(h1, vget_high_u16(bh), 29);

  uint16x8_t l = vcombine_u16(vrshrn_n_u32(l0, 8), vrshrn_n_u32(l1, 8));
  uint16x8_t h = vcombine_u16(vrshrn_n_u32(h0, 8), vrshrn_n_u32(h1, 8));
  return vcombine_u8(vqmovn_u16(l), vqmovn_u16(h));
}

// ------------------------------------------------------------
// ASCII grayscale renderer (SIMD)
void render_ascii_grayscale(const ImageRGB *img, Str *out) {
  const int W = img->w, H = img->h;
  const uint8_t *p = img->pixels;

  str_reserve(out, out->len + (size_t)W * (size_t)(H + 1));

  // 16-byte LUT table with glyphs
  uint8x16_t lut = vld1q_u8((const uint8_t *)ascii_ramp16());

  for (int y = 0; y < H; ++y) {
    const uint8_t *row = p + (size_t)y * (size_t)W * 3u;
    int x = 0;
    while (x + 16 <= W) {
      uint8x16x3_t rgb = vld3q_u8(row + (size_t)x * 3u);
      uint8x16_t Y = simd_luma(rgb.val[0], rgb.val[1], rgb.val[2]);
      uint8x16_t idx = luma_to_idx_nibble(Y);
      uint8x16_t glyphs = vqtbl1q_u8(lut, idx);
      uint8_t tmp[16];
      vst1q_u8(tmp, glyphs);
      str_append_bytes(out, tmp, 16);
      x += 16;
    }
    // tail
    for (; x < W; ++x) {
      const uint8_t *px = row + (size_t)x * 3u;
      uint16_t yv = (77u * px[0] + 150u * px[1] + 29u * px[2]) >> 8;
      uint8_t idx = (uint8_t)(yv >> 4);
      str_append_c(out, ascii_ramp16()[idx]);
    }
    str_append_c(out, '\n');
  }
}

// ------------------------------------------------------------
// Half-upper-block renderer with truecolor FG(top)/BG(bottom)
// ANSI RLE across cells: keep (FG,BG) and only re-emit when changed.
// RLEState is now defined in ascii_simd.h

void rle_init(RLEState *st) {
  st->cFR = st->cFG = st->cFB = st->cBR = st->cBG = st->cBB = -1;
  st->runLen = 0;
  st->seeded = 0;
}

void rle_flush(Str *out, RLEState *st, int FR, int FG, int FB, int BR, int BG, int BB) {
  if (st->runLen <= 0)
    return;
  // If color changed vs currently set on terminal, emit SGR
  if (!st->seeded || FR != st->cFR || FG != st->cFG || FB != st->cFB || BR != st->cBR || BG != st->cBG ||
      BB != st->cBB) {
    emit_sgr(out, FR, FG, FB, BR, BG, BB);
    st->cFR = FR;
    st->cFG = FG;
    st->cFB = FB;
    st->cBR = BR;
    st->cBG = BG;
    st->cBB = BB;
    st->seeded = 1;
  }
  // emit st->runLen copies of '▀'
  static const char glyph[] = "\xE2\x96\x80"; // U+2580
  for (int i = 0; i < st->runLen; ++i)
    str_append_bytes(out, glyph, 3);
  st->runLen = 0;
}

void render_halfblock_truecolor(const ImageRGB *img, Str *out) {
  const int W = img->w;
  const int H = img->h & ~1; // even
  const uint8_t *p = img->pixels;

  for (int y = 0; y < H; y += 2) {
    const uint8_t *rowT = p + (size_t)y * (size_t)W * 3u;
    const uint8_t *rowB = p + (size_t)(y + 1) * (size_t)W * 3u;

    RLEState st;
    rle_init(&st);

    int x = 0;
    while (x + 16 <= W) {
      uint8x16x3_t T = vld3q_u8(rowT + (size_t)x * 3u);
      uint8x16x3_t B = vld3q_u8(rowB + (size_t)x * 3u);

      // Move lanes to scalars for span finding (SIMD loads/stores, scalar run accounting)
      uint8_t rT[16], gT[16], bT[16], rB[16], gB[16], bB[16];
      vst1q_u8(rT, T.val[0]);
      vst1q_u8(gT, T.val[1]);
      vst1q_u8(bT, T.val[2]);
      vst1q_u8(rB, B.val[0]);
      vst1q_u8(gB, B.val[1]);
      vst1q_u8(bB, B.val[2]);

      int i = 0;
      while (i < 16) {
        int FR = rT[i], FG = gT[i], FB = bT[i];
        int BR = rB[i], BG = gB[i], BB = bB[i];
        // grow span while colors match
        int j = i + 1;
        while (j < 16 && rT[j] == FR && gT[j] == FG && bT[j] == FB && rB[j] == BR && gB[j] == BG && bB[j] == BB) {
          ++j;
        }
        // if current run's color differs from this span's color, flush it first
        if (st.runLen > 0 &&
            (FR != st.cFR || FG != st.cFG || FB != st.cFB || BR != st.cBR || BG != st.cBG || BB != st.cBB)) {
          rle_flush(out, &st, st.cFR, st.cFG, st.cFB, st.cBR, st.cBG, st.cBB);
        }
        // add span to run using this color
        st.cFR = FR;
        st.cFG = FG;
        st.cFB = FB;
        st.cBR = BR;
        st.cBG = BG;
        st.cBB = BB;
        st.seeded = 1;
        st.runLen += (j - i);
        // emit now (keeps memory footprint small; optional)
        rle_flush(out, &st, FR, FG, FB, BR, BG, BB);
        i = j;
      }
      x += 16;
    }

    // tail
    while (x < W) {
      int FR = rowT[3 * (size_t)x + 0], FG = rowT[3 * (size_t)x + 1], FB = rowT[3 * (size_t)x + 2];
      int BR = rowB[3 * (size_t)x + 0], BG = rowB[3 * (size_t)x + 1], BB = rowB[3 * (size_t)x + 2];
      if (st.runLen > 0 &&
          (FR != st.cFR || FG != st.cFG || FB != st.cFB || BR != st.cBR || BG != st.cBG || BB != st.cBB)) {
        rle_flush(out, &st, st.cFR, st.cFG, st.cFB, st.cBR, st.cBG, st.cBB);
      }
      st.cFR = FR;
      st.cFG = FG;
      st.cFB = FB;
      st.cBR = BR;
      st.cBG = BG;
      st.cBB = BB;
      st.seeded = 1;
      st.runLen += 1;
      ++x;
    }

    // flush line + reset
    rle_flush(out, &st, st.cFR, st.cFG, st.cFB, st.cBR, st.cBG, st.cBB);
    emit_reset(out);
    str_append_c(out, '\n');
  }
}

// ===== add near emit helpers =====
static inline void emit_sgr_256(Str *out, int fg_idx, int bg_idx) {
  // fg_idx/bg_idx in [0..255], or -1 to skip that side
  if (fg_idx >= 0 && bg_idx >= 0) {
    uint8_t len;
    char *sgr_str = get_sgr256_fg_bg_string((uint8_t)fg_idx, (uint8_t)bg_idx, &len);
    str_append_bytes(out, sgr_str, len);
  } else if (fg_idx >= 0) {
    uint8_t len;
    char *sgr_str = get_sgr256_fg_string((uint8_t)fg_idx, &len);
    str_append_bytes(out, sgr_str, len);
  } else if (bg_idx >= 0) {
    // Need to implement get_sgr256_bg_string or use manual builder for BG-only
    str_printf(out, "\x1b[48;5;%dm", bg_idx);  // Fallback for BG-only case
  }
}

// ===== SIMD helpers for 256-color quantization =====

// Approximate quantize 0..255 -> 0..5 : q ≈ round(x*5/255) = (x*5 + 128)>>8
static inline uint8x16_t q6_from_u8(uint8x16_t x) {
  uint16x8_t xl = vmovl_u8(vget_low_u8(x));
  uint16x8_t xh = vmovl_u8(vget_high_u8(x));
  xl = vmlaq_n_u16(vdupq_n_u16(0), xl, 5);
  xh = vmlaq_n_u16(vdupq_n_u16(0), xh, 5);
  xl = vaddq_u16(xl, vdupq_n_u16(128));
  xh = vaddq_u16(xh, vdupq_n_u16(128));
  xl = vshrq_n_u16(xl, 8);
  xh = vshrq_n_u16(xh, 8);
  return vcombine_u8(vqmovn_u16(xl), vqmovn_u16(xh)); // 0..5
}

// Make 256-color index (cube vs gray). threshold: max-min < thr ⇒ gray
#ifndef CUBE_GRAY_THRESHOLD
#define CUBE_GRAY_THRESHOLD 10
#endif

// Ordered dither matrix for reducing color variation and extending runs
static const uint8_t dither4x4[16] = {
   0,  8,  2, 10,
  12,  4, 14,  6,
   3, 11,  1,  9,
  15,  7, 13,  5
};

// Apply ordered dithering to reduce color variations (creates longer runs)
static inline uint8x16_t apply_ordered_dither(uint8x16_t color, int pixel_offset, uint8_t dither_strength) {
  if (dither_strength == 0) return color;

  // Create dither pattern for 16 pixels based on position
  uint8_t dither_vals[16];
  for (int i = 0; i < 16; i++) {
    int x = (pixel_offset + i) & 3;  // x position in 4x4 pattern
    int y = ((pixel_offset + i) >> 2) & 3;  // y position in 4x4 pattern
    uint8_t dither = dither4x4[y * 4 + x];
    // Scale dither to strength: 0-15 -> 0-strength
    dither_vals[i] = (dither * dither_strength) >> 4;
  }

  uint8x16_t dither_v = vld1q_u8(dither_vals);
  return vqsubq_u8(color, dither_v); // Subtract dither (clamp to 0)
}

uint8x16_t palette256_index_dithered(uint8x16_t r, uint8x16_t g, uint8x16_t b, int pixel_offset) {
  // Apply light ordered dithering to create longer runs (strength = 8)
  r = apply_ordered_dither(r, pixel_offset, 8);
  g = apply_ordered_dither(g, pixel_offset + 1, 8);
  b = apply_ordered_dither(b, pixel_offset + 2, 8);

  // cube index
  uint8x16_t R6 = q6_from_u8(r);
  uint8x16_t G6 = q6_from_u8(g);
  uint8x16_t B6 = q6_from_u8(b);

  // idx_cube = 16 + R6*36 + G6*6 + B6  (do in 16-bit to avoid overflow)
  uint16x8_t R6l = vmovl_u8(vget_low_u8(R6));
  uint16x8_t R6h = vmovl_u8(vget_high_u8(R6));
  uint16x8_t G6l = vmovl_u8(vget_low_u8(G6));
  uint16x8_t G6h = vmovl_u8(vget_high_u8(G6));
  uint16x8_t B6l = vmovl_u8(vget_low_u8(B6));
  uint16x8_t B6h = vmovl_u8(vget_high_u8(B6));

  uint16x8_t idxl = vmlaq_n_u16(vmulq_n_u16(R6l, 36), G6l, 6);
  uint16x8_t idxh = vmlaq_n_u16(vmulq_n_u16(R6h, 36), G6h, 6);
  idxl = vaddq_u16(idxl, B6l);
  idxh = vaddq_u16(idxh, B6h);
  idxl = vaddq_u16(idxl, vdupq_n_u16(16));
  idxh = vaddq_u16(idxh, vdupq_n_u16(16));

  // gray decision: max-min < thr ?
  uint8x16_t maxrg = vmaxq_u8(r, g);
  uint8x16_t minrg = vminq_u8(r, g);
  uint8x16_t maxrgb = vmaxq_u8(maxrg, b);
  uint8x16_t minrgb = vminq_u8(minrg, b);
  uint8x16_t diff = vsubq_u8(maxrgb, minrgb);
  uint8x16_t thr = vdupq_n_u8((uint8_t)CUBE_GRAY_THRESHOLD);
  uint8x16_t is_gray = vcltq_u8(diff, thr);

  // gray idx = 232 + round(Y*23/255)
  uint8x16_t Y = simd_luma(r, g, b);
  // q23 ≈ round(Y*23/255) = (Y*23 + 128)>>8
  uint16x8_t Yl = vmovl_u8(vget_low_u8(Y));
  uint16x8_t Yh = vmovl_u8(vget_high_u8(Y));
  Yl = vmlaq_n_u16(vdupq_n_u16(0), Yl, 23);
  Yh = vmlaq_n_u16(vdupq_n_u16(0), Yh, 23);
  Yl = vaddq_u16(Yl, vdupq_n_u16(128));
  Yh = vaddq_u16(Yh, vdupq_n_u16(128));
  Yl = vshrq_n_u16(Yl, 8);
  Yh = vshrq_n_u16(Yh, 8);
  uint16x8_t gidxl = vaddq_u16(Yl, vdupq_n_u16(232));
  uint16x8_t gidxh = vaddq_u16(Yh, vdupq_n_u16(232));

  // select gray or cube per lane
  uint8x16_t idx_cube = vcombine_u8(vqmovn_u16(idxl), vqmovn_u16(idxh));
  uint8x16_t idx_gray = vcombine_u8(vqmovn_u16(gidxl), vqmovn_u16(gidxh));
  return vbslq_u8(is_gray, idx_gray, idx_cube);
}

// ULTRA-FAST emergency fix - just use luminance for color approximation
static inline uint8x16_t palette256_index(uint8x16_t r, uint8x16_t g, uint8x16_t b) {
  // EMERGENCY FIX: Ultra-simple - just map luminance to 16-255 color range
  // This is incorrect color-wise but extremely fast
  uint8x16_t luma = simd_luma(r, g, b);

  // Map luminance 0-255 -> color indices 16-255 (240 colors)
  // idx = 16 + (luma * 240) / 256 = 16 + (luma * 15) / 16
  uint16x8_t luma_lo = vmovl_u8(vget_low_u8(luma));
  uint16x8_t luma_hi = vmovl_u8(vget_high_u8(luma));

  // Multiply by 15, then shift by 4 (divide by 16)
  uint16x8_t scaled_lo = vshrq_n_u16(vmulq_n_u16(luma_lo, 15), 4);
  uint16x8_t scaled_hi = vshrq_n_u16(vmulq_n_u16(luma_hi, 15), 4);

  // Add base offset 16
  uint16x8_t idx_lo = vaddq_u16(scaled_lo, vdupq_n_u16(16));
  uint16x8_t idx_hi = vaddq_u16(scaled_hi, vdupq_n_u16(16));

  return vcombine_u8(vqmovn_u16(idx_lo), vqmovn_u16(idx_hi));
}

// ===== add with the other declarations =====

// ===== paste alongside other renderers =====
// Background-colored ASCII: BG = pixel color, FG ∈ {white, black} chosen by luma.
// ANSI RLE: groups spans where (FG,BG) stays constant; glyphs may vary within the run.
void render_ascii_bgcolor(const ImageRGB *img, Str *out) {
  const int W = img->w, H = img->h;
  const uint8_t *p = img->pixels;

  // Glyph ramp LUT (same as grayscale path)
  const uint8x16_t lut = vld1q_u8((const uint8_t *)ascii_ramp16());
  const uint8_t glyph_white_black[2][3] = {{255, 255, 255}, {0, 0, 0}}; // 0=white,1=black

  for (int y = 0; y < H; ++y) {
    const uint8_t *row = p + (size_t)y * (size_t)W * 3u;

    // RLE state for current terminal SGR
    int cFR = -1, cFG = -1, cFB = -1, cBR = -1, cBG = -1, cBB = -1;
    int seeded = 0;

    int x = 0;
    while (x + 16 <= W) {
      // SIMD load + luma + glyph lookup
      uint8x16x3_t rgb = vld3q_u8(row + (size_t)x * 3u);
      uint8x16_t Y = simd_luma(rgb.val[0], rgb.val[1], rgb.val[2]); // 0..255
      uint8x16_t idx = luma_to_idx_nibble(Y);                       // 0..15
      uint8x16_t glyphsV = vqtbl1q_u8(lut, idx);

      // Decide FG (white/black) from Y >= threshold
      uint8x16_t thr = vdupq_n_u8((uint8_t)BGASCII_LUMA_THRESHOLD);
      uint8x16_t ge_mask = vcgeq_u8(Y, thr); // 0xFF where Y >= thr (black text)

      // Spill to scalars for span finding (BG varies a lot in practice)
      uint8_t r[16], g[16], b[16], ch[16], mask[16];
      vst1q_u8(r, rgb.val[0]);
      vst1q_u8(g, rgb.val[1]);
      vst1q_u8(b, rgb.val[2]);
      vst1q_u8(ch, glyphsV);
      vst1q_u8(mask, ge_mask);

      int i = 0;
      while (i < 16) {
        // Determine span FG/BG from lane i
        int use_black = (mask[i] == 0xFF); // 1 = black, 0 = white
        int FR = glyph_white_black[use_black][0];
        int FGc = glyph_white_black[use_black][1];
        int FBc = glyph_white_black[use_black][2];
        int BR = r[i], BGc = g[i], BB = b[i];

        // Grow span while both FG choice and BG color stay the same
        int j = i + 1;
        while (j < 16) {
          int ub = (mask[j] == 0xFF);
          if (ub != use_black || r[j] != BR || g[j] != BGc || b[j] != BB)
            break;
          ++j;
        }

        // If terminal's current SGR differs, update once (RLE)
        if (!seeded || FR != cFR || FGc != cFG || FBc != cFB || BR != cBR || BGc != cBG || BB != cBB) {
          emit_sgr(out, FR, FGc, FBc, BR, BGc, BB);
          cFR = FR;
          cFG = FGc;
          cFB = FBc;
          cBR = BR;
          cBG = BGc;
          cBB = BB;
          seeded = 1;
        }

        // Emit glyphs for this span (they can vary freely)
        str_append_bytes(out, (const void *)(ch + i), (size_t)(j - i));

        i = j;
      }

      x += 16;
    }

    // Tail (scalar)
    while (x < W) {
      const uint8_t *px = row + (size_t)x * 3u;
      uint16_t yv = (77u * px[0] + 150u * px[1] + 29u * px[2]) >> 8;
      int use_black = (yv >= BGASCII_LUMA_THRESHOLD);
      int FR = use_black ? 0 : 255;
      int FGc = use_black ? 0 : 255;
      int FBc = use_black ? 0 : 255;
      int BR = px[0], BGc = px[1], BB = px[2];

      // SGR if needed
      if (FR != cFR || FGc != cFG || FBc != cFB || BR != cBR || BGc != cBG || BB != cBB) {
        emit_sgr(out, FR, FGc, FBc, BR, BGc, BB);
        cFR = FR;
        cFG = FGc;
        cFB = FBc;
        cBR = BR;
        cBG = BGc;
        cBB = BB;
        seeded = 1;
      }

      // ASCII glyph by nibble from the same ramp
      uint8_t idx = (uint8_t)(yv >> 4);
      str_append_c(out, ascii_ramp16()[idx]);

      ++x;
    }

    emit_reset(out);
    str_append_c(out, '\n');
  }
}

// ===== new renderer =====
void render_ascii_bgcolor_256(const ImageRGB *img, Str *out) {
  const int W = img->w, H = img->h;
  const uint8x16_t lut = vld1q_u8((const uint8_t *)ascii_ramp16());
  const uint8x16_t thr = vdupq_n_u8((uint8_t)BGASCII_LUMA_THRESHOLD);

  for (int y = 0; y < H; ++y) {
    const uint8_t *row = img->pixels + (size_t)y * (size_t)W * 3u;
    int cur_fg = -2, cur_bg = -2; // -2 = unset

    int x = 0;
    while (x + 16 <= W) {
      uint8x16x3_t rgb = vld3q_u8(row + (size_t)x * 3u);
      uint8x16_t Y = simd_luma(rgb.val[0], rgb.val[1], rgb.val[2]);
      uint8x16_t idx = vqtbl1q_u8(lut, vshrq_n_u8(Y, 4)); // ASCII glyph
      uint8x16_t bg_idx = palette256_index_dithered(rgb.val[0], rgb.val[1], rgb.val[2], x);

      // text color: Y>=thr ⇒ black(0), else white(15)
      uint8x16_t ge = vcgeq_u8(Y, thr);
      uint8x16_t fg_idx_v = vbslq_u8(ge, vdupq_n_u8(0), vdupq_n_u8(15));

      // IN-VECTOR RUN DETECTION: Use vector operations for boundary detection
      uint8x16_t fg_shifted = vextq_u8(vdupq_n_u8((uint8_t)cur_fg), fg_idx_v, 15);
      uint8x16_t bg_shifted = vextq_u8(vdupq_n_u8((uint8_t)cur_bg), bg_idx, 15);

      uint8x16_t fg_same = vceqq_u8(fg_idx_v, fg_shifted);
      uint8x16_t bg_same = vceqq_u8(bg_idx, bg_shifted);
      uint8x16_t both_same = vandq_u8(fg_same, bg_same);
      uint8x16_t boundary_mask = vmvnq_u8(both_same); // 0xFF = boundary

      // Extract for processing (SIMD pre-filtered boundaries)
      uint8_t boundaries[16], fg_idx_arr[16], bg_idx_a[16], glyphs[16];
      vst1q_u8(boundaries, boundary_mask);
      vst1q_u8(fg_idx_arr, fg_idx_v);
      vst1q_u8(bg_idx_a, bg_idx);
      vst1q_u8(glyphs, idx);

      int i = 0;
      while (i < 16) {
        int fgc = fg_idx_arr[i];
        int bgc = bg_idx_a[i];
        int j = i + 1;

        // Skip to next boundary using SIMD-detected boundaries
        while (j < 16 && boundaries[j] == 0)
          ++j;

        if (fgc != cur_fg || bgc != cur_bg) {
          emit_sgr_256(out, fgc, bgc);
          cur_fg = fgc;
          cur_bg = bgc;
        }
        str_append_bytes(out, glyphs + i, (size_t)(j - i));
        i = j;
      }
      x += 16;
    }
    // tail (scalar)
    for (; x < W; ++x) {
      const uint8_t *px = row + (size_t)x * 3u;
      uint16_t yv = (77u * px[0] + 150u * px[1] + 29u * px[2]) >> 8;
      int fg = (yv >= BGASCII_LUMA_THRESHOLD) ? 0 : 15;
      // quick scalar 256 mapping (same math as SIMD)
      int r6 = (px[0] * 5 + 128) >> 8;
      int g6 = (px[1] * 5 + 128) >> 8;
      int b6 = (px[2] * 5 + 128) >> 8;
      int maxv = px[0] > px[1] ? (px[0] > px[2] ? px[0] : px[2]) : (px[1] > px[2] ? px[1] : px[2]);
      int minv = px[0] < px[1] ? (px[0] < px[2] ? px[0] : px[2]) : (px[1] < px[2] ? px[1] : px[2]);
      int bg = (maxv - minv < CUBE_GRAY_THRESHOLD) ? (232 + ((yv * 23 + 128) >> 8)) : (16 + r6 * 36 + g6 * 6 + b6);
      if (fg != cur_fg || bg != cur_bg) {
        emit_sgr_256(out, fg, bg);
        cur_fg = fg;
        cur_bg = bg;
      }
      str_append_c(out, ascii_ramp16()[yv >> 4]);
    }
    emit_reset(out);
    str_append_c(out, '\n');
  }
}

void render_halfblock_256(const ImageRGB *img, Str *out) {
  const int W = img->w, H = img->h & ~1;
  static const char glyph[] = "\xE2\x96\x80";

  for (int y = 0; y < H; y += 2) {
    const uint8_t *rowT = img->pixels + (size_t)y * (size_t)W * 3u;
    const uint8_t *rowB = img->pixels + (size_t)(y + 1) * (size_t)W * 3u;

    int cur_fg = -2, cur_bg = -2;

    int x = 0;
    while (x + 16 <= W) {
      uint8x16x3_t T = vld3q_u8(rowT + (size_t)x * 3u);
      uint8x16x3_t B = vld3q_u8(rowB + (size_t)x * 3u);

      uint8x16_t fg_idx_v = palette256_index_dithered(T.val[0], T.val[1], T.val[2], x);
      uint8x16_t bg_idx_v = palette256_index_dithered(B.val[0], B.val[1], B.val[2], x);

      // IN-VECTOR RUN DETECTION for halfblock mode
      uint8x16_t fg_shifted = vextq_u8(vdupq_n_u8((uint8_t)cur_fg), fg_idx_v, 15);
      uint8x16_t bg_shifted = vextq_u8(vdupq_n_u8((uint8_t)cur_bg), bg_idx_v, 15);

      uint8x16_t fg_same = vceqq_u8(fg_idx_v, fg_shifted);
      uint8x16_t bg_same = vceqq_u8(bg_idx_v, bg_shifted);
      uint8x16_t both_same = vandq_u8(fg_same, bg_same);
      uint8x16_t boundary_mask = vmvnq_u8(both_same); // 0xFF = boundary

      uint8_t boundaries[16], fg_idx[16], bg_idx[16];
      vst1q_u8(boundaries, boundary_mask);
      vst1q_u8(fg_idx, fg_idx_v);
      vst1q_u8(bg_idx, bg_idx_v);

      int i = 0;
      while (i < 16) {
        int fgc = fg_idx[i];
        int bgc = bg_idx[i];
        int j = i + 1;

        // Skip to next boundary using SIMD-detected boundaries
        while (j < 16 && boundaries[j] == 0)
          ++j;

        if (fgc != cur_fg || bgc != cur_bg) {
          emit_sgr_256(out, fgc, bgc);
          cur_fg = fgc;
          cur_bg = bgc;
        }
        for (int k = i; k < j; ++k)
          str_append_bytes(out, glyph, 3);
        i = j;
      }
      x += 16;
    }
    // tail
    for (; x < W; ++x) {
      const uint8_t *t = rowT + (size_t)x * 3u;
      const uint8_t *b = rowB + (size_t)x * 3u;
      int r6 = (t[0] * 5 + 128) >> 8, g6 = (t[1] * 5 + 128) >> 8, b6 = (t[2] * 5 + 128) >> 8;
      int trmax = t[0] > t[1] ? (t[0] > t[2] ? t[0] : t[2]) : (t[1] > t[2] ? t[1] : t[2]);
      int trmin = t[0] < t[1] ? (t[0] < t[2] ? t[0] : t[2]) : (t[1] < t[2] ? t[1] : t[2]);
      int ytop = (77 * t[0] + 150 * t[1] + 29 * t[2]) >> 8;
      int fg = (trmax - trmin < CUBE_GRAY_THRESHOLD) ? (232 + ((ytop * 23 + 128) >> 8)) : (16 + r6 * 36 + g6 * 6 + b6);

      r6 = (b[0] * 5 + 128) >> 8;
      g6 = (b[1] * 5 + 128) >> 8;
      b6 = (b[2] * 5 + 128) >> 8;
      int brmax = b[0] > b[1] ? (b[0] > b[2] ? b[0] : b[2]) : (b[1] > b[2] ? b[1] : b[2]);
      int brmin = b[0] < b[1] ? (b[0] < b[2] ? b[0] : b[2]) : (b[1] < b[2] ? b[1] : b[2]);
      int ybot = (77 * b[0] + 150 * b[1] + 29 * b[2]) >> 8;
      int bg = (brmax - brmin < CUBE_GRAY_THRESHOLD) ? (232 + ((ybot * 23 + 128) >> 8)) : (16 + r6 * 36 + g6 * 6 + b6);

      if (fg != cur_fg || bg != cur_bg) {
        emit_sgr_256(out, fg, bg);
        cur_fg = fg;
        cur_bg = bg;
      }
      str_append_bytes(out, glyph, 3);
    }
    emit_reset(out);
    str_append_c(out, '\n');
  }
}

// Load 16 RGBx pixels, deinterleave into R,G,B (ignore X)
static inline void load16_rgbx_deinterleave(const uint8_t *pRGBA, uint8x16_t *R, uint8x16_t *G, uint8x16_t *B) {
  // vld4q_u8 expects RGBA RGBA …
  uint8x16x4_t rgba = vld4q_u8(pRGBA);
  *R = rgba.val[0];
  *G = rgba.val[1];
  *B = rgba.val[2];
}

// Complete SIMD version: convert 16 RGB pixels to ANSI 256-color indices with gray vs cube comparison
void rgb_to_ansi256_neon(const rgb_pixel_t *pixels, uint8_t *indices) {
  // Check if we have 3-byte RGB or 4-byte RGBx pixels
  uint8x16_t r, g, b;
  if (sizeof(rgb_pixel_t) == 3) {
    // Load 16 RGB pixels (48 bytes) as 3 separate vectors
    uint8x16x3_t rgb = vld3q_u8((const uint8_t *)pixels);
    r = rgb.val[0];
    g = rgb.val[1];
    b = rgb.val[2];
  } else {
    // Load 16 RGBx pixels using the proper 4-byte loader
    load16_rgbx_deinterleave((const uint8_t *)pixels, &r, &g, &b);
  }

  // Calculate grayscale: gray = (r+g+b)/3 (rounded)
  uint8x16_t gray = vrhaddq_u8(vrhaddq_u8(r, g), b);

  // Quantize each channel to 0-5 using proper ANSI formula
  uint8x16_t r6 = quant6_neon(r);
  uint8x16_t g6 = quant6_neon(g);
  uint8x16_t b6 = quant6_neon(b);

  // Convert cube coords back to 0..255: cr*255/5 = cr*51
  uint16x8_t rl = vmovl_u8(vget_low_u8(r6));
  uint16x8_t rh = vmovl_u8(vget_high_u8(r6));
  uint16x8_t gl = vmovl_u8(vget_low_u8(g6));
  uint16x8_t gh = vmovl_u8(vget_high_u8(g6));
  uint16x8_t bl = vmovl_u8(vget_low_u8(b6));
  uint16x8_t bh = vmovl_u8(vget_high_u8(b6));

  uint16x8_t cube_rl = vmulq_n_u16(rl, 51);
  uint16x8_t cube_rh = vmulq_n_u16(rh, 51);
  uint16x8_t cube_gl = vmulq_n_u16(gl, 51);
  uint16x8_t cube_gh = vmulq_n_u16(gh, 51);
  uint16x8_t cube_bl = vmulq_n_u16(bl, 51);
  uint16x8_t cube_bh = vmulq_n_u16(bh, 51);

  uint8x16_t cube_r = vcombine_u8(vqmovn_u16(cube_rl), vqmovn_u16(cube_rh));
  uint8x16_t cube_g = vcombine_u8(vqmovn_u16(cube_gl), vqmovn_u16(cube_gh));
  uint8x16_t cube_b = vcombine_u8(vqmovn_u16(cube_bl), vqmovn_u16(cube_bh));

  // Calculate cube distance: |r - cube_r| + |g - cube_g| + |b - cube_b|
  uint16x8_t cube_dist_l =
      vaddq_u16(vaddq_u16(vabdl_u8(vget_low_u8(r), vget_low_u8(cube_r)), vabdl_u8(vget_low_u8(g), vget_low_u8(cube_g))),
                vabdl_u8(vget_low_u8(b), vget_low_u8(cube_b)));
  uint16x8_t cube_dist_h = vaddq_u16(
      vaddq_u16(vabdl_u8(vget_high_u8(r), vget_high_u8(cube_r)), vabdl_u8(vget_high_u8(g), vget_high_u8(cube_g))),
      vabdl_u8(vget_high_u8(b), vget_high_u8(cube_b)));

  // Calculate gray index: 232 + round(gray*23/255)
  uint16x8_t gray_l = vmovl_u8(vget_low_u8(gray));
  uint16x8_t gray_h = vmovl_u8(vget_high_u8(gray));
  uint16x8_t gray_t_l = vaddq_u16(vmulq_n_u16(gray_l, 23), vdupq_n_u16(127));
  uint16x8_t gray_t_h = vaddq_u16(vmulq_n_u16(gray_h, 23), vdupq_n_u16(127));

  uint32x4_t gray_t_l0 = vmull_n_u16(vget_low_u16(gray_t_l), 257);
  uint32x4_t gray_t_l1 = vmull_n_u16(vget_high_u16(gray_t_l), 257);
  uint32x4_t gray_t_h0 = vmull_n_u16(vget_low_u16(gray_t_h), 257);
  uint32x4_t gray_t_h1 = vmull_n_u16(vget_high_u16(gray_t_h), 257);

  uint16x8_t gray_idx_l =
      vaddq_u16(vcombine_u16(vshrn_n_u32(gray_t_l0, 16), vshrn_n_u32(gray_t_l1, 16)), vdupq_n_u16(232));
  uint16x8_t gray_idx_h =
      vaddq_u16(vcombine_u16(vshrn_n_u32(gray_t_h0, 16), vshrn_n_u32(gray_t_h1, 16)), vdupq_n_u16(232));

  // Calculate gray level: 8 + (idx-232)*10
  uint16x8_t gray_level_l = vaddq_u16(vmulq_n_u16(vsubq_u16(gray_idx_l, vdupq_n_u16(232)), 10), vdupq_n_u16(8));
  uint16x8_t gray_level_h = vaddq_u16(vmulq_n_u16(vsubq_u16(gray_idx_h, vdupq_n_u16(232)), 10), vdupq_n_u16(8));

  // Calculate gray distance: |gray - gray_level|
  uint16x8_t gray_dist_l = vabdq_u16(gray_l, gray_level_l);
  uint16x8_t gray_dist_h = vabdq_u16(gray_h, gray_level_h);

  // Choose gray if gray_dist < cube_dist
  uint8x16_t cube_idx = vaddq_u8(cube216_index_neon(r6, g6, b6), vdupq_n_u8(16));
  uint8x16_t gray_idx_u8 = vcombine_u8(vqmovn_u16(gray_idx_l), vqmovn_u16(gray_idx_h));

  uint16x8_t use_gray_l = vcltq_u16(gray_dist_l, cube_dist_l);
  uint16x8_t use_gray_h = vcltq_u16(gray_dist_h, cube_dist_h);
  uint8x16_t use_gray_mask = vcombine_u8(vmovn_u16(use_gray_l), vmovn_u16(use_gray_h));

  uint8x16_t final_idx = vbslq_u8(use_gray_mask, gray_idx_u8, cube_idx);

  // Store the result
  vst1q_u8(indices, final_idx);
}

// New streaming row renderer - no heap allocation, direct output to dst
// NEON helper: Process remaining pixels (< 16) efficiently for scalar fallback
void process_remaining_pixels_neon(const rgb_pixel_t *pixels, int count, uint8_t *luminance, char *glyphs) {
  if (count <= 0)
    return;

  // Handle remaining pixels with scalar (NEON lane ops require compile-time constants)
  static const char palette[] = "   ...',;:clodxkO0KXNWM";
  for (int i = 0; i < count; i++) {
    int y = (77 * pixels[i].r + 150 * pixels[i].g + 29 * pixels[i].b) >> 8;
    if (y > 255)
      y = 255;
    luminance[i] = y;
    glyphs[i] = palette[y * 20 / 255];
  }

  const uint8_t ascii_table[32] = {
      ' ', ' ', ' ', '.', '.', '.', '\'', ' ', // 0-7
      ',', ';', ':', 'c', 'l', 'o', 'd',  'x', // 8-15
      'k', 'O', '0', 'K', 'X', 'N', 'W',  'M', // 16-23
      'M', 'M', 'M', 'M', 'M', 'M', 'M',  'M'  // 24-31 (padding)
  };
}

// palette256_index function is already defined earlier in this file

// NEON REP renderer: 256-color background with ▀ blocks
size_t render_row_neon_256_bg_block_rep(
    const rgb_pixel_t *pixels,
    int width,
    char *dst,
    size_t cap)
{
    uint8_t fg_idx[width];
    uint8_t bg_idx[width];

    init_palette();

    int x = 0;
    // NEON processing (16 pixels at a time)
    while (x + 15 < width) {
        uint8x16x3_t rgb = vld3q_u8((const uint8_t *)(pixels + x));

        // Luminance computation for FG contrast
        uint16x8_t y0 = vmlaq_n_u16(vmlaq_n_u16(
            vmulq_n_u16(vmovl_u8(vget_low_u8(rgb.val[0])), 77),
            vmovl_u8(vget_low_u8(rgb.val[1])), 150),
            vmovl_u8(vget_low_u8(rgb.val[2])), 29);
        uint16x8_t y1 = vmlaq_n_u16(vmlaq_n_u16(
            vmulq_n_u16(vmovl_u8(vget_high_u8(rgb.val[0])), 77),
            vmovl_u8(vget_high_u8(rgb.val[1])), 150),
            vmovl_u8(vget_high_u8(rgb.val[2])), 29);

        uint8x16_t luma = vcombine_u8(vqshrn_n_u16(y0, 8), vqshrn_n_u16(y1, 8));

        // FG: white(15) if dark else black(0)
        uint8x16_t fg = vbslq_u8(vcltq_u8(luma, vdupq_n_u8(127)),
                                 vdupq_n_u8(15), vdupq_n_u8(0));

        // BG: 256-color cube quantization with dithering
        uint8x16_t bg = palette256_index_dithered(rgb.val[0], rgb.val[1], rgb.val[2], x);

        // Store results to arrays
        vst1q_u8(&fg_idx[x], fg);
        vst1q_u8(&bg_idx[x], bg);
        x += 16;
    }

    // Scalar processing for remaining pixels
    for (; x < width; x++) {
        const rgb_pixel_t *px = &pixels[x];
        uint8_t lum = (77 * px->r + 150 * px->g + 29 * px->b) >> 8;
        fg_idx[x] = (lum < 127) ? 15 : 0;
        bg_idx[x] = rgb_to_ansi256(px->r, px->g, px->b);
    }

    // Use REP compression
    return write_row_rep_from_arrays(fg_idx, bg_idx, NULL,
                                     width, dst, cap, true, true);
}

// NEON REP renderer: 256-color foreground with ASCII characters
size_t render_row_neon_256_fg_rep(
    const rgb_pixel_t *pixels,
    int width,
    char *dst,
    size_t cap)
{
    uint8_t fg_idx[width];
    char ascii_chars[width];

    int x = 0;
    // NEON processing (16 pixels at a time)
    while (x + 15 < width) {
        uint8x16x3_t rgb = vld3q_u8((const uint8_t *)(pixels + x));

        // FG: 256-color cube quantization with dithering
        uint8x16_t fg = palette256_index_dithered(rgb.val[0], rgb.val[1], rgb.val[2], x);

        // Luminance for ASCII character lookup
        uint16x8_t y0 = vmlaq_n_u16(vmlaq_n_u16(
            vmulq_n_u16(vmovl_u8(vget_low_u8(rgb.val[0])), 77),
            vmovl_u8(vget_low_u8(rgb.val[1])), 150),
            vmovl_u8(vget_low_u8(rgb.val[2])), 29);
        uint16x8_t y1 = vmlaq_n_u16(vmlaq_n_u16(
            vmulq_n_u16(vmovl_u8(vget_high_u8(rgb.val[0])), 77),
            vmovl_u8(vget_high_u8(rgb.val[1])), 150),
            vmovl_u8(vget_high_u8(rgb.val[2])), 29);

        uint8x16_t luma = vcombine_u8(vqshrn_n_u16(y0, 8), vqshrn_n_u16(y1, 8));

        // ASCII lookup using 16-character palette
        uint8x16_t idx = vshrq_n_u8(luma, 4);  // Map 0-255 to 0-15
        uint8x16_t ascii = vqtbl1q_u8(vld1q_u8((const uint8_t *)ascii_ramp16()), idx);

        // Store results to arrays
        vst1q_u8(&fg_idx[x], fg);
        vst1q_u8((uint8_t *)&ascii_chars[x], ascii);
        x += 16;
    }

    // Scalar processing for remaining pixels
    init_palette();
    for (; x < width; x++) {
        const rgb_pixel_t *px = &pixels[x];
        fg_idx[x] = rgb_to_ansi256(px->r, px->g, px->b);
        uint8_t luma = (77 * px->r + 150 * px->g + 29 * px->b) >> 8;
        ascii_chars[x] = luminance_palette[luma];
    }

    // Use REP compression (FG only, with ASCII chars)
    return write_row_rep_from_arrays(fg_idx, NULL, ascii_chars,
                                     width, dst, cap, false, true);
}

// Helper: write decimal RGB triplet using dec3 cache
static inline size_t write_rgb_triplet(uint8_t value, char *dst) {
  extern struct ascii_color_cache g_ascii_cache; // Import from ascii_simd.c
  const dec3_t *d = &g_ascii_cache.dec3_table[value];
  memcpy(dst, d->s, d->len);
  return d->len;
}

// NEON Truecolor Background Renderer with REP compression - OPTIMIZED
size_t render_row_neon_truecolor_bg_block_rep(const rgb_pixel_t *pixels, int width, char *dst, size_t cap)
{
    init_palette();
    // Use bulk array processing instead of manual pixel-by-pixel
    // Allocate arrays for RGB values (background colors)
    uint8_t *bg_r, *bg_g, *bg_b, *fg_r, *fg_g, *fg_b;
    SAFE_MALLOC(bg_r, (size_t)width * sizeof(uint8_t), uint8_t *);
    SAFE_MALLOC(bg_g, (size_t)width * sizeof(uint8_t), uint8_t *);
    SAFE_MALLOC(bg_b, (size_t)width * sizeof(uint8_t), uint8_t *);
    SAFE_MALLOC(fg_r, (size_t)width * sizeof(uint8_t), uint8_t *);
    SAFE_MALLOC(fg_g, (size_t)width * sizeof(uint8_t), uint8_t *);
    SAFE_MALLOC(fg_b, (size_t)width * sizeof(uint8_t), uint8_t *);

    // Process pixels in NEON chunks and fill arrays
    int i = 0;
    for (; i + 16 <= width; i += 16) {
        // Load 16 RGB pixels at once
        uint8x16x3_t rgb = vld3q_u8((const uint8_t *)(pixels + i));

        // Calculate luminance: Y = (77R + 150G + 29B) >> 8
        uint16x8_t r_lo = vmovl_u8(vget_low_u8(rgb.val[0]));
        uint16x8_t r_hi = vmovl_u8(vget_high_u8(rgb.val[0]));
        uint16x8_t g_lo = vmovl_u8(vget_low_u8(rgb.val[1]));
        uint16x8_t g_hi = vmovl_u8(vget_high_u8(rgb.val[1]));
        uint16x8_t b_lo = vmovl_u8(vget_low_u8(rgb.val[2]));
        uint16x8_t b_hi = vmovl_u8(vget_high_u8(rgb.val[2]));

        uint16x8_t luma_lo = vmlaq_n_u16(vmlaq_n_u16(vmulq_n_u16(r_lo, 77), g_lo, 150), b_lo, 29);
        uint16x8_t luma_hi = vmlaq_n_u16(vmlaq_n_u16(vmulq_n_u16(r_hi, 77), g_hi, 150), b_hi, 29);

        uint8x8_t luma_lo_u8 = vshrn_n_u16(luma_lo, 8);
        uint8x8_t luma_hi_u8 = vshrn_n_u16(luma_hi, 8);
        uint8x16_t luma = vcombine_u8(luma_lo_u8, luma_hi_u8);

        // Determine FG text color based on luminance
        uint8x16_t black_threshold = vdupq_n_u8(BGASCII_LUMA_THRESHOLD);
        uint8x16_t use_black = vcgeq_u8(luma, black_threshold); // 0xFF if luma >= threshold

        // Generate FG colors: white (255) or black (0) based on luminance
        uint8x16_t fg_color = vandq_u8(use_black, vdupq_n_u8(0)); // 0 for black text
        fg_color = vornq_u8(fg_color, use_black); // 255 for white text when use_black is 0

        // Store background colors (pixel RGB values)
        vst1q_u8(bg_r + i, rgb.val[0]);
        vst1q_u8(bg_g + i, rgb.val[1]);
        vst1q_u8(bg_b + i, rgb.val[2]);

        // Store foreground colors (computed text colors)
        vst1q_u8(fg_r + i, fg_color);
        vst1q_u8(fg_g + i, fg_color);
        vst1q_u8(fg_b + i, fg_color);
    }

    // Handle remaining pixels with NEON chunks
    while (i + 16 <= width) {
        // Load 16 RGB pixels at once
        uint8x16x3_t rgb = vld3q_u8((const uint8_t *)(pixels + i));

        // Store RGB arrays directly
        vst1q_u8(&bg_r[i], rgb.val[0]);
        vst1q_u8(&bg_g[i], rgb.val[1]);
        vst1q_u8(&bg_b[i], rgb.val[2]);

        // Calculate luminance: Y = (77R + 150G + 29B) >> 8
        uint16x8_t r_lo = vmovl_u8(vget_low_u8(rgb.val[0]));
        uint16x8_t r_hi = vmovl_u8(vget_high_u8(rgb.val[0]));
        uint16x8_t g_lo = vmovl_u8(vget_low_u8(rgb.val[1]));
        uint16x8_t g_hi = vmovl_u8(vget_high_u8(rgb.val[1]));
        uint16x8_t b_lo = vmovl_u8(vget_low_u8(rgb.val[2]));
        uint16x8_t b_hi = vmovl_u8(vget_high_u8(rgb.val[2]));

        uint16x8_t luma_lo = vmlaq_n_u16(vmlaq_n_u16(vmulq_n_u16(r_lo, LUMA_RED), g_lo, LUMA_GREEN), b_lo, LUMA_BLUE);
        uint16x8_t luma_hi = vmlaq_n_u16(vmlaq_n_u16(vmulq_n_u16(r_hi, LUMA_RED), g_hi, LUMA_GREEN), b_hi, LUMA_BLUE);

        uint8x16_t luma = vcombine_u8(vshrn_n_u16(luma_lo, 8), vshrn_n_u16(luma_hi, 8));

        // Threshold comparison: luma >= BGASCII_LUMA_THRESHOLD ? 0 : 255
        uint8x16_t use_black = vcgeq_u8(luma, vdupq_n_u8(BGASCII_LUMA_THRESHOLD));
        uint8x16_t text_color = vbicq_u8(vdupq_n_u8(255), use_black); // 0 if use_black, 255 otherwise

        // Store FG text colors
        vst1q_u8(&fg_r[i], text_color);
        vst1q_u8(&fg_g[i], text_color);
        vst1q_u8(&fg_b[i], text_color);

        i += 16;
    }

    // Handle final few pixels (< 16) with scalar processing if needed
    for (; i < width; i++) {
        const rgb_pixel_t *px = &pixels[i];
        bg_r[i] = px->r;
        bg_g[i] = px->g;
        bg_b[i] = px->b;

        uint8_t luma = (uint8_t)((LUMA_RED * px->r + LUMA_GREEN * px->g + LUMA_BLUE * px->b) >> 8);
        bool use_black_text = (luma >= BGASCII_LUMA_THRESHOLD);
        uint8_t text_color = use_black_text ? 0 : 255;

        fg_r[i] = text_color;
        fg_g[i] = text_color;
        fg_b[i] = text_color;
    }

    // Use unified REP compression with truecolor RGB arrays
    size_t result = write_row_rep_from_arrays_enhanced(fg_r, fg_g, fg_b,
                                                      bg_r, bg_g, bg_b,
                                                      NULL, NULL, NULL,
                                                      width, dst, cap,
                                                      true, false, true);

    SAFE_FREE(bg_r);
    SAFE_FREE(bg_g);
    SAFE_FREE(bg_b);
    SAFE_FREE(fg_r);
    SAFE_FREE(fg_g);
    SAFE_FREE(fg_b);

    return result;
}

// NEON Truecolor Foreground Renderer with REP compression - OPTIMIZED
size_t render_row_neon_truecolor_fg_rep(const rgb_pixel_t *pixels, int width, char *dst, size_t cap)
{
    init_palette();

    // Use bulk array processing instead of manual pixel-by-pixel
    // Allocate arrays for RGB and ASCII values
    uint8_t *fg_r, *fg_g, *fg_b;
    char *ascii_chars;
    SAFE_MALLOC(fg_r, (size_t)width * sizeof(uint8_t), uint8_t *);
    SAFE_MALLOC(fg_g, (size_t)width * sizeof(uint8_t), uint8_t *);
    SAFE_MALLOC(fg_b, (size_t)width * sizeof(uint8_t), uint8_t *);
    SAFE_MALLOC(ascii_chars, (size_t)width * sizeof(char), char *);

    // Process pixels in NEON chunks and fill arrays
    int i = 0;
    for (; i + 16 <= width; i += 16) {
        // Load 16 RGB pixels at once
        uint8x16x3_t rgb = vld3q_u8((const uint8_t *)(pixels + i));

        // Calculate luminance: Y = (77R + 150G + 29B) >> 8
        uint16x8_t r_lo = vmovl_u8(vget_low_u8(rgb.val[0]));
        uint16x8_t r_hi = vmovl_u8(vget_high_u8(rgb.val[0]));
        uint16x8_t g_lo = vmovl_u8(vget_low_u8(rgb.val[1]));
        uint16x8_t g_hi = vmovl_u8(vget_high_u8(rgb.val[1]));
        uint16x8_t b_lo = vmovl_u8(vget_low_u8(rgb.val[2]));
        uint16x8_t b_hi = vmovl_u8(vget_high_u8(rgb.val[2]));

        uint16x8_t luma_lo = vmlaq_n_u16(vmlaq_n_u16(vmulq_n_u16(r_lo, 77), g_lo, 150), b_lo, 29);
        uint16x8_t luma_hi = vmlaq_n_u16(vmlaq_n_u16(vmulq_n_u16(r_hi, 77), g_hi, 150), b_hi, 29);

        uint8x8_t luma_lo_u8 = vshrn_n_u16(luma_lo, 8);
        uint8x8_t luma_hi_u8 = vshrn_n_u16(luma_hi, 8);
        uint8x16_t luma = vcombine_u8(luma_lo_u8, luma_hi_u8);

        // Store pixel colors
        vst1q_u8(fg_r + i, rgb.val[0]);
        vst1q_u8(fg_g + i, rgb.val[1]);
        vst1q_u8(fg_b + i, rgb.val[2]);

        // Convert luminance to ASCII characters using vectorized lookup
        uint8_t luma_vals[16];
        vst1q_u8(luma_vals, luma);
        for (int j = 0; j < 16; j++) {
            ascii_chars[i + j] = luminance_palette[luma_vals[j]];
        }
    }

    // Handle remaining pixels with NEON chunks
    while (i + 16 <= width) {
        // Load 16 RGB pixels at once
        uint8x16x3_t rgb = vld3q_u8((const uint8_t *)(pixels + i));

        // Store RGB arrays directly
        vst1q_u8(&fg_r[i], rgb.val[0]);
        vst1q_u8(&fg_g[i], rgb.val[1]);
        vst1q_u8(&fg_b[i], rgb.val[2]);

        // Calculate luminance: Y = (77R + 150G + 29B) >> 8
        uint16x8_t r_lo = vmovl_u8(vget_low_u8(rgb.val[0]));
        uint16x8_t r_hi = vmovl_u8(vget_high_u8(rgb.val[0]));
        uint16x8_t g_lo = vmovl_u8(vget_low_u8(rgb.val[1]));
        uint16x8_t g_hi = vmovl_u8(vget_high_u8(rgb.val[1]));
        uint16x8_t b_lo = vmovl_u8(vget_low_u8(rgb.val[2]));
        uint16x8_t b_hi = vmovl_u8(vget_high_u8(rgb.val[2]));

        uint16x8_t luma_lo = vmlaq_n_u16(vmlaq_n_u16(vmulq_n_u16(r_lo, LUMA_RED), g_lo, LUMA_GREEN), b_lo, LUMA_BLUE);
        uint16x8_t luma_hi = vmlaq_n_u16(vmlaq_n_u16(vmulq_n_u16(r_hi, LUMA_RED), g_hi, LUMA_GREEN), b_hi, LUMA_BLUE);

        uint8x16_t luma = vcombine_u8(vshrn_n_u16(luma_lo, 8), vshrn_n_u16(luma_hi, 8));

        // Store luminance values temporarily and convert to ASCII characters
        uint8_t luma_values[16];
        vst1q_u8(luma_values, luma);
        for (int j = 0; j < 16 && i + j < width; j++) {
            ascii_chars[i + j] = luminance_palette[luma_values[j]];
        }

        i += 16;
    }

    // Handle final few pixels (< 16) with scalar processing if needed
    for (; i < width; i++) {
        const rgb_pixel_t *px = &pixels[i];
        fg_r[i] = px->r;
        fg_g[i] = px->g;
        fg_b[i] = px->b;

        uint8_t luma = (uint8_t)((LUMA_RED * px->r + LUMA_GREEN * px->g + LUMA_BLUE * px->b) >> 8);
        ascii_chars[i] = luminance_palette[luma];
    }

    // Use unified REP compression with truecolor RGB arrays and ASCII chars
    size_t result = write_row_rep_from_arrays_enhanced(fg_r, fg_g, fg_b,
                                                      NULL, NULL, NULL,
                                                      NULL, NULL, ascii_chars,
                                                      width, dst, cap,
                                                      false, false, true);

    SAFE_FREE(fg_r);
    SAFE_FREE(fg_g);
    SAFE_FREE(fg_b);
    SAFE_FREE(ascii_chars);

    return result;
}

// Forward declarations for scalar unified REP functions (implemented in ascii_simd_color.c)
extern size_t render_row_256color_background_rep_unified(const rgb_pixel_t *row, int width, char *dst, size_t cap);
extern size_t render_row_truecolor_background_rep_unified(const rgb_pixel_t *row, int width, char *dst, size_t cap);
extern size_t render_row_256color_foreground_rep_unified(const rgb_pixel_t *row, int width, char *dst, size_t cap);
extern size_t render_row_truecolor_foreground_rep_unified(const rgb_pixel_t *row, int width, char *dst, size_t cap);

// Unified NEON + scalar REP dispatcher
size_t render_row_ascii_rep_dispatch_neon(
    const rgb_pixel_t *row,
    int width,
    char *dst,
    size_t cap,
    bool background_mode,
    bool use_fast_path)
{
#ifdef SIMD_SUPPORT_NEON
    // Use NEON SIMD REP versions when available
    if (background_mode) {
        if (use_fast_path) {
            // 256-color background (▀)
            return render_row_neon_256_bg_block_rep(row, width, dst, cap);
        } else {
            // Truecolor background (▀)
            return render_row_neon_truecolor_bg_block_rep(row, width, dst, cap);
        }
    } else {
        if (use_fast_path) {
            // 256-color foreground ASCII
            return render_row_neon_256_fg_rep(row, width, dst, cap);
        } else {
            // Truecolor foreground ASCII
            return render_row_neon_truecolor_fg_rep(row, width, dst, cap);
        }
    }
#endif

    // Scalar fallback (scalar REP code)
    if (background_mode) {
        if (use_fast_path) {
            return render_row_256color_background_rep_unified(row, width, dst, cap);
        } else {
            return render_row_truecolor_background_rep_unified(row, width, dst, cap);
        }
    } else {
        if (use_fast_path) {
            return render_row_256color_foreground_rep_unified(row, width, dst, cap);
        } else {
            return render_row_truecolor_foreground_rep_unified(row, width, dst, cap);
        }
    }
}

#endif
