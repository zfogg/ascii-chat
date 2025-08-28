#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>
#include <math.h>
#include "neon.h"
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

#ifdef SIMD_SUPPORT_NEON // main block of code ifdef

// Definitions are in ascii_simd.h - just use them
// REMOVED: #define luminance_palette g_ascii_cache.luminance_palette (causes macro expansion issues)

// Forward declarations
static inline size_t write_rgb_triplet(uint8_t value, char *dst);
static inline size_t write_decimal(int value, char *dst);

// Thread-local reusable scratch buffers to avoid per-row malloc/free
static __thread uint8_t *tls_u8_a = NULL;
static __thread uint8_t *tls_u8_b = NULL;
static __thread uint8_t *tls_u8_c = NULL;
static __thread uint8_t *tls_u8_d = NULL;
static __thread uint8_t *tls_u8_e = NULL;
static __thread uint8_t *tls_u8_f = NULL;
static __thread size_t tls_cap = 0;

static inline void ensure_tls_cap(size_t need) {
  if (need <= tls_cap)
    return;
  size_t ncap = tls_cap ? tls_cap : 1024;
  while (ncap < need)
    ncap = (ncap * 3) / 2 + 64;
  uint8_t *na = (uint8_t *)realloc(tls_u8_a, ncap);
  uint8_t *nb = (uint8_t *)realloc(tls_u8_b, ncap);
  uint8_t *nc = (uint8_t *)realloc(tls_u8_c, ncap);
  uint8_t *nd = (uint8_t *)realloc(tls_u8_d, ncap);
  uint8_t *ne = (uint8_t *)realloc(tls_u8_e, ncap);
  uint8_t *nf = (uint8_t *)realloc(tls_u8_f, ncap);
  if (!na || !nb || !nc || !nd || !ne || !nf) {
    log_error("OOM in ensure_tls_cap");
    exit(1);
  }
  tls_u8_a = na;
  tls_u8_b = nb;
  tls_u8_c = nc;
  tls_u8_d = nd;
  tls_u8_e = ne;
  tls_u8_f = nf;
  tls_cap = ncap;
}

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
    log_error("OOM");
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
    log_error("OOM");
    exit(1);
  }
  va_start(ap, fmt);
  vsnprintf(heap, (size_t)n + 1, fmt, ap);
  va_end(ap);
  str_append_bytes(s, heap, (size_t)n);
  free(heap);
}

// Allocate a new image (RGB8), abort on OOM
ImageRGB alloc_image(int w, int h) {
  ImageRGB out;
  out.w = w;
  out.h = h;
  size_t n = (size_t)w * (size_t)h * 3u;
  out.pixels = (uint8_t *)malloc(n);
  if (!out.pixels) {
    log_error("OOM");
    exit(1);
  }
  return out;
}

// ===== add near top (config) =====
#ifndef BGASCII_LUMA_THRESHOLD
#define BGASCII_LUMA_THRESHOLD 128 // Y >= 128 -> black text; else white text
#endif

// ------------------------------------------------------------
// Map luminance [0..255] → 4-bit index [0..15] using top nibble
static inline uint8x16_t luma_to_idx_nibble(uint8x16_t y) {
  return vshrq_n_u8(y, 4);
}

// Emit ANSI SGR for truecolor FG/BG - use fast manual builder
static inline void emit_sgr(Str *out, int fr, int fg, int fb, int br, int bg, int bb) {
  // Fast truecolor FG+BG SGR without sprintf
  // Builds: "\x1b[38;2;FR;FG;FB;48;2;BR;BG;BBm"
  str_reserve(out, out->len + 40);
  char *p = out->data + out->len;

  // FG prefix
  memcpy(p, "\x1b[38;2;", 7);
  p += 7;
  p += write_rgb_triplet((uint8_t)fr, p);
  *p++ = ';';
  p += write_rgb_triplet((uint8_t)fg, p);
  *p++ = ';';
  p += write_rgb_triplet((uint8_t)fb, p);

  // BG prefix
  *p++ = ';';
  *p++ = '4';
  *p++ = '8';
  *p++ = ';';
  *p++ = '2';
  *p++ = ';';
  p += write_rgb_triplet((uint8_t)br, p);
  *p++ = ';';
  p += write_rgb_triplet((uint8_t)bg, p);
  *p++ = ';';
  p += write_rgb_triplet((uint8_t)bb, p);
  *p++ = 'm';

  out->len = (size_t)(p - out->data);
}

// Emit reset SGR
static inline void emit_reset(Str *out) {
  str_append_bytes(out, "\x1b[0m", 4);
}

// Fast 256-color SGR builders
static inline char *append_sgr256_fg(char *dst, uint8_t fg) {
  uint8_t len;
  char *sgr_str = get_sgr256_fg_string(fg, &len);
  memcpy(dst, sgr_str, len);
  return dst + len;
}

// Local fast SGR builder for truecolor FG+BG that writes into a char* buffer
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
// Unified buffer safety macro - ensures adequate space before writing escape sequences
#define ENSURE_SPACE(ptr, end, needed)                                                                                 \
  do {                                                                                                                 \
    if ((ptr) + (needed) > (end)) {                                                                                    \
      log_debug("BUFFER GUARD: Insufficient space at %s:%d - need %d bytes, have %ld", __FILE__, __LINE__, (needed),   \
                (long)((end) - (ptr)));                                                                                \
      goto buffer_full;                                                                                                \
    }                                                                                                                  \
  } while (0)

size_t write_row_rep_from_arrays_enhanced(const uint8_t *fg_r, const uint8_t *fg_g,
                                          const uint8_t *fg_b, // Truecolor FG RGB (NULL if using indices)
                                          const uint8_t *bg_r, const uint8_t *bg_g,
                                          const uint8_t *bg_b, // Truecolor BG RGB (NULL if using indices)
                                          const uint8_t *fg_idx,
                                          const uint8_t *bg_idx,   // 256-color indices (NULL if using RGB)
                                          const char *ascii_chars, // ASCII characters for traditional ASCII art
                                          int width, char *dst, size_t cap, bool is_truecolor) {
  // log_debug("CORRUPTION DEBUG: Starting write_row_rep_from_arrays_enhanced - width=%d, cap=%zu, is_truecolor=%d",
  // width, cap, is_truecolor);

  char *p = dst;
  char *row_end = dst + cap - 100; // Conservative safety margin for all escape sequences
  bool have_color = false;
  uint8_t last_fg_r = 0, last_fg_g = 0, last_fg_b = 0;
  uint8_t last_bg_r = 0, last_bg_g = 0, last_bg_b = 0;
  uint8_t last_fg_idx = 0, last_bg_idx = 0;
  char last_char = 0;
  int run_len = 0;

  // log_debug("CORRUPTION DEBUG: Buffer setup - dst=%p, p=%p, row_end=%p, safety_margin=%ld", dst, p, row_end,
  // (long)(row_end - dst));

  for (int x = 0; x < width; x++) {
    char ch = ascii_chars[x];
    bool color_changed = false;

    // Declare color variables outside scope so they can be used later
    uint8_t curr_fg_r = 0, curr_fg_g = 0, curr_fg_b = 0;
    uint8_t curr_bg_r = 0, curr_bg_g = 0, curr_bg_b = 0;
    uint8_t fg = 0, bg = 0;

    if (is_truecolor) {
      // Compare RGB values for truecolor mode - use original values
      curr_fg_r = fg_r ? fg_r[x] : 0;
      curr_fg_g = fg_g ? fg_g[x] : 0;
      curr_fg_b = fg_b ? fg_b[x] : 0;
      curr_bg_r = bg_r ? bg_r[x] : 0;
      curr_bg_g = bg_g ? bg_g[x] : 0;
      curr_bg_b = bg_b ? bg_b[x] : 0;

      color_changed = (!have_color || curr_fg_r != last_fg_r || curr_fg_g != last_fg_g || curr_fg_b != last_fg_b ||
                       (bg_r && (curr_bg_r != last_bg_r || curr_bg_g != last_bg_g || curr_bg_b != last_bg_b)));
    } else {
      // Compare indices for 256-color mode
      fg = fg_idx[x];
      bg = bg_idx ? bg_idx[x] : 0;
      color_changed = (!have_color || fg != last_fg_idx || (bg_idx && bg != last_bg_idx));
    }

    if (color_changed) {
      // CRITICAL FIX: Check buffer space BEFORE generating escape sequences to prevent corruption
      // Truecolor sequences can be up to ~30 bytes, 256-color ~15 bytes, REP sequences ~7 bytes
      // log_debug("CORRUPTION DEBUG: Color changed at x=%d, ch='%c', buffer_remaining=%ld", x, ch, (long)(row_end -
      // p));

      if (p >= row_end) {
        // log_debug("CORRUPTION DEBUG: EMERGENCY BREAK - insufficient buffer space, remaining: %ld", (long)(row_end -
        // p));
        break; // Not enough space for complete escape sequence - stop here
      }

      // Flush any pending run BEFORE emitting color sequence with REP compression
      if (run_len > 0) {
        if (run_len == 1) {
          // Single character - just emit it
          *p++ = last_char;
        } else {
          // Multiple characters - use REP sequence: char + \033[<count-1>b
          *p++ = last_char;
          ENSURE_SPACE(p, row_end, 10); // Space for REP sequence
          memcpy(p, "\033[", 2);
          p += 2;
          p += write_decimal(run_len - 1, p); // REP repeats the previous char
          *p++ = 'b';
        }
        run_len = 0;
      }

      // NOW emit new color sequence (AFTER flushing previous run to avoid breaking REP context)
      if (is_truecolor) {
        // Truecolor mode: use quantized RGB values (same ones used for comparison)
        uint8_t fg_r_val = curr_fg_r, fg_g_val = curr_fg_g, fg_b_val = curr_fg_b;
        uint8_t bg_r_val = curr_bg_r, bg_g_val = curr_bg_g, bg_b_val = curr_bg_b;

        if (bg_r) {
          // Background mode: FG+BG truecolor (up to 30 bytes)
          ENSURE_SPACE(p, row_end, 30);
          memcpy(p, "\033[38;2;", 7);
          p += 7;
          p += write_rgb_triplet(fg_r_val, p);
          *p++ = ';';
          p += write_rgb_triplet(fg_g_val, p);
          *p++ = ';';
          p += write_rgb_triplet(fg_b_val, p);
          memcpy(p, ";48;2;", 6);
          p += 6;
          p += write_rgb_triplet(bg_r_val, p);
          *p++ = ';';
          p += write_rgb_triplet(bg_g_val, p);
          *p++ = ';';
          p += write_rgb_triplet(bg_b_val, p);
          *p++ = 'm';
        } else {
          // Foreground mode: FG truecolor only (up to 20 bytes)
          ENSURE_SPACE(p, row_end, 20);

          memcpy(p, "\033[38;2;", 7);
          p += 7;
          p += write_rgb_triplet(fg_r_val, p);
          *p++ = ';';
          p += write_rgb_triplet(fg_g_val, p);
          *p++ = ';';
          p += write_rgb_triplet(fg_b_val, p);
          *p++ = 'm';
        }

        last_fg_r = fg_r_val;
        last_fg_g = fg_g_val;
        last_fg_b = fg_b_val;
        last_bg_r = bg_r_val;
        last_bg_g = bg_g_val;
        last_bg_b = bg_b_val;
      } else {
        // 256-color mode: use existing logic
        fg = fg_idx[x];
        bg = bg_idx ? bg_idx[x] : 0;

        if (bg_idx) {
          // 256-color FG+BG (up to 15 bytes)
          ENSURE_SPACE(p, row_end, 15);
          p = append_sgr256_fg_bg(p, fg, bg);
        } else {
          // 256-color FG only (up to 10 bytes)
          ENSURE_SPACE(p, row_end, 10);
          p = append_sgr256_fg(p, fg);
        }

        last_fg_idx = fg;
        last_bg_idx = bg;
      }

      have_color = true;
      // Emit the current character after the color sequence
      *p++ = ch;
      last_char = ch;
      run_len = 1;

    } else if (ch != last_char) {
      // Character changed (but not color), flush any pending run first
      if (run_len > 1) {
        // Use REP compression for remaining characters
        ENSURE_SPACE(p, row_end, 10); // Space for REP sequence
        memcpy(p, "\033[", 2);
        p += 2;
        p += write_decimal(run_len - 1, p); // REP repeats the previous char
        *p++ = 'b';
      }

      // Now emit the new character
      *p++ = ch;
      last_char = ch;
      run_len = 1;

    } else {
      // Same character, same color - just increment run length
      run_len++;
    }
  }

  // log_debug("CORRUPTION DEBUG: Finished processing %d characters, final buffer pos %ld", width, (long)(p - dst));

  // CRITICAL FIX: Check buffer space BEFORE flushing final run and reset
  // We've already reserved space with our safety margin
  if (p < row_end) {
    // log_debug("CORRUPTION DEBUG: Final flush - run_len=%d, buffer_remaining=%ld", run_len, (long)(row_end - p));

    // Flush final run with REP compression
    if (run_len > 1) {
      // Use REP compression for remaining characters
      ENSURE_SPACE(p, row_end, 10); // Space for REP sequence
      memcpy(p, "\033[", 2);
      p += 2;
      p += write_decimal(run_len - 1, p); // REP repeats the previous char
      *p++ = 'b';
    }

    // Reset sequence (4 bytes) + newline (1 byte)
    ENSURE_SPACE(p, row_end, 5);
    memcpy(p, "\033[0m\n", 5);
    p += 5;
    // log_debug("CORRUPTION DEBUG: Reset sequence added %ld bytes", (long)(p - reset_start));
  } else {
    // log_debug("CORRUPTION DEBUG: EMERGENCY - No space for final run/reset, truncating, remaining: %ld",
    // (long)(row_end - p));
  }

  size_t total_bytes = p - dst;
  // log_debug("CORRUPTION DEBUG: Final output size: %zu bytes", total_bytes);

  // Show a preview of the final output as hex bytes to check for corruption
  if (total_bytes > 0) {
    int preview_len = (total_bytes < 50) ? total_bytes : 50;
    char hex_preview[201]; // 50 bytes * 4 chars per byte (xx ) + null terminator
    char *hex_ptr = hex_preview;

    for (int i = 0; i < preview_len; i++) {
      unsigned char byte = (unsigned char)dst[i];
      if (byte == 0x1b) {
        // Special case for escape character
        strcpy(hex_ptr, "ESC ");
        hex_ptr += 4;
      } else if (byte >= 32 && byte <= 126) {
        // Printable ASCII
        *hex_ptr++ = byte;
        *hex_ptr++ = ' ';
      } else {
        // Non-printable as hex
        sprintf(hex_ptr, "%02x ", byte);
        hex_ptr += 3;
      }
    }
    *hex_ptr = '\0';
    // log_debug("CORRUPTION DEBUG: Final output preview (first %d bytes as hex): %s", preview_len, hex_preview);
  }

  return total_bytes;

buffer_full:
  // Buffer capacity exceeded - return partial result
  log_debug("BUFFER GUARD: Buffer capacity exceeded, returning partial result: %zu bytes", (size_t)(p - dst));
  return (size_t)(p - dst);
}

