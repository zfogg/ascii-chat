#pragma once

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "common.h"
#include "image.h"
#include "ascii_simd.h"

// Forward declarations for architecture-specific implementations
typedef rgb_t rgb_pixel_t;

// Dynamic output buffer (auto-expanding)
typedef struct {
  char *buf;
  size_t len;
  size_t cap;
} outbuf_t;

// Buffer management functions
void ob_reserve(outbuf_t *ob, size_t need);
void ob_putc(outbuf_t *ob, char c);
void ob_write(outbuf_t *ob, const char *s, size_t n);
void ob_term(outbuf_t *ob);
void ob_u8(outbuf_t *ob, uint8_t v);
void ob_u32(outbuf_t *ob, uint32_t v);

#define RAMP64_SIZE 64
void build_ramp64(uint8_t ramp64[RAMP64_SIZE], const char *ascii_chars);

// ANSI escape sequence emission
void emit_set_truecolor_fg(outbuf_t *ob, uint8_t r, uint8_t g, uint8_t b);
void emit_set_truecolor_bg(outbuf_t *ob, uint8_t r, uint8_t g, uint8_t b);
// rgb_to_256color is now static in common.c
void emit_set_256_color_fg(outbuf_t *ob, uint8_t color_idx);
void emit_set_256_color_bg(outbuf_t *ob, uint8_t color_idx);
void emit_reset(outbuf_t *ob);
bool rep_is_profitable(uint32_t runlen);
void emit_rep(outbuf_t *ob, uint32_t extra);
void emit_set_fg(outbuf_t *ob, uint8_t r, uint8_t g, uint8_t b);
void emit_set_bg(outbuf_t *ob, uint8_t r, uint8_t g, uint8_t b);

// Row-based functions removed - use image-based API instead
// See individual architecture headers (sse2.h, ssse3.h, avx2.h, neon.h) for new image-based functions

// Architecture-specific capability detection
bool has_sse2_support(void);
bool has_ssse3_support(void);
bool has_avx2_support(void);
bool has_neon_support(void);
bool has_sve_support(void);

// ANSI color sequence generation functions (defined in ascii_simd_color.c)
char *append_sgr_truecolor_fg(char *dst, uint8_t r, uint8_t g, uint8_t b);
char *append_sgr_truecolor_bg(char *dst, uint8_t r, uint8_t g, uint8_t b);
char *append_sgr_truecolor_fg_bg(char *dst, uint8_t fr, uint8_t fg, uint8_t fb, uint8_t br, uint8_t bg, uint8_t bb);
char *append_sgr_reset(char *dst);

// Helper: write decimal RGB triplet using dec3 cache
static inline size_t write_rgb_triplet(uint8_t value, char *dst) {
  const dec3_t *d = &g_dec3_cache.dec3_table[value];
  memcpy(dst, d->s, d->len);
  return d->len;
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
