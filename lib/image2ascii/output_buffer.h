#pragma once

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "simd/common.h"

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

// ANSI escape sequence emission
void emit_set_truecolor_fg(outbuf_t *ob, uint8_t r, uint8_t g, uint8_t b);
void emit_set_truecolor_bg(outbuf_t *ob, uint8_t r, uint8_t g, uint8_t b);
void emit_set_256_color_fg(outbuf_t *ob, uint8_t color_idx);
void emit_set_256_color_bg(outbuf_t *ob, uint8_t color_idx);
void emit_reset(outbuf_t *ob);
bool rep_is_profitable(uint32_t runlen);
void emit_rep(outbuf_t *ob, uint32_t extra);
void emit_set_fg(outbuf_t *ob, uint8_t r, uint8_t g, uint8_t b);
void emit_set_bg(outbuf_t *ob, uint8_t r, uint8_t g, uint8_t b);

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