// Legacy wrapper for existing 256-color code
static size_t write_row_rep_from_arrays(const uint8_t *fg_idx,
                                        const uint8_t *bg_idx,   // NULL if FG-only
                                        const char *ascii_chars, // ASCII characters for traditional ASCII art
                                        int width, char *dst, size_t cap) {
  return write_row_rep_from_arrays_enhanced(NULL, NULL, NULL,        // No truecolor FG RGB
                                            NULL, NULL, NULL,        // No truecolor BG RGB
                                            fg_idx, bg_idx,          // 256-color indices
                                            ascii_chars,             // ASCII characters
                                            width, dst, cap, false); // Not truecolor mode
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
  uint8x16_t lut = vld1q_u8((const uint8_t *)g_ascii_cache.ascii_chars);

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
      str_append_c(out, g_ascii_cache.ascii_chars[idx]);
    }
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
    // TEMPORARY: Disable BG-only escape sequences to prevent corruption
    // str_printf(out, "\x1b[48;5;%dm", bg_idx); // DISABLED - causes corruption
    log_debug("DISABLED: Background-only mode for index %d (preventing corruption)", bg_idx);
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

// Apply ordered dithering to reduce color variations (creates longer runs)
static inline uint8x16_t apply_ordered_dither(uint8x16_t color, int pixel_offset, uint8_t dither_strength) {
  // Speed mode: disable dithering to reduce color variation and CPU
  (void)pixel_offset;
  (void)dither_strength;
  return color;
}

uint8x16_t palette256_index_dithered(uint8x16_t r, uint8x16_t g, uint8x16_t b, int pixel_offset) {
  // Dithering disabled in speed mode (no-op)
  r = apply_ordered_dither(r, pixel_offset, 0);
  g = apply_ordered_dither(g, pixel_offset + 1, 0);
  b = apply_ordered_dither(b, pixel_offset + 2, 0);

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

// Background-colored ASCII: BG = pixel color, FG ∈ {white, black} chosen by luma.
// ANSI RLE: groups spans where (FG,BG) stays constant; glyphs may vary within the run.
void render_ascii_bgcolor(const ImageRGB *img, Str *out) {
  const int W = img->w, H = img->h;
  const uint8_t *p = img->pixels;

  // Glyph ramp LUT (same as grayscale path)
  const uint8x16_t lut = vld1q_u8((const uint8_t *)g_ascii_cache.ascii_chars);
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
      str_append_c(out, g_ascii_cache.ascii_chars[idx]);

      ++x;
    }

    emit_reset(out);
    str_append_c(out, '\n');
  }
}

// ===== new renderer =====
void render_ascii_bgcolor_256(const ImageRGB *img, Str *out) {
  const int W = img->w, H = img->h;
  const uint8x16_t lut = vld1q_u8((const uint8_t *)g_ascii_cache.ascii_chars);
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
      str_append_c(out, g_ascii_cache.ascii_chars[yv >> 4]);
    }
    emit_reset(out);
    str_append_c(out, '\n');
  }
}

// Complete SIMD version: convert 16 RGB pixels to ANSI 256-color indices with gray vs cube comparison
void rgb_to_ansi256_neon(const rgb_pixel_t *pixels, uint8_t *indices) {
  // Check if we have 3-byte RGB or 4-byte RGBx pixels
  uint8x16_t r, g, b;
  // Load 16 RGB pixels (48 bytes) as 3 separate vectors
  uint8x16x3_t rgb = vld3q_u8((const uint8_t *)pixels);
  r = rgb.val[0];
  g = rgb.val[1];
  b = rgb.val[2];

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
  for (int i = 0; i < count; i++) {
    int y = (77 * pixels[i].r + 150 * pixels[i].g + 29 * pixels[i].b) >> 8;
    if (y > 255)
      y = 255;
    luminance[i] = y;
    glyphs[i] = g_ascii_cache.ascii_chars[y];
  }
}

// NEON REP renderer: 256-color foreground with ASCII characters
size_t render_row_neon_256_fg_rep(const rgb_pixel_t *pixels, int width, char *dst, size_t cap) {
  // DEBUG: Log function entry
  log_debug("NEON 256: Processing %d pixels", width);
  if (width >= 3) {
    log_debug("NEON 256: First 3 pixels RGB: (%d,%d,%d) (%d,%d,%d) (%d,%d,%d)", pixels[0].r, pixels[0].g, pixels[0].b,
              pixels[1].r, pixels[1].g, pixels[1].b, pixels[2].r, pixels[2].g, pixels[2].b);
  }

  // CRITICAL FIX: Avoid VLA stack overflow for large widths - use thread-local buffers instead
  ensure_tls_cap((size_t)width);
  uint8_t *fg_idx = tls_u8_a;           // Thread-safe, no malloc/free overhead
  char *ascii_chars = (char *)tls_u8_b; // Reuse second buffer for character data

  // CRITICAL FIX: Add buffer bounds checking to prevent corruption
  if (cap < 16) { // Minimum space needed for any output
    log_error("NEON 256: Insufficient buffer capacity: %zu bytes (need at least 16)", cap);
    return 0;
  }

  int x = 0;
  // NEON processing (16 pixels at a time)
  while (x + 15 < width) {
    uint8x16x3_t rgb = vld3q_u8((const uint8_t *)(pixels + x));

    // FG: 256-color cube quantization with dithering
    uint8x16_t fg = palette256_index_dithered(rgb.val[0], rgb.val[1], rgb.val[2], x);

    // Luminance for ASCII character lookup
    uint16x8_t y0 = vmlaq_n_u16(
        vmlaq_n_u16(vmulq_n_u16(vmovl_u8(vget_low_u8(rgb.val[0])), 77), vmovl_u8(vget_low_u8(rgb.val[1])), 150),
        vmovl_u8(vget_low_u8(rgb.val[2])), 29);
    uint16x8_t y1 = vmlaq_n_u16(
        vmlaq_n_u16(vmulq_n_u16(vmovl_u8(vget_high_u8(rgb.val[0])), 77), vmovl_u8(vget_high_u8(rgb.val[1])), 150),
        vmovl_u8(vget_high_u8(rgb.val[2])), 29);

    uint8x16_t luma = vcombine_u8(vqshrn_n_u16(y0, 8), vqshrn_n_u16(y1, 8));

    // Store FG indices first
    vst1q_u8(&fg_idx[x], fg);

    // ASCII lookup must be scalar - luminance_palette is 256 bytes, can't fit in NEON vtbl
    uint8_t luma_array[16];
    vst1q_u8(luma_array, luma);

    // CRITICAL FIX: Add bounds checking to prevent corruption
    int safe_end = (x + 16 > width) ? width : x + 16;
    for (int i = 0; i < 16 && (x + i) < safe_end; i++) {
      // Thread-safe read: g_ascii_cache is read-only after initialization
      ascii_chars[x + i] = g_ascii_cache.luminance_palette[luma_array[i]];
    }
    x += 16;
  }

  // Scalar processing for remaining pixels
  for (; x < width; x++) {
    const rgb_pixel_t *px = &pixels[x];
    fg_idx[x] = rgb_to_ansi256(px->r, px->g, px->b);
    uint8_t luma = (77 * px->r + 150 * px->g + 29 * px->b) >> 8;
    ascii_chars[x] = g_ascii_cache.luminance_palette[luma]; // Use same method as truecolor
  }

  // CRITICAL FIX: Add final bounds checking before writing output
  // Conservative estimate: 256-color codes are max ~14 chars per pixel + REP overhead
  size_t estimated_output = (size_t)width * 14 + 32; // 32 bytes safety margin
  if (estimated_output > cap) {
    log_error("NEON 256: Insufficient output buffer: need ~%zu, have %zu for width %d", estimated_output, cap, width);
    return 0;
  }

  // DEBUG: Log final ASCII characters and 256-color indices before compression
  if (width >= 10) {
    log_debug("NEON 256: Final ASCII chars: '%.10s'", ascii_chars);
    log_debug("NEON 256: First 4 color indices: %d %d %d %d", fg_idx[0], fg_idx[1], fg_idx[2], fg_idx[3]);
  }

  // Use REP compression (FG only, with ASCII chars)
  size_t result = write_row_rep_from_arrays(fg_idx, NULL, ascii_chars, width, dst, cap);

  // DEBUG: Log output result
  log_debug("NEON 256: Generated %zu bytes of output", result);

  // CRITICAL FIX: Validate output size doesn't exceed capacity
  if (result > cap) {
    log_error("NEON 256: Output corruption detected! Generated %zu bytes in %zu capacity buffer", result, cap);
    return 0;
  }

  return result;
}

