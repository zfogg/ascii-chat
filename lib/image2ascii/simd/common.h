#pragma once

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "common.h"
#include "image.h"
#include "ascii_simd.h"
#include "image2ascii/output_buffer.h"

// Forward declarations for architecture-specific implementations
typedef rgb_t rgb_pixel_t;

#define RAMP64_SIZE 64
void build_ramp64(uint8_t ramp64[RAMP64_SIZE], const char *ascii_chars);

// UTF-8 character cache system for SIMD renderers
typedef struct {
  char utf8_bytes[4]; // Up to 4 bytes for UTF-8 character
  uint8_t byte_len;   // Actual length (1-4 bytes)
} utf8_char_t;

typedef struct {
  utf8_char_t cache[256];      // 256-entry cache for direct luminance lookup (monochrome)
  utf8_char_t cache64[64];     // 64-entry cache for SIMD color lookup
  uint8_t char_index_ramp[64]; // Character indices for vqtbl4q_u8 lookup
  char palette_hash[64];       // Hash of palette for cache validation
  bool is_valid;               // Whether cache is valid
} utf8_palette_cache_t;

// UTF-8 palette cache functions
utf8_palette_cache_t *get_utf8_palette_cache(const char *ascii_chars);
void build_utf8_luminance_cache(const char *ascii_chars, utf8_char_t cache[256]);
void build_utf8_ramp64_cache(const char *ascii_chars, utf8_char_t cache64[64], uint8_t char_index_ramp[64]);

// ANSI escape sequence emission functions now in output_buffer.h

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

// Fast decimal for REP counts - now in output_buffer.h