// Helper: write decimal RGB triplet using dec3 cache
static inline size_t write_rgb_triplet(uint8_t value, char *dst) {
  const dec3_t *d = &g_ascii_cache.dec3_table[value];
  memcpy(dst, d->s, d->len);
  return d->len;
}

// Simple decimal writer for REP counts (can be larger than 255)
static inline size_t write_decimal(int value, char *dst) {
  if (value == 0) {
    *dst = '0';
    return 1;
  }

  char temp[10]; // Enough for 32-bit int
  int pos = 0;
  int v = value;

  while (v > 0) {
    temp[pos++] = '0' + (v % 10);
    v /= 10;
  }

  // Reverse digits into dst
  for (int i = 0; i < pos; i++) {
    dst[i] = temp[pos - 1 - i];
  }

  return pos;
}

// NEON Truecolor Foreground Renderer - SIMPLIFIED APPROACH (NO RLE)
// Matches scalar performance by using NEON only for luma conversion,
// then simple per-pixel output like the fast scalar version
size_t render_row_neon_truecolor_fg_rep(const rgb_pixel_t *pixels, int width, char *dst, size_t cap) {
  char *current_pos = dst;
  char *buffer_end = dst + cap;

  int i = 0;

  // Process 16 pixels at a time with NEON for luma + color generation
  while (i + 16 <= width && current_pos + 1024 < buffer_end) { // Conservative buffer check
    uint8x16x3_t rgb = vld3q_u8((const uint8_t *)(pixels + i));

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

    // Extract RGB and luma values for scalar processing
    uint8_t r_vals[16], g_vals[16], b_vals[16], luma_vals[16];
    vst1q_u8(r_vals, rgb.val[0]);
    vst1q_u8(g_vals, rgb.val[1]);
    vst1q_u8(b_vals, rgb.val[2]);
    vst1q_u8(luma_vals, luma);

    // Process each pixel in the 16-pixel chunk
    for (int j = 0; j < 16; j++) {
      // Get ASCII character from luma
      char ascii_char = g_ascii_cache.luminance_palette[luma_vals[j]];

      // Generate ANSI foreground color sequence: \033[38;2;R;G;Bm
      size_t remaining = buffer_end - current_pos;
      if (remaining < 64)
        break; // Safety check

      // Use optimized color generation
      memcpy(current_pos, "\033[38;2;", 7);
      current_pos += 7;
      current_pos += write_rgb_triplet(r_vals[j], current_pos);
      *current_pos++ = ';';
      current_pos += write_rgb_triplet(g_vals[j], current_pos);
      *current_pos++ = ';';
      current_pos += write_rgb_triplet(b_vals[j], current_pos);
      *current_pos++ = 'm';

      // Write ASCII character
      *current_pos++ = ascii_char;
    }

    i += 16;
  }

  // Handle remaining pixels with scalar processing (matches scalar implementation exactly)
  for (; i < width; i++) {
    const rgb_pixel_t *pixel = &pixels[i];

    // Calculate luminance (scalar)
    int luminance = (LUMA_RED * pixel->r + LUMA_GREEN * pixel->g + LUMA_BLUE * pixel->b) >> 8;
    if (luminance > 255)
      luminance = 255;

    // Get ASCII character - Use global luminance palette
    char ascii_char = g_ascii_cache.luminance_palette[luminance];

    size_t remaining = buffer_end - current_pos;
    if (remaining < 64)
      break; // Safety check

    // Generate ANSI foreground color (matches scalar exactly)
    memcpy(current_pos, "\033[38;2;", 7);
    current_pos += 7;
    current_pos += write_rgb_triplet(pixel->r, current_pos);
    *current_pos++ = ';';
    current_pos += write_rgb_triplet(pixel->g, current_pos);
    *current_pos++ = ';';
    current_pos += write_rgb_triplet(pixel->b, current_pos);
    *current_pos++ = 'm';

    // Write ASCII character
    *current_pos++ = ascii_char;
  }

  // Reset sequence (matches scalar)
  size_t remaining = buffer_end - current_pos;
  if (remaining >= 5) { // \033[0m\n
    memcpy(current_pos, "\033[0m\n", 5);
    current_pos += 5;
  }

  return current_pos - dst;
}

// Forward declarations for scalar unified REP functions (implemented in ascii_simd_color.c)
extern size_t render_row_256color_background_rep_unified(const rgb_pixel_t *row, int width, char *dst, size_t cap);
extern size_t render_row_truecolor_background_rep_unified(const rgb_pixel_t *row, int width, char *dst, size_t cap);
extern size_t render_row_256color_foreground_rep_unified(const rgb_pixel_t *row, int width, char *dst, size_t cap);
extern size_t render_row_truecolor_foreground_rep_unified(const rgb_pixel_t *row, int width, char *dst, size_t cap);

// Ultra-simple monochrome NEON implementation - just do the arithmetic, skip complex palette
void convert_pixels_neon(const rgb_pixel_t *__restrict pixels, char *__restrict ascii_chars, int count) {
  int i = 0;

  // MAXIMUM PERFORMANCE NEON - Process 32 pixels per iteration with PIPELINE OPTIMIZATION
  // Pre-load next batch while processing current batch to eliminate memory stalls
  uint8x16x3_t rgb_batch1_next, rgb_batch2_next;
  bool prefetch_valid = false;

  for (; i + 31 < count; i += 32) {
    const uint8_t *rgb_data = (const uint8_t *)&pixels[i];

    // Load current batch (or use pre-loaded data from previous iteration)
    uint8x16x3_t rgb_batch1, rgb_batch2;
    if (!prefetch_valid) {
      rgb_batch1 = vld3q_u8(rgb_data);      // Pixels 0-15 (first iteration)
      rgb_batch2 = vld3q_u8(rgb_data + 48); // Pixels 16-31 (first iteration)
    } else {
      rgb_batch1 = rgb_batch1_next; // Use pre-loaded data
      rgb_batch2 = rgb_batch2_next; // Use pre-loaded data
    }

    // PRE-LOAD NEXT ITERATION while processing current (pipeline optimization)
    // This overlaps memory access with computation, eliminating memory stalls
    if (i + 63 < count) {
      const uint8_t *next_rgb_data = (const uint8_t *)&pixels[i + 32];

      // Optional: Add prefetch hint for data beyond next iteration
      if (i + 95 < count) {
        __builtin_prefetch(&pixels[i + 64], 0, 3); // Prefetch 2 iterations ahead
      }

      rgb_batch1_next = vld3q_u8(next_rgb_data);      // Pre-load next batch 1
      rgb_batch2_next = vld3q_u8(next_rgb_data + 48); // Pre-load next batch 2
      prefetch_valid = true;
    } else {
      prefetch_valid = false;
    }

    // Process batch 1 (pixels 0-15)
    uint16x8_t r1_lo = vmovl_u8(vget_low_u8(rgb_batch1.val[0]));
    uint16x8_t r1_hi = vmovl_u8(vget_high_u8(rgb_batch1.val[0]));
    uint16x8_t g1_lo = vmovl_u8(vget_low_u8(rgb_batch1.val[1]));
    uint16x8_t g1_hi = vmovl_u8(vget_high_u8(rgb_batch1.val[1]));
    uint16x8_t b1_lo = vmovl_u8(vget_low_u8(rgb_batch1.val[2]));
    uint16x8_t b1_hi = vmovl_u8(vget_high_u8(rgb_batch1.val[2]));

    // Process batch 2 (pixels 16-31)
    uint16x8_t r2_lo = vmovl_u8(vget_low_u8(rgb_batch2.val[0]));
    uint16x8_t r2_hi = vmovl_u8(vget_high_u8(rgb_batch2.val[0]));
    uint16x8_t g2_lo = vmovl_u8(vget_low_u8(rgb_batch2.val[1]));
    uint16x8_t g2_hi = vmovl_u8(vget_high_u8(rgb_batch2.val[1]));
    uint16x8_t b2_lo = vmovl_u8(vget_low_u8(rgb_batch2.val[2]));
    uint16x8_t b2_hi = vmovl_u8(vget_high_u8(rgb_batch2.val[2]));

    // Parallel luminance computation on 4 NEON vectors (32 pixels total)
    uint16x8_t luma1_lo = vmulq_n_u16(r1_lo, LUMA_RED);
    luma1_lo = vmlaq_n_u16(luma1_lo, g1_lo, LUMA_GREEN);
    luma1_lo = vmlaq_n_u16(luma1_lo, b1_lo, LUMA_BLUE);
    luma1_lo = vshrq_n_u16(luma1_lo, 8);

    uint16x8_t luma1_hi = vmulq_n_u16(r1_hi, LUMA_RED);
    luma1_hi = vmlaq_n_u16(luma1_hi, g1_hi, LUMA_GREEN);
    luma1_hi = vmlaq_n_u16(luma1_hi, b1_hi, LUMA_BLUE);
    luma1_hi = vshrq_n_u16(luma1_hi, 8);

    uint16x8_t luma2_lo = vmulq_n_u16(r2_lo, LUMA_RED);
    luma2_lo = vmlaq_n_u16(luma2_lo, g2_lo, LUMA_GREEN);
    luma2_lo = vmlaq_n_u16(luma2_lo, b2_lo, LUMA_BLUE);
    luma2_lo = vshrq_n_u16(luma2_lo, 8);

    uint16x8_t luma2_hi = vmulq_n_u16(r2_hi, LUMA_RED);
    luma2_hi = vmlaq_n_u16(luma2_hi, g2_hi, LUMA_GREEN);
    luma2_hi = vmlaq_n_u16(luma2_hi, b2_hi, LUMA_BLUE);
    luma2_hi = vshrq_n_u16(luma2_hi, 8);

    // Pack luminance results into two 16-byte vectors
    uint8x16_t luma_vec1 = vcombine_u8(vqmovn_u16(luma1_lo), vqmovn_u16(luma1_hi));
    uint8x16_t luma_vec2 = vcombine_u8(vqmovn_u16(luma2_lo), vqmovn_u16(luma2_hi));

    // PRIORITY 3: ASCII MAPPING OPTIMIZATION with vtbl4_u8() - 16-way parallel lookup
    // Use NEON table lookup to convert 16 luminance values to ASCII characters simultaneously
    // This eliminates 32 scalar array lookups with just 2 NEON vtbl operations

    // PRIORITY 3 OPTIMIZATION: Hybrid approach using existing luminance_palette with vectorized stores
    // This maintains 100% correctness while getting SIMD benefits from batch processing and vectorized stores

    // Exact palette index: floor(luma * palette_len / 255) using (p + 1 + (p >> 8)) >> 8
    const uint8x16_t palette_len_u8 = vdupq_n_u8((uint8_t)g_ascii_cache.palette_len);
    uint8x8_t l_lo_u8_1 = vget_low_u8(luma_vec1);
    uint8x8_t l_hi_u8_1 = vget_high_u8(luma_vec1);
    uint16x8_t prod_lo_1 = vmull_u8(l_lo_u8_1, vget_low_u8(palette_len_u8));
    uint16x8_t prod_hi_1 = vmull_u8(l_hi_u8_1, vget_high_u8(palette_len_u8));
    uint16x8_t idx16_lo_1 = vshrq_n_u16(vaddq_u16(vaddq_u16(prod_lo_1, vshrq_n_u16(prod_lo_1, 8)), vdupq_n_u16(1)), 8);
    uint16x8_t idx16_hi_1 = vshrq_n_u16(vaddq_u16(vaddq_u16(prod_hi_1, vshrq_n_u16(prod_hi_1, 8)), vdupq_n_u16(1)), 8);
    uint8x16_t idx_vec1 = vcombine_u8(vqmovn_u16(idx16_lo_1), vqmovn_u16(idx16_hi_1));

    // Repeat for luma_vec2
    uint8x8_t l_lo_u8_2 = vget_low_u8(luma_vec2);
    uint8x8_t l_hi_u8_2 = vget_high_u8(luma_vec2);
    uint16x8_t prod_lo_2 = vmull_u8(l_lo_u8_2, vget_low_u8(palette_len_u8));
    uint16x8_t prod_hi_2 = vmull_u8(l_hi_u8_2, vget_high_u8(palette_len_u8));
    uint16x8_t idx16_lo_2 = vshrq_n_u16(vaddq_u16(vaddq_u16(prod_lo_2, vshrq_n_u16(prod_lo_2, 8)), vdupq_n_u16(1)), 8);
    uint16x8_t idx16_hi_2 = vshrq_n_u16(vaddq_u16(vaddq_u16(prod_hi_2, vshrq_n_u16(prod_hi_2, 8)), vdupq_n_u16(1)), 8);
    uint8x16_t idx_vec2 = vcombine_u8(vqmovn_u16(idx16_lo_2), vqmovn_u16(idx16_hi_2));

    // Create padded 32-byte table from ascii_chars (only 21 valid characters)
    uint8_t padded_ascii_table[32];
    memcpy(padded_ascii_table, g_ascii_cache.ascii_chars, g_ascii_cache.palette_len);
    // Fill remaining bytes with spaces to handle out-of-bounds indices gracefully
    memset(padded_ascii_table + g_ascii_cache.palette_len, ' ', 32 - g_ascii_cache.palette_len);

    const uint8x16x2_t ascii_tbl = {vld1q_u8(&padded_ascii_table[0]), vld1q_u8(&padded_ascii_table[16])};
    uint8x16_t ascii1 = vqtbl2q_u8(ascii_tbl, idx_vec1);
    uint8x16_t ascii2 = vqtbl2q_u8(ascii_tbl, idx_vec2);

    // Store 32 ASCII characters
    vst1q_u8((uint8_t *)&ascii_chars[i], ascii1);
    vst1q_u8((uint8_t *)&ascii_chars[i + 16], ascii2);
  }

  // Handle remaining 16 pixels
  // FIXME: this should be scalar code -- don't use SIMD functions claude lol
  for (; i + 15 < count; i += 16) {
    const uint8_t *rgb_data = (const uint8_t *)&pixels[i];
    uint8x16x3_t rgb_vectors = vld3q_u8(rgb_data);

    uint16x8_t r_lo = vmovl_u8(vget_low_u8(rgb_vectors.val[0]));
    uint16x8_t r_hi = vmovl_u8(vget_high_u8(rgb_vectors.val[0]));
    uint16x8_t g_lo = vmovl_u8(vget_low_u8(rgb_vectors.val[1]));
    uint16x8_t g_hi = vmovl_u8(vget_high_u8(rgb_vectors.val[1]));
    uint16x8_t b_lo = vmovl_u8(vget_low_u8(rgb_vectors.val[2]));
    uint16x8_t b_hi = vmovl_u8(vget_high_u8(rgb_vectors.val[2]));

    uint16x8_t luma_lo = vmulq_n_u16(r_lo, LUMA_RED);
    luma_lo = vmlaq_n_u16(luma_lo, g_lo, LUMA_GREEN);
    luma_lo = vmlaq_n_u16(luma_lo, b_lo, LUMA_BLUE);
    luma_lo = vshrq_n_u16(luma_lo, 8);

    uint16x8_t luma_hi = vmulq_n_u16(r_hi, LUMA_RED);
    luma_hi = vmlaq_n_u16(luma_hi, g_hi, LUMA_GREEN);
    luma_hi = vmlaq_n_u16(luma_hi, b_hi, LUMA_BLUE);
    luma_hi = vshrq_n_u16(luma_hi, 8);

    uint8x16_t luma_vec = vcombine_u8(vqmovn_u16(luma_lo), vqmovn_u16(luma_hi));

    // SIMD table lookup: map luminance (0-255) to ASCII character index (0-20)
    // Formula: char_idx = (luminance * palette_len) / 256
    const uint8x16_t pl_u8 = vdupq_n_u8((uint8_t)g_ascii_cache.palette_len);

    // Multiply luminance by palette_len (21) to get indices in range 0-5355
    uint8x8_t l_lo = vget_low_u8(luma_vec);
    uint8x8_t l_hi = vget_high_u8(luma_vec);
    uint16x8_t prod_lo = vmull_u8(l_lo, vget_low_u8(pl_u8));
    uint16x8_t prod_hi = vmull_u8(l_hi, vget_high_u8(pl_u8));

    // Divide by 256 to get final character indices (0-20)
    uint8x8_t idx_lo = vshrn_n_u16(prod_lo, 8);
    uint8x8_t idx_hi = vshrn_n_u16(prod_hi, 8);
    uint8x16_t char_idx = vcombine_u8(idx_lo, idx_hi);

    // Build NEON table with the actual ASCII characters (21 chars + 11 padding)
    uint8x16x2_t ascii_tbl;
    ascii_tbl.val[0] = vld1q_u8((const uint8_t *)g_ascii_cache.ascii_chars);              // chars 0-15
    ascii_tbl.val[1] = vsetq_lane_u8(g_ascii_cache.ascii_chars[16], vdupq_n_u8(0), 0);    // char 16 + padding
    ascii_tbl.val[1] = vsetq_lane_u8(g_ascii_cache.ascii_chars[17], ascii_tbl.val[1], 1); // char 17
    ascii_tbl.val[1] = vsetq_lane_u8(g_ascii_cache.ascii_chars[18], ascii_tbl.val[1], 2); // char 18
    ascii_tbl.val[1] = vsetq_lane_u8(g_ascii_cache.ascii_chars[19], ascii_tbl.val[1], 3); // char 19
    ascii_tbl.val[1] = vsetq_lane_u8(g_ascii_cache.ascii_chars[20], ascii_tbl.val[1], 4); // char 20

    // Vectorized table lookup: 16 character indices → 16 ASCII characters
    uint8x16_t ascii_vec = vqtbl2q_u8(ascii_tbl, char_idx);
    vst1q_u8((uint8_t *)&ascii_chars[i], ascii_vec);
  }
}

// =============== ChatGPT's REP-safe renderer helpers =======================================

static inline uint8_t luminance_u8_neon(uint8_t r, uint8_t g, uint8_t b) {
  // same weights: (77R + 150G + 29B) >> 8
  unsigned y = 77u * r + 150u * g + 29u * b;
  return (uint8_t)(y >> 8);
}

// fast 256-color FG mapping (cube vs. gray) – same math as existing code
static inline uint8_t rgb_to_ansi256_fg_neon(uint8_t r, uint8_t g, uint8_t b) {
  // Convert to 6-level cube coordinates (0-5)
  int cr = (r * 5 + 127) / 255;
  int cg = (g * 5 + 127) / 255;
  int cb = (b * 5 + 127) / 255;

  // Calculate 6x6x6 cube color values
  int cube_r = (cr * 255) / 5;
  int cube_g = (cg * 255) / 5;
  int cube_b = (cb * 255) / 5;

  // Check if it's closer to a gray level (colors 232-255: 24 grays)
  int gray = (r + g + b) / 3;
  int closest_gray_idx = 232 + (gray * 23) / 255;
  int gray_level = 8 + (closest_gray_idx - 232) * 10;

  // Use proper Euclidean distance for fair comparison
  int gray_dist_sq =
      (r - gray_level) * (r - gray_level) + (g - gray_level) * (g - gray_level) + (b - gray_level) * (b - gray_level);

  int cube_dist_sq = (r - cube_r) * (r - cube_r) + (g - cube_g) * (g - cube_g) + (b - cube_b) * (b - cube_b);

  // Add bias toward color cube to prevent grayscale dominance
  // Only use grayscale if it's significantly better (20% threshold)
  if (gray_dist_sq * 5 < cube_dist_sq * 4) {
    return (uint8_t)closest_gray_idx;
  } else {
    return (uint8_t)(16 + cr * 36 + cg * 6 + cb);
  }
}

static inline void append_sgr256_fg_simple(char **pos, char *end, uint8_t idx) {
  // Simple SGR: \x1b[38;5;<idx>m
  if (*pos + 16 >= end)
    return; // safety check

  **pos = '\x1b';
  (*pos)++;
  **pos = '[';
  (*pos)++;
  **pos = '3';
  (*pos)++;
  **pos = '8';
  (*pos)++;
  **pos = ';';
  (*pos)++;
  **pos = '5';
  (*pos)++;
  **pos = ';';
  (*pos)++;

  // Convert idx to decimal
  if (idx >= 100) {
    **pos = '0' + (idx / 100);
    (*pos)++;
    idx %= 100;
  }
  if (idx >= 10) {
    **pos = '0' + (idx / 10);
    (*pos)++;
    idx %= 10;
  }
  **pos = '0' + idx;
  (*pos)++;
  **pos = 'm';
  (*pos)++;
}

// One-pixel ASCII+color encoder for 256-FG mode
typedef struct {
  char glyph;
  uint8_t fg_idx;
} Enc256;
static inline Enc256 encode_pixel_256fg_neon(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t y = luminance_u8_neon(r, g, b);
  // Use correct luminance palette with full 8-bit luminance
  char ch = g_ascii_cache.luminance_palette[y];
  uint8_t idx = rgb_to_ansi256_fg_neon(r, g, b);
  Enc256 e = {ch, idx};
  return e;
}

// Flush the current run EXACTLY ONCE (never across newlines)
static inline void flush_run_safe(char **pos, char *end, char last_ch, int run_len) {
  if (run_len <= 0)
    return;
  if (*pos >= end)
    return; // safety check

  // CRITICAL FIX: Don't use REP sequences - many terminals don't support them
  // Instead, just output the character repeatedly for maximum compatibility
  for (int i = 0; i < run_len && *pos < end; i++) {
    **pos = last_ch;
    (*pos)++;
  }
}

// =============== ChatGPT's main REP-safe image renderer =======================================

char *render_ascii_image_256fg_rep_safe(const image_t *image) {
  const int W = image->w, H = image->h;
  const rgb_pixel_t *pixels = image->pixels;

  // Heuristic reserve: worst-case per glyph: SGR(<=15) + char(1)
  // Reserve generously to avoid reallocs
  const size_t buffer_size = (size_t)H * ((size_t)W * 16u + 8u) + 32u;

  char *output;
  SAFE_MALLOC(output, buffer_size, char *);

  char *pos = output;
  char *end = output + buffer_size - 32; // safety margin

  // Terminal state for current row (SGR FG index)
  int cur_fg = -1;

  for (int y = 0; y < H; ++y) {
    const rgb_pixel_t *row = &pixels[(size_t)y * (size_t)W];

    // Per-row RLE state - CRITICAL: reset at each row to prevent REP crossing newlines
    int run_len = 0;
    char last_ch = 0;
    int last_fg = -1;

    for (int x = 0; x < W; ++x) {
      uint8_t r = row[x].r;
      uint8_t g = row[x].g;
      uint8_t b = row[x].b;

      Enc256 e = encode_pixel_256fg_neon(r, g, b);
      char ch = e.glyph;
      int fg = (int)e.fg_idx;

      // If color changed, flush run, then set SGR
      if (fg != cur_fg) {
        flush_run_safe(&pos, end, last_ch, run_len);
        run_len = 0;
        last_ch = 0;
        last_fg = -1;
        append_sgr256_fg_simple(&pos, end, (uint8_t)fg);
        cur_fg = fg;
      }

      // If glyph continues the same run (same glyph & same color), extend
      if (run_len > 0 && ch == last_ch && fg == last_fg) {
        run_len++;
      } else {
        // glyph changed or starting new run → flush previous, start new
        flush_run_safe(&pos, end, last_ch, run_len);
        last_ch = ch;
        last_fg = fg;
        run_len = 1;
      }
    }

    // End of row: flush the final run for this line
    flush_run_safe(&pos, end, last_ch, run_len);

    // Add newline only if this is NOT the final row
    if (y < H - 1 && pos < end) {
      *pos++ = '\n';
    }

    // Keep FG state across lines to reduce SGRs
  }

  // End-of-frame reset
  if (pos + 4 < end) {
    memcpy(pos, "\033[0m", 4);
    pos += 4;
  }

  // Null terminate
  if (pos < end) {
    *pos = '\0';
  }

  return output;
}

// Helper function for truecolor run flushing
static inline void flush_run_truecolor_safe(char **pos, char *end, char ch, int run_len, int r, int g, int b) {
  if (*pos >= end - 50)
    return; // Safety check

  // Emit truecolor FG SGR: \033[38;2;R;G;Bm
  **pos = '\033';
  (*pos)++;
  **pos = '[';
  (*pos)++;
  **pos = '3';
  (*pos)++;
  **pos = '8';
  (*pos)++;
  **pos = ';';
  (*pos)++;
  **pos = '2';
  (*pos)++;
  **pos = ';';
  (*pos)++;

  // Write RGB values using decimal conversion
  *pos += write_decimal(r, *pos);
  **pos = ';';
  (*pos)++;
  *pos += write_decimal(g, *pos);
  **pos = ';';
  (*pos)++;
  *pos += write_decimal(b, *pos);
  **pos = 'm';
  (*pos)++;

  // Emit the character(s) - don't use REP sequences for compatibility
  for (int i = 0; i < run_len && *pos < end; i++) {
    **pos = ch;
    (*pos)++;
  }
}

// Truecolor REP-safe renderer that handles newlines internally (like 256fg version)
char *render_ascii_image_truecolor_fg_rep_safe(const image_t *image) {
  if (!image || !image->pixels) {
    log_error("render_ascii_image_truecolor_fg_rep_safe: image or pixels is NULL");
    return NULL;
  }

  const int w = image->w;
  const int h = image->h;

  // Conservative buffer size calculation for truecolor (20 bytes per pixel max)
  const size_t max_per_pixel = 20; // \033[38;2;255;255;255m + char = ~20 bytes
  const size_t buffer_size = (size_t)h * (size_t)w * max_per_pixel + (size_t)h * 10 + 1000;

  char *output;
  SAFE_MALLOC(output, buffer_size, char *);
  if (!output) {
    log_error("render_ascii_image_truecolor_fg_rep_safe: Failed to allocate %zu bytes", buffer_size);
    return NULL;
  }

  char *pos = output;
  char *end = output + buffer_size - 100; // Safety margin

  // State for FG color continuity across lines
  int last_fg_r = -1, last_fg_g = -1, last_fg_b = -1;

  for (int y = 0; y < h; y++) {
    const rgb_pixel_t *row = &image->pixels[y * w];

    // RLE state for this row (reset each row to prevent REP crossing newlines)
    char last_ch = '\0';
    int run_len = 0;
    int curr_fg_r = -1, curr_fg_g = -1, curr_fg_b = -1;

    for (int x = 0; x < w; x++) {
      const rgb_pixel_t *px = &row[x];

      // Convert pixel to ASCII character
      uint8_t luma = luminance_u8_neon(px->r, px->g, px->b);
      char ch = g_ascii_cache.luminance_palette[luma];

      // Use truecolor RGB values directly (no quantization)
      curr_fg_r = px->r;
      curr_fg_g = px->g;
      curr_fg_b = px->b;

      // Check if we can extend the current run
      bool can_extend = (ch == last_ch && curr_fg_r == last_fg_r && curr_fg_g == last_fg_g && curr_fg_b == last_fg_b);

      if (can_extend && run_len > 0) {
        run_len++;
      } else {
        // Flush previous run
        if (run_len > 0) {
          flush_run_truecolor_safe(&pos, end, last_ch, run_len, last_fg_r, last_fg_g, last_fg_b);
        }

        // Start new run
        last_ch = ch;
        run_len = 1;
        last_fg_r = curr_fg_r;
        last_fg_g = curr_fg_g;
        last_fg_b = curr_fg_b;
      }
    }

    // End of row: flush the final run for this line
    flush_run_truecolor_safe(&pos, end, last_ch, run_len, last_fg_r, last_fg_g, last_fg_b);

    // Add newline only if this is NOT the final row
    if (y < h - 1 && pos < end) {
      *pos++ = '\n';
    }

    // Keep FG state across lines to reduce SGRs
  }

  // End-of-frame reset
  if (pos + 4 < end) {
    memcpy(pos, "\033[0m", 4);
    pos += 4;
  }

  // Null terminate
  if (pos < end) {
    *pos = '\0';
  }

  return output;
}

#endif // main block of code endif

// Unified NEON + scalar REP dispatcher
size_t render_row_ascii_rep_dispatch_neon_color(const rgb_pixel_t *row, int width, char *dst, size_t cap,
                                                bool background_mode, bool use_fast_path) {
  if (background_mode) {
    if (use_fast_path) {
      return render_row_256color_background_rep_unified(row, width, dst, cap);
    } else {
      return render_row_truecolor_background_rep_unified(row, width, dst, cap);
    }
  } else {
#ifdef SIMD_SUPPORT_NEON
    if (use_fast_path) {
      // 256-color foreground ASCII
      return render_row_neon_256_fg_rep(row, width, dst, cap);
    } else {
      // Truecolor foreground ASCII
      return render_row_neon_truecolor_fg_rep(row, width, dst, cap);
    }
#else
    // Fallback for non-NEON platforms
    if (use_fast_path) {
      return render_row_256color_foreground_rep_unified(row, width, dst, cap);
    } else {
      return render_row_truecolor_foreground_rep_unified(row, width, dst, cap);
    }
#endif
  }
}

//=============================================================================
// Simple Monochrome ASCII Function (matches scalar image_print performance)
#ifdef SIMD_SUPPORT_NEON
//=============================================================================
char *render_ascii_image_monochrome_neon(const image_t *image) {
  if (!image || !image->pixels) {
    return NULL;
  }

  const int h = image->h;
  const int w = image->w;

  if (h <= 0 || w <= 0) {
    return NULL;
  }

  // Match scalar allocation exactly: h rows * (w chars + 1 newline) + null terminator
  const size_t len = (size_t)h * ((size_t)w + 1);

  char *output;
  SAFE_MALLOC(output, len, char *);

  char *pos = output;
  const rgb_pixel_t *pixels = (const rgb_pixel_t *)image->pixels;

  // Pure NEON processing - no scalar fallbacks
  for (int y = 0; y < h; y++) {
    const rgb_pixel_t *row = &pixels[y * w];
    int x = 0;

    // Process 16 pixels at a time with NEON
    for (; x + 15 < w; x += 16) {
      // Load 16 RGB pixels (48 bytes)
      uint8x16x3_t rgb = vld3q_u8((const uint8_t *)(row + x));

      // Calculate luminance for all 16 pixels: (77*R + 150*G + 29*B + 128) >> 8
      uint16x8_t luma_lo = vmull_u8(vget_low_u8(rgb.val[0]), vdup_n_u8(77)); // R * 77
      luma_lo = vmlal_u8(luma_lo, vget_low_u8(rgb.val[1]), vdup_n_u8(150));  // + G * 150
      luma_lo = vmlal_u8(luma_lo, vget_low_u8(rgb.val[2]), vdup_n_u8(29));   // + B * 29
      luma_lo = vaddq_u16(luma_lo, vdupq_n_u16(128));                        // + 128 (rounding)
      luma_lo = vshrq_n_u16(luma_lo, 8);                                     // >> 8

      uint16x8_t luma_hi = vmull_u8(vget_high_u8(rgb.val[0]), vdup_n_u8(77));
      luma_hi = vmlal_u8(luma_hi, vget_high_u8(rgb.val[1]), vdup_n_u8(150));
      luma_hi = vmlal_u8(luma_hi, vget_high_u8(rgb.val[2]), vdup_n_u8(29));
      luma_hi = vaddq_u16(luma_hi, vdupq_n_u16(128));
      luma_hi = vshrq_n_u16(luma_hi, 8);

      // Convert 16-bit luminance back to 8-bit
      uint8x16_t luminance = vcombine_u8(vmovn_u16(luma_lo), vmovn_u16(luma_hi));

      // Store luminance values and convert to ASCII characters
      uint8_t luma_array[16];
      vst1q_u8(luma_array, luminance);

      // Convert luminance to ASCII characters using palette lookup
      for (int i = 0; i < 16; i++) {
        pos[i] = g_ascii_cache.luminance_palette[luma_array[i]];
      }
      pos += 16;
    }

    // Handle remaining pixels with scalar code
    for (; x < w; x++) {
      const rgb_pixel_t pixel = row[x];
      const int luminance = (77 * pixel.r + 150 * pixel.g + 29 * pixel.b + 128) >> 8;
      *pos++ = g_ascii_cache.luminance_palette[luminance];
    }

    // Add newline (except for last row)
    if (y < h - 1) {
      *pos++ = '\n';
    }
  }

  // Null terminate
  *pos = '\0';

  return output;
}

//=============================================================================
// Optimized NEON Truecolor Converter (based on ChatGPT reference)
//=============================================================================

// Dynamic output buffer (auto-expanding)
typedef struct {
  char *buf;
  size_t len;
  size_t cap;
} neon_outbuf_t;

static inline void ob_reserve(neon_outbuf_t *ob, size_t need) {
  if (ob->len + need <= ob->cap)
    return;
  size_t ncap = ob->cap ? ob->cap : 4096;
  while (ncap < ob->len + need)
    ncap = (ncap * 3) / 2;
  char *nbuf = (char *)realloc(ob->buf, ncap);
  if (!nbuf) {
    abort();
  }
  ob->buf = nbuf;
  ob->cap = ncap;
}

static inline void ob_putc(neon_outbuf_t *ob, char c) {
  ob_reserve(ob, 1);
  ob->buf[ob->len++] = c;
}

static inline void ob_write(neon_outbuf_t *ob, const char *s, size_t n) {
  ob_reserve(ob, n);
  memcpy(ob->buf + ob->len, s, n);
  ob->len += n;
}

static inline void ob_term(neon_outbuf_t *ob) {
  ob_putc(ob, '\0');
}

// Fast decimal for uint8_t (0..255)
static inline void ob_u8(neon_outbuf_t *ob, uint8_t v) {
  if (v >= 100) {
    uint8_t d0 = v / 100;
    uint8_t r = v % 100;
    uint8_t d1 = r / 10;
    uint8_t d2 = r % 10;
    ob_putc(ob, '0' + d0);
    ob_putc(ob, '0' + d1);
    ob_putc(ob, '0' + d2);
  } else if (v >= 10) {
    uint8_t d1 = v / 10;
    uint8_t d2 = v % 10;
    ob_putc(ob, '0' + d1);
    ob_putc(ob, '0' + d2);
  } else {
    ob_putc(ob, '0' + v);
  }
}

// Fast decimal for REP counts
static inline int digits_u32(uint32_t v) {
  if (v >= 1000000000u)
    return 10;
  if (v >= 100000000u)
    return 9;
  if (v >= 10000000u)
    return 8;
  if (v >= 1000000u)
    return 7;
  if (v >= 100000u)
    return 6;
  if (v >= 10000u)
    return 5;
  if (v >= 1000u)
    return 4;
  if (v >= 100u)
    return 3;
  if (v >= 10u)
    return 2;
  return 1;
}

static inline void ob_u32(neon_outbuf_t *ob, uint32_t v) {
  char tmp[10];
  int i = 0;
  do {
    tmp[i++] = '0' + (v % 10u);
    v /= 10u;
  } while (v);
  ob_reserve(ob, (size_t)i);
  while (i--)
    ob->buf[ob->len++] = tmp[i];
}

#define RAMP64_SIZE 64

static void build_ramp64(uint8_t ramp64[RAMP64_SIZE]) {
  // Use the project's ASCII character palette from g_ascii_cache
  const char *ascii_chars = g_ascii_cache.ascii_chars; // "   ...',;:clodxkO0KXNWM"
  const int L = strlen(ascii_chars);                   // 24 chars

  for (int i = 0; i < RAMP64_SIZE; i++) {
    // Map 0-63 to 0-23 (project's 24-char palette)
    int idx = (i * (L - 1) + (RAMP64_SIZE - 1) / 2) / (RAMP64_SIZE - 1); // rounded
    ramp64[i] = (uint8_t)ascii_chars[idx];
  }
}

// Truecolor SGR emission (foreground)
static inline void emit_set_truecolor_fg(neon_outbuf_t *ob, uint8_t r, uint8_t g, uint8_t b) {
  // ESC[38;2;R;G;Bm
  ob_putc(ob, 0x1b);
  ob_putc(ob, '[');
  ob_write(ob, "38;2;", 5);
  ob_u8(ob, r);
  ob_putc(ob, ';');
  ob_u8(ob, g);
  ob_putc(ob, ';');
  ob_u8(ob, b);
  ob_putc(ob, 'm');
}

// Truecolor SGR emission (background)
static inline void emit_set_truecolor_bg(neon_outbuf_t *ob, uint8_t r, uint8_t g, uint8_t b) {
  // ESC[48;2;R;G;Bm
  ob_putc(ob, 0x1b);
  ob_putc(ob, '[');
  ob_write(ob, "48;2;", 5);
  ob_u8(ob, r);
  ob_putc(ob, ';');
  ob_u8(ob, g);
  ob_putc(ob, ';');
  ob_u8(ob, b);
  ob_putc(ob, 'm');
}

static inline void emit_reset_neon(neon_outbuf_t *ob) {
  ob_putc(ob, 0x1b);
  ob_putc(ob, '[');
  ob_putc(ob, '0');
  ob_putc(ob, 'm');
}

// REP profitability calculation
static inline bool rep_is_profitable(uint32_t runlen) {
  if (runlen <= 1)
    return false;
  uint32_t k = runlen - 1;
  return k > (uint32_t)(digits_u32(k) + 3);
}

static inline void emit_rep(neon_outbuf_t *ob, uint32_t extra) {
  // ESC [ extra b
  ob_putc(ob, 0x1b);
  ob_putc(ob, '[');
  ob_u32(ob, extra);
  ob_putc(ob, 'b');
}

// Optimized NEON truecolor converter based on ChatGPT reference
char *render_truecolor_ascii_neon_optimized(const image_t *image) {
  if (!image || !image->pixels) {
    return NULL;
  }

  const int width = image->w;
  const int height = image->h;

  if (width <= 0 || height <= 0) {
    char *empty;
    SAFE_MALLOC(empty, 1, char *);
    empty[0] = '\0';
    return empty;
  }

  neon_outbuf_t ob = {0};
  // Estimate buffer size: ~8 bytes per pixel + overhead
  ob.cap = (size_t)height * (size_t)width * 8u + (size_t)height * 16u + 64u;
  ob.buf = (char *)malloc(ob.cap ? ob.cap : 1);
  if (!ob.buf)
    return NULL;

  // Build 64-entry glyph LUT for vqtbl4q_u8
  uint8_t ramp64[RAMP64_SIZE];
  build_ramp64(ramp64);

  uint8x16x4_t tbl; // 4 * 16 = 64 entries
  tbl.val[0] = vld1q_u8(&ramp64[0]);
  tbl.val[1] = vld1q_u8(&ramp64[16]);
  tbl.val[2] = vld1q_u8(&ramp64[32]);
  tbl.val[3] = vld1q_u8(&ramp64[48]);

  // Track current SGR color to avoid redundant escapes
  int curR = -1, curG = -1, curB = -1;

  for (int y = 0; y < height; y++) {
    const rgb_pixel_t *row = &((const rgb_pixel_t *)image->pixels)[y * width];
    int x = 0;

    // Process 16-pixel chunks with NEON
    while (x + 16 <= width) {
      // Load 16 pixels: R,G,B interleaved
      const uint8_t *p = (const uint8_t *)(row + x);
      uint8x16x3_t pix = vld3q_u8(p); // 48 bytes

      // Vector luminance: Y ≈ (77*R + 150*G + 29*B + 128) >> 8
      uint16x8_t ylo = vmull_u8(vget_low_u8(pix.val[0]), vdup_n_u8(77));
      ylo = vmlal_u8(ylo, vget_low_u8(pix.val[1]), vdup_n_u8(150));
      ylo = vmlal_u8(ylo, vget_low_u8(pix.val[2]), vdup_n_u8(29));
      ylo = vaddq_u16(ylo, vdupq_n_u16(128));
      ylo = vshrq_n_u16(ylo, 8);

      uint16x8_t yhi = vmull_u8(vget_high_u8(pix.val[0]), vdup_n_u8(77));
      yhi = vmlal_u8(yhi, vget_high_u8(pix.val[1]), vdup_n_u8(150));
      yhi = vmlal_u8(yhi, vget_high_u8(pix.val[2]), vdup_n_u8(29));
      yhi = vaddq_u16(yhi, vdupq_n_u16(128));
      yhi = vshrq_n_u16(yhi, 8);

      uint8x16_t y8 = vcombine_u8(vmovn_u16(ylo), vmovn_u16(yhi));
      uint8x16_t idx = vshrq_n_u8(y8, 2); // 0..63

      // 64-entry glyph lookup using vqtbl4q_u8
      uint8x16_t glyph = vqtbl4q_u8(tbl, idx);

      // Spill to arrays for RLE + ANSI emission
      uint8_t gbuf[16], rbuf[16], gbuf_green[16], bbuf[16];
      vst1q_u8(gbuf, glyph);
      vst1q_u8(rbuf, pix.val[0]);
      vst1q_u8(gbuf_green, pix.val[1]);
      vst1q_u8(bbuf, pix.val[2]);

      // Emit with RLE on (glyph, color) runs
      for (int i = 0; i < 16;) {
        const uint8_t ch = gbuf[i];
        const uint8_t r = rbuf[i];
        const uint8_t g = gbuf_green[i];
        const uint8_t b = bbuf[i];

        int j = i + 1;
        // Find run of identical (glyph, color)
        while (j < 16 && gbuf[j] == ch && rbuf[j] == r && gbuf_green[j] == g && bbuf[j] == b) {
          j++;
        }
        const uint32_t run = (uint32_t)(j - i);

        // Set color if changed
        if (r != curR || g != curG || b != curB) {
          emit_set_truecolor_fg(&ob, r, g, b);
          curR = r;
          curG = g;
          curB = b;
        }

        // Emit character
        ob_putc(&ob, (char)ch);

        // Use REP if profitable
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; k++) {
            ob_putc(&ob, (char)ch);
          }
        }
        i = j;
      }
      x += 16;
    }

    // Scalar tail for remaining pixels
    for (; x < width;) {
      const rgb_pixel_t *p = &row[x];
      // Luminance calculation (matches NEON)
      uint32_t R = p->r, G = p->g, B = p->b;
      uint8_t Y = (uint8_t)((77u * R + 150u * G + 29u * B + 128u) >> 8);
      uint8_t ch = ramp64[Y >> 2];

      // Find run of identical (glyph, color)
      int j = x + 1;
      while (j < width) {
        const rgb_pixel_t *q = &row[j];
        uint32_t R2 = q->r, G2 = q->g, B2 = q->b;
        uint8_t Y2 = (uint8_t)((77u * R2 + 150u * G2 + 29u * B2 + 128u) >> 8);
        if (((Y2 >> 2) != (Y >> 2)) || R2 != R || G2 != G || B2 != B)
          break;
        j++;
      }
      uint32_t run = (uint32_t)(j - x);

      // Set color if changed
      if ((int)R != curR || (int)G != curG || (int)B != curB) {
        emit_set_truecolor_fg(&ob, (uint8_t)R, (uint8_t)G, (uint8_t)B);
        curR = (int)R;
        curG = (int)G;
        curB = (int)B;
      }

      // Emit character
      ob_putc(&ob, (char)ch);

      // Use REP if profitable
      if (rep_is_profitable(run)) {
        emit_rep(&ob, run - 1);
      } else {
        for (uint32_t k = 1; k < run; k++) {
          ob_putc(&ob, (char)ch);
        }
      }
      x = j;
    }

    // End row: reset SGR, newline, invalidate color cache
    emit_reset_neon(&ob);
    ob_putc(&ob, '\n');
    curR = curG = curB = -1;
  }

  ob_term(&ob);
  return ob.buf;
}

// 256-color palette mapping (RGB to ANSI 256 color index)
static inline uint8_t rgb_to_256color(uint8_t r, uint8_t g, uint8_t b) {
  // Use existing project function for consistency
  return (uint8_t)(16 + 36 * (r / 51) + 6 * (g / 51) + (b / 51));
}

// 256-color SGR emission (foreground)
static inline void emit_set_256_color_fg(neon_outbuf_t *ob, uint8_t color_idx) {
  // ESC[38;5;Nm
  ob_putc(ob, 0x1b);
  ob_putc(ob, '[');
  ob_write(ob, "38;5;", 5);
  ob_u8(ob, color_idx);
  ob_putc(ob, 'm');
}

// 256-color SGR emission (background)
static inline void emit_set_256_color_bg(neon_outbuf_t *ob, uint8_t color_idx) {
  // ESC[48;5;Nm
  ob_putc(ob, 0x1b);
  ob_putc(ob, '[');
  ob_write(ob, "48;5;", 5);
  ob_u8(ob, color_idx);
  ob_putc(ob, 'm');
}

// Optimized NEON 256-color converter (based on ChatGPT reference)
char *render_256color_ascii_neon_optimized(const image_t *image) {
  if (!image || !image->pixels) {
    return NULL;
  }

  const int width = image->w;
  const int height = image->h;

  if (width <= 0 || height <= 0) {
    char *empty;
    SAFE_MALLOC(empty, 1, char *);
    empty[0] = '\0';
    return empty;
  }

  neon_outbuf_t ob = {0};
  // 256-color uses shorter escape sequences than truecolor
  ob.cap = (size_t)height * (size_t)width * 6u + (size_t)height * 12u + 64u;
  ob.buf = (char *)malloc(ob.cap ? ob.cap : 1);
  if (!ob.buf)
    return NULL;

  // Build 64-entry glyph LUT for vqtbl4q_u8 (reuse same function)
  uint8_t ramp64[RAMP64_SIZE];
  build_ramp64(ramp64);

  uint8x16x4_t tbl; // 4 * 16 = 64 entries
  tbl.val[0] = vld1q_u8(&ramp64[0]);
  tbl.val[1] = vld1q_u8(&ramp64[16]);
  tbl.val[2] = vld1q_u8(&ramp64[32]);
  tbl.val[3] = vld1q_u8(&ramp64[48]);

  // Track current SGR 256-color index to avoid redundant escapes
  int cur_color_idx = -1;

  for (int y = 0; y < height; y++) {
    const rgb_pixel_t *row = &((const rgb_pixel_t *)image->pixels)[y * width];
    int x = 0;

    // Process 16-pixel chunks with NEON
    while (x + 16 <= width) {
      // Load 16 pixels: R,G,B interleaved
      const uint8_t *p = (const uint8_t *)(row + x);
      uint8x16x3_t pix = vld3q_u8(p); // 48 bytes

      // Vector luminance: Y ≈ (77*R + 150*G + 29*B + 128) >> 8
      uint16x8_t ylo = vmull_u8(vget_low_u8(pix.val[0]), vdup_n_u8(77));
      ylo = vmlal_u8(ylo, vget_low_u8(pix.val[1]), vdup_n_u8(150));
      ylo = vmlal_u8(ylo, vget_low_u8(pix.val[2]), vdup_n_u8(29));
      ylo = vaddq_u16(ylo, vdupq_n_u16(128));
      ylo = vshrq_n_u16(ylo, 8);

      uint16x8_t yhi = vmull_u8(vget_high_u8(pix.val[0]), vdup_n_u8(77));
      yhi = vmlal_u8(yhi, vget_high_u8(pix.val[1]), vdup_n_u8(150));
      yhi = vmlal_u8(yhi, vget_high_u8(pix.val[2]), vdup_n_u8(29));
      yhi = vaddq_u16(yhi, vdupq_n_u16(128));
      yhi = vshrq_n_u16(yhi, 8);

      uint8x16_t y8 = vcombine_u8(vmovn_u16(ylo), vmovn_u16(yhi));
      uint8x16_t idx = vshrq_n_u8(y8, 2); // 0..63

      // 64-entry glyph lookup using vqtbl4q_u8
      uint8x16_t glyph = vqtbl4q_u8(tbl, idx);

      // Spill to arrays for RLE + ANSI emission
      uint8_t gbuf[16], rbuf[16], gbuf_green[16], bbuf[16];
      vst1q_u8(gbuf, glyph);
      vst1q_u8(rbuf, pix.val[0]);
      vst1q_u8(gbuf_green, pix.val[1]);
      vst1q_u8(bbuf, pix.val[2]);

      // Convert RGB to 256-color indices
      uint8_t color_indices[16];
      for (int i = 0; i < 16; i++) {
        color_indices[i] = rgb_to_256color(rbuf[i], gbuf_green[i], bbuf[i]);
      }

      // Emit with RLE on (glyph, color) runs
      for (int i = 0; i < 16;) {
        const uint8_t ch = gbuf[i];
        const uint8_t color_idx = color_indices[i];

        int j = i + 1;
        // Find run of identical (glyph, color)
        while (j < 16 && gbuf[j] == ch && color_indices[j] == color_idx) {
          j++;
        }
        const uint32_t run = (uint32_t)(j - i);

        // Set color if changed
        if (color_idx != cur_color_idx) {
          emit_set_256_color_fg(&ob, color_idx);
          cur_color_idx = color_idx;
        }

        // Emit character
        ob_putc(&ob, (char)ch);

        // Use REP if profitable
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; k++) {
            ob_putc(&ob, (char)ch);
          }
        }
        i = j;
      }
      x += 16;
    }

    // Scalar tail for remaining pixels
    for (; x < width;) {
      const rgb_pixel_t *p = &row[x];
      // Luminance calculation (matches NEON)
      uint32_t R = p->r, G = p->g, B = p->b;
      uint8_t Y = (uint8_t)((77u * R + 150u * G + 29u * B + 128u) >> 8);
      uint8_t ch = ramp64[Y >> 2];
      uint8_t color_idx = rgb_to_256color((uint8_t)R, (uint8_t)G, (uint8_t)B);

      // Find run of identical (glyph, color)
      int j = x + 1;
      while (j < width) {
        const rgb_pixel_t *q = &row[j];
        uint32_t R2 = q->r, G2 = q->g, B2 = q->b;
        uint8_t Y2 = (uint8_t)((77u * R2 + 150u * G2 + 29u * B2 + 128u) >> 8);
        uint8_t color_idx2 = rgb_to_256color((uint8_t)R2, (uint8_t)G2, (uint8_t)B2);
        if (((Y2 >> 2) != (Y >> 2)) || color_idx2 != color_idx)
          break;
        j++;
      }
      uint32_t run = (uint32_t)(j - x);

      // Set color if changed
      if (color_idx != cur_color_idx) {
        emit_set_256_color_fg(&ob, color_idx);
        cur_color_idx = color_idx;
      }

      // Emit character
      ob_putc(&ob, (char)ch);

      // Use REP if profitable
      if (rep_is_profitable(run)) {
        emit_rep(&ob, run - 1);
      } else {
        for (uint32_t k = 1; k < run; k++) {
          ob_putc(&ob, (char)ch);
        }
      }
      x = j;
    }

    // End row: reset SGR, newline, invalidate color cache
    emit_reset_neon(&ob);
    ob_putc(&ob, '\n');
    cur_color_idx = -1;
  }

  ob_term(&ob);
  return ob.buf;
}

// Unified optimized NEON converter (foreground/background + 256-color/truecolor)
char *render_ascii_neon_unified_optimized(const image_t *image, bool use_background, bool use_256color) {
  if (!image || !image->pixels) {
    return NULL;
  }

  const int width = image->w;
  const int height = image->h;

  if (width <= 0 || height <= 0) {
    char *empty;
    SAFE_MALLOC(empty, 1, char *);
    empty[0] = '\0';
    return empty;
  }

  neon_outbuf_t ob = {0};
  // Estimate buffer size based on mode
  size_t bytes_per_pixel = use_256color ? 6u : 8u; // 256-color shorter than truecolor
  ob.cap = (size_t)height * (size_t)width * bytes_per_pixel + (size_t)height * 16u + 64u;
  ob.buf = (char *)malloc(ob.cap ? ob.cap : 1);
  if (!ob.buf)
    return NULL;

  // Build 64-entry glyph LUT for vqtbl4q_u8
  uint8_t ramp64[RAMP64_SIZE];
  build_ramp64(ramp64);

  uint8x16x4_t tbl; // 4 * 16 = 64 entries
  tbl.val[0] = vld1q_u8(&ramp64[0]);
  tbl.val[1] = vld1q_u8(&ramp64[16]);
  tbl.val[2] = vld1q_u8(&ramp64[32]);
  tbl.val[3] = vld1q_u8(&ramp64[48]);

  // Track current color state
  int curR = -1, curG = -1, curB = -1;
  int cur_color_idx = -1;

  for (int y = 0; y < height; y++) {
    const rgb_pixel_t *row = &((const rgb_pixel_t *)image->pixels)[y * width];
    int x = 0;

    // Process 16-pixel chunks with NEON
    while (x + 16 <= width) {
      // Load 16 pixels: R,G,B interleaved
      const uint8_t *p = (const uint8_t *)(row + x);
      uint8x16x3_t pix = vld3q_u8(p); // 48 bytes

      // Vector luminance: Y ≈ (77*R + 150*G + 29*B + 128) >> 8
      uint16x8_t ylo = vmull_u8(vget_low_u8(pix.val[0]), vdup_n_u8(77));
      ylo = vmlal_u8(ylo, vget_low_u8(pix.val[1]), vdup_n_u8(150));
      ylo = vmlal_u8(ylo, vget_low_u8(pix.val[2]), vdup_n_u8(29));
      ylo = vaddq_u16(ylo, vdupq_n_u16(128));
      ylo = vshrq_n_u16(ylo, 8);

      uint16x8_t yhi = vmull_u8(vget_high_u8(pix.val[0]), vdup_n_u8(77));
      yhi = vmlal_u8(yhi, vget_high_u8(pix.val[1]), vdup_n_u8(150));
      yhi = vmlal_u8(yhi, vget_high_u8(pix.val[2]), vdup_n_u8(29));
      yhi = vaddq_u16(yhi, vdupq_n_u16(128));
      yhi = vshrq_n_u16(yhi, 8);

      uint8x16_t y8 = vcombine_u8(vmovn_u16(ylo), vmovn_u16(yhi));
      uint8x16_t idx = vshrq_n_u8(y8, 2); // 0..63

      // 64-entry glyph lookup using vqtbl4q_u8
      uint8x16_t glyph = vqtbl4q_u8(tbl, idx);

      // Spill to arrays for RLE + ANSI emission
      uint8_t gbuf[16], rbuf[16], gbuf_green[16], bbuf[16];
      vst1q_u8(gbuf, glyph);
      vst1q_u8(rbuf, pix.val[0]);
      vst1q_u8(gbuf_green, pix.val[1]);
      vst1q_u8(bbuf, pix.val[2]);

      if (use_256color) {
        // 256-color mode processing
        uint8_t color_indices[16];
        for (int i = 0; i < 16; i++) {
          color_indices[i] = rgb_to_256color(rbuf[i], gbuf_green[i], bbuf[i]);
        }

        // Emit with RLE on (glyph, color) runs
        for (int i = 0; i < 16;) {
          const uint8_t ch = gbuf[i];
          const uint8_t color_idx = color_indices[i];

          int j = i + 1;
          while (j < 16 && gbuf[j] == ch && color_indices[j] == color_idx) {
            j++;
          }
          const uint32_t run = (uint32_t)(j - i);

          if (color_idx != cur_color_idx) {
            if (use_background) {
              emit_set_256_color_bg(&ob, color_idx);
            } else {
              emit_set_256_color_fg(&ob, color_idx);
            }
            cur_color_idx = color_idx;
          }

          ob_putc(&ob, (char)ch);
          if (rep_is_profitable(run)) {
            emit_rep(&ob, run - 1);
          } else {
            for (uint32_t k = 1; k < run; k++) {
              ob_putc(&ob, (char)ch);
            }
          }
          i = j;
        }
      } else {
        // Truecolor mode processing
        for (int i = 0; i < 16;) {
          const uint8_t ch = gbuf[i];
          const uint8_t r = rbuf[i];
          const uint8_t g = gbuf_green[i];
          const uint8_t b = bbuf[i];

          int j = i + 1;
          while (j < 16 && gbuf[j] == ch && rbuf[j] == r && gbuf_green[j] == g && bbuf[j] == b) {
            j++;
          }
          const uint32_t run = (uint32_t)(j - i);

          if (r != curR || g != curG || b != curB) {
            if (use_background) {
              emit_set_truecolor_bg(&ob, r, g, b);
            } else {
              emit_set_truecolor_fg(&ob, r, g, b);
            }
            curR = r;
            curG = g;
            curB = b;
          }

          ob_putc(&ob, (char)ch);
          if (rep_is_profitable(run)) {
            emit_rep(&ob, run - 1);
          } else {
            for (uint32_t k = 1; k < run; k++) {
              ob_putc(&ob, (char)ch);
            }
          }
          i = j;
        }
      }
      x += 16;
    }

    // Scalar tail for remaining pixels
    for (; x < width;) {
      const rgb_pixel_t *p = &row[x];
      uint32_t R = p->r, G = p->g, B = p->b;
      uint8_t Y = (uint8_t)((77u * R + 150u * G + 29u * B + 128u) >> 8);
      uint8_t ch = ramp64[Y >> 2];

      if (use_256color) {
        // 256-color scalar tail
        uint8_t color_idx = rgb_to_256color((uint8_t)R, (uint8_t)G, (uint8_t)B);

        int j = x + 1;
        while (j < width) {
          const rgb_pixel_t *q = &row[j];
          uint32_t R2 = q->r, G2 = q->g, B2 = q->b;
          uint8_t Y2 = (uint8_t)((77u * R2 + 150u * G2 + 29u * B2 + 128u) >> 8);
          uint8_t color_idx2 = rgb_to_256color((uint8_t)R2, (uint8_t)G2, (uint8_t)B2);
          if (((Y2 >> 2) != (Y >> 2)) || color_idx2 != color_idx)
            break;
          j++;
        }
        uint32_t run = (uint32_t)(j - x);

        if (color_idx != cur_color_idx) {
          if (use_background) {
            emit_set_256_color_bg(&ob, color_idx);
          } else {
            emit_set_256_color_fg(&ob, color_idx);
          }
          cur_color_idx = color_idx;
        }

        ob_putc(&ob, (char)ch);
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; k++) {
            ob_putc(&ob, (char)ch);
          }
        }
        x = j;
      } else {
        // Truecolor scalar tail
        int j = x + 1;
        while (j < width) {
          const rgb_pixel_t *q = &row[j];
          uint32_t R2 = q->r, G2 = q->g, B2 = q->b;
          uint8_t Y2 = (uint8_t)((77u * R2 + 150u * G2 + 29u * B2 + 128u) >> 8);
          if (((Y2 >> 2) != (Y >> 2)) || R2 != R || G2 != G || B2 != B)
            break;
          j++;
        }
        uint32_t run = (uint32_t)(j - x);

        if ((int)R != curR || (int)G != curG || (int)B != curB) {
          if (use_background) {
            emit_set_truecolor_bg(&ob, (uint8_t)R, (uint8_t)G, (uint8_t)B);
          } else {
            emit_set_truecolor_fg(&ob, (uint8_t)R, (uint8_t)G, (uint8_t)B);
          }
          curR = (int)R;
          curG = (int)G;
          curB = (int)B;
        }

        ob_putc(&ob, (char)ch);
        if (rep_is_profitable(run)) {
          emit_rep(&ob, run - 1);
        } else {
          for (uint32_t k = 1; k < run; k++) {
            ob_putc(&ob, (char)ch);
          }
        }
        x = j;
      }
    }

    // End row: reset SGR, newline, invalidate color cache
    emit_reset_neon(&ob);
    ob_putc(&ob, '\n');
    curR = curG = curB = -1;
    cur_color_idx = -1;
  }

  ob_term(&ob);
  return ob.buf;
}

// CHATGPT OPTIMIZATION: Vectorized ASCII lookup using vqtbl2q_u8
// This processes 16 pixels at once instead of scalar loop
static inline uint8x16_t luma_to_ascii_vectorized(uint8x16_t luma_vec) {
  // Create 32-entry ASCII lookup table split across two vectors
  // Our palette: "   ...',;:clodxkO0KXNWM" (22 chars) -> spread across 32 slots
  static const uint8_t ascii_lut_0[16] = {
      ' ', ' ', ' ', '.', '.', '.', '\'', '\'', // Entries 0-7
      ',', ';', ':', 'c', 'l', 'o', 'd',  'x'   // Entries 8-15
  };
  static const uint8_t ascii_lut_1[16] = {
      'k', 'O', '0', 'K', 'X', 'N', 'W', 'M', // Entries 16-23
      'M', 'M', 'M', 'M', 'M', 'M', 'M', 'M'  // Entries 24-31 (repeat densest)
  };

  const uint8x16_t lut0 = vld1q_u8(ascii_lut_0);
  const uint8x16_t lut1 = vld1q_u8(ascii_lut_1);

  // Map luma 0-255 to index 0-31 (top 5 bits)
  uint8x16_t indices = vshrq_n_u8(luma_vec, 3); // Divide by 8: 256→32 bins

  // Vector table lookup: 16 ASCII characters computed in parallel!
  uint8x16x2_t combined_lut = {lut0, lut1};
  return vqtbl2q_u8(combined_lut, indices);
}

// CHATGPT OPTIMIZATION 1: Specialized functions eliminate per-pixel branching
static size_t convert_row_mono_neon(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width);

// OPTIMIZED MONOCHROME NEON: No color code at all - pure ASCII generation
static size_t convert_row_mono_neon(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width) {

  char *p = output_buffer;
  char *end = output_buffer + buffer_size;
  int x = 0;

  // CHATGPT OPTIMIZATION 2: Vectorized stores - process 16 pixels at once with st1
  for (; x + 15 < width && (end - p) >= 16; x += 16) {
    const uint8_t *rgb_data = (const uint8_t *)&pixels[x];

    // Load 16 RGB pixels
    uint8x16x3_t rgb_batch = vld3q_u8(rgb_data);

    // Vectorized luminance computation (16 pixels)
    uint16x8_t r_lo = vmovl_u8(vget_low_u8(rgb_batch.val[0]));
    uint16x8_t r_hi = vmovl_u8(vget_high_u8(rgb_batch.val[0]));
    uint16x8_t g_lo = vmovl_u8(vget_low_u8(rgb_batch.val[1]));
    uint16x8_t g_hi = vmovl_u8(vget_high_u8(rgb_batch.val[1]));
    uint16x8_t b_lo = vmovl_u8(vget_low_u8(rgb_batch.val[2]));
    uint16x8_t b_hi = vmovl_u8(vget_high_u8(rgb_batch.val[2]));

    uint16x8_t luma_lo = vmulq_n_u16(r_lo, LUMA_RED);
    luma_lo = vmlaq_n_u16(luma_lo, g_lo, LUMA_GREEN);
    luma_lo = vmlaq_n_u16(luma_lo, b_lo, LUMA_BLUE);
    luma_lo = vshrq_n_u16(luma_lo, 8);

    uint16x8_t luma_hi = vmulq_n_u16(r_hi, LUMA_RED);
    luma_hi = vmlaq_n_u16(luma_hi, g_hi, LUMA_GREEN);
    luma_hi = vmlaq_n_u16(luma_hi, b_hi, LUMA_BLUE);
    luma_hi = vshrq_n_u16(luma_hi, 8);

    // Pack luminance and convert to ASCII (vectorized)
    uint8x16_t luma_vec = vcombine_u8(vqmovn_u16(luma_lo), vqmovn_u16(luma_hi));
    uint8x16_t ascii_chars = luma_to_ascii_vectorized(luma_vec);

    // CHATGPT OPTIMIZATION 2: Vector store - 16 bytes in one shot!
    vst1q_u8((uint8_t *)p, ascii_chars);
    p += 16;
  }

  // Handle remaining pixels with scalar processing
  for (; x < width && (end - p) >= 1; x++) {
    const rgb_pixel_t *pixel = &pixels[x];
    uint8_t luma = (LUMA_RED * pixel->r + LUMA_GREEN * pixel->g + LUMA_BLUE * pixel->b) >> 8;
    *p++ = g_ascii_cache.luminance_palette[luma];
  }

  return p - output_buffer;
}

// Forward declaration for colored NEON
static size_t convert_row_colored_neon(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                       bool background_mode);

// Top-level dispatch - no per-pixel color branches inside hot loops
size_t convert_row_with_color_neon(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                   bool background_mode) {
  // CHATGPT INSIGHT: Duplicate 150-250 bytes of code is cheap compared to branch mispredicts
  if (opt_color_output || background_mode) {
    return convert_row_colored_neon(pixels, output_buffer, buffer_size, width, background_mode);
  } else {
    return convert_row_mono_neon(pixels, output_buffer, buffer_size, width);
  }
}

// OPTIMIZED COLORED NEON: Specialized for color output only
static size_t convert_row_colored_neon(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                       bool background_mode) {

  char *p = output_buffer;
  const char *buffer_end = output_buffer + buffer_size - 1;

  // NEON weight constants (same as mono code)
  const uint8x8_t wR = vdup_n_u8(LUMA_RED), wG = vdup_n_u8(LUMA_GREEN), wB = vdup_n_u8(LUMA_BLUE);

  // ASCII lookup tables (same as mono code)
  const uint8x16_t lut0 = vld1q_u8((const uint8_t *)" ...',;:clodxk");
  const uint8x16_t lut1 = vld1q_u8((const uint8_t *)"O0KXNWMMMMMMMMM");

  // Process 16 pixels at a time (same as mono code)
  int processed = 0;
  while (processed + 16 <= width && p < buffer_end - 1000) { // Leave room for ANSI sequences

    // Load interleaved RGB (same as mono code)
    uint8x16x3_t rgb = vld3q_u8((const uint8_t *)(pixels + processed));

    // Compute luminance: (77*R + 150*G + 29*B) >> 8 (same formula as mono code)
    uint16x8_t y0 = vshrq_n_u16(vmlaq_u16(vmlaq_u16(vmulq_u16(vmovl_u8(vget_low_u8(rgb.val[0])), vmovl_u8(wR)),
                                                    vmovl_u8(vget_low_u8(rgb.val[1])), vmovl_u8(wG)),
                                          vmovl_u8(vget_low_u8(rgb.val[2])), vmovl_u8(wB)),
                                8);
    uint16x8_t y1 = vshrq_n_u16(vmlaq_u16(vmlaq_u16(vmulq_u16(vmovl_u8(vget_high_u8(rgb.val[0])), vmovl_u8(wR)),
                                                    vmovl_u8(vget_high_u8(rgb.val[1])), vmovl_u8(wG)),
                                          vmovl_u8(vget_high_u8(rgb.val[2])), vmovl_u8(wB)),
                                8);
    uint8x16_t y = vcombine_u8(vqmovn_u16(y0), vqmovn_u16(y1));

    // ASCII lookup (same as mono code)
    uint8x16_t idx = vshrq_n_u8(y, 3); // 0..31
    uint8x16x2_t L = {lut0, lut1};
    uint8x16_t ascii_chars = vqtbl2q_u8(L, idx);

    // PERFORMANCE FIX: Store SIMD results and emit per-pixel (simplest approach)
    uint8_t ascii_batch[16] __attribute__((aligned(16)));
    uint8_t r_batch[16] __attribute__((aligned(16)));
    uint8_t g_batch[16] __attribute__((aligned(16)));
    uint8_t b_batch[16] __attribute__((aligned(16)));
    uint8_t luma_batch[16] __attribute__((aligned(16)));

    vst1q_u8(ascii_batch, ascii_chars);
    vst1q_u8(r_batch, rgb.val[0]);
    vst1q_u8(g_batch, rgb.val[1]);
    vst1q_u8(b_batch, rgb.val[2]);
    vst1q_u8(luma_batch, y);

    // Simple per-pixel emission - no complex quantization or hysteresis for now
    for (int k = 0; k < 16; k++) {
      uint8_t r = r_batch[k], g = g_batch[k], b = b_batch[k];
      if (background_mode) {
        uint8_t luma = luma_batch[k];              // Use SIMD-computed luminance
        uint8_t fg_color = (luma < 127) ? 255 : 0; // Simple threshold
        p = append_sgr_truecolor_fg_bg(p, fg_color, fg_color, fg_color, r, g, b);
      } else {
        p = append_sgr_truecolor_fg(p, r, g, b);
      }
      *p++ = ascii_batch[k];
      if (p >= buffer_end - 100)
        break;
    }
    processed += 16;
  }

  // Handle remaining pixels (< 16) with scalar processing
  for (int x = processed; x < width; x++) {
    const rgb_pixel_t *pixel = &pixels[x];
    // Scalar luminance calculation (same formula as NEON)
    uint16_t luma_16 = (LUMA_RED * pixel->r + LUMA_GREEN * pixel->g + LUMA_BLUE * pixel->b);
    uint8_t luma = (uint8_t)(luma_16 >> 8);
    // Use global ASCII cache like other parts of the codebase
    char ascii_char = g_ascii_cache.luminance_palette[luma];

    // Color sequences (same optimization as NEON loop)
    if (background_mode) {
      // FIX D: Scalar fallback also needs proper contrasting FG with hysteresis
      uint8_t luma = (LUMA_RED * pixel->r + LUMA_GREEN * pixel->g + LUMA_BLUE * pixel->b) >> 8;
      static uint8_t last_scalar_fg = 255;
      uint8_t fg_color = last_scalar_fg;
      if (luma < 110)
        fg_color = 255; // White on dark
      else if (luma > 145)
        fg_color = 0; // Black on light
      // else keep last_scalar_fg (hysteresis prevents flickering)
      last_scalar_fg = fg_color;
      p = append_sgr_truecolor_fg_bg(p, fg_color, fg_color, fg_color, pixel->r, pixel->g, pixel->b);
    } else {
      p = append_sgr_truecolor_fg(p, pixel->r, pixel->g, pixel->b);
    }
    *p++ = ascii_char;
    if (p >= buffer_end - 100)
      break;
  }

  return p - output_buffer;
}
#endif // SIMD_SUPPORT_NEON
