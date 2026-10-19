/**
 * @file video/output_buffer.c
 * @ingroup video
 * @brief 📝 Output buffer helpers for efficient string building in ASCII rendering pipeline
 */

#include <ascii-chat/video/ascii/output_buffer.h>
#include <ascii-chat/common.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/ascii/sgr.h>

/*****************************************************************************/
// char* output buffer helpers
//
// ob_* - build strings with an output buffer
// ob_reserve - reserve space in the output buffer
// ob_putc - append a single character to the output buffer
// ob_write - append a string to the output buffer
// ob_term - terminate the output buffer
// ob_u8 - append a decimal number to the output buffer
// ob_u32 - append a decimal number to the output buffer

void ob_reserve(outbuf_t *ob, size_t need) {
  if (!ob)
    return;
  if (need > SIZE_MAX - ob->len)
    FATAL(ERROR_MEMORY, "Output buffer size overflow");
  size_t required = ob->len + need;
  if (ob->cap && required <= ob->cap)
    return;
  size_t capacity = ob->cap ? ob->cap : 4096;
  while (capacity < required) {
    size_t increment = capacity / 2 + 1;
    if (capacity > SIZE_MAX - increment) {
      capacity = required;
      break;
    }
    capacity += increment;
  }
  ob->buf = SAFE_REALLOC(ob->buf, capacity, char *);
  ob->cap = capacity;
}

void ob_putc(outbuf_t *ob, char c) {
  if (!ob)
    return;
  ob_reserve(ob, 1);
  ob->buf[ob->len++] = c;
}

void ob_write(outbuf_t *ob, const void *s, size_t n) {
  if (!ob || n == 0)
    return;
  ob_reserve(ob, n);
  SAFE_MEMCPY(ob->buf + ob->len, ob->cap - ob->len, s, n);
  ob->len += n;
}

void ob_term(outbuf_t *ob) {
  if (!ob)
    return;
  // Reserve space for null terminator if needed
  if (ob->len >= ob->cap) {
    ob_reserve(ob, 1);
  }
  // Append null terminator WITHOUT incrementing length
  // (null terminator is not part of the string length)
  ob->buf[ob->len] = '\0';
}

// Fast decimal for uint8_t (0..255)
void ob_u8(outbuf_t *ob, uint8_t v) {
  if (!ob)
    return;
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

void ob_u32(outbuf_t *ob, uint32_t v) {
  if (!ob)
    return;
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

// Reserve once per SGR sequence; no ISA-specific encoding is necessary.
static void emit_truecolor(outbuf_t *out, bool background, uint8_t r, uint8_t g, uint8_t b) {
  if (!out)
    return;
  ob_reserve(out, 19);
  char *start = out->buf + out->len;
  char *p = background ? append_sgr_truecolor_bg(start, r, g, b) : append_sgr_truecolor_fg(start, r, g, b);
  out->len += (size_t)(p - start);
}

void emit_set_truecolor_fg(outbuf_t *out, uint8_t r, uint8_t g, uint8_t b) {
  emit_truecolor(out, false, r, g, b);
}

void emit_set_truecolor_bg(outbuf_t *out, uint8_t r, uint8_t g, uint8_t b) {
  emit_truecolor(out, true, r, g, b);
}

void emit_reset(outbuf_t *out) {
  if (!out)
    return;
  ob_reserve(out, 4);
  append_sgr_reset(out->buf + out->len);
  out->len += 4;
}

// REP profitability calculation
bool rep_is_profitable(uint32_t runlen) {
  if (runlen <= 2)
    return false;
  uint32_t k = runlen - 1;                           // Extra repetitions beyond the first character
  uint32_t rep_cost = (uint32_t)(digits_u32(k) + 3); // ESC [ digits b
  return k > rep_cost;                               // Manual repetition cost vs REP cost
}

void emit_rep(outbuf_t *ob, uint32_t extra) {
  if (!ob)
    return;
  // ESC [ extra b
  ob_putc(ob, 0x1b);
  ob_putc(ob, '[');
  ob_u32(ob, extra);
  ob_putc(ob, 'b');
}

// 256-color SGR emission (foreground) - uses cached sequences for performance
void emit_set_256_color_fg(outbuf_t *ob, uint8_t color_idx) {
  if (!ob)
    return;
  // Use pre-cached color sequence instead of building on-demand
  uint8_t seq_len;
  const char *seq = get_sgr256_fg_string(color_idx, &seq_len);
  ob_write(ob, seq, seq_len);
}

// 256-color SGR emission (background) - uses cached sequences for performance
void emit_set_256_color_bg(outbuf_t *ob, uint8_t color_idx) {
  if (!ob)
    return;
  // Use pre-cached color sequence instead of building on-demand
  uint8_t seq_len;
  const char *seq = get_sgr256_bg_string(color_idx, &seq_len);
  ob_write(ob, seq, seq_len);
}

void emit_set_fg(outbuf_t *ob, uint8_t r, uint8_t g, uint8_t b) {
  emit_set_truecolor_fg(ob, r, g, b);
}

void emit_set_bg(outbuf_t *ob, uint8_t r, uint8_t g, uint8_t b) {
  emit_set_truecolor_bg(ob, r, g, b);
}

void emit_glyph_run(outbuf_t *out, const void *glyph, size_t bytes, uint32_t count) {
  if (!count || !bytes)
    return;
  ob_write(out, glyph, bytes);
  uint32_t extra = count - 1;
  // Compare division rather than multiplying count by glyph size (overflow).
  size_t cost = digits_u32(extra) + 3u;
  if (extra > cost / bytes) {
    emit_rep(out, extra);
  } else {
    for (uint32_t i = 0; i < extra; ++i)
      ob_write(out, glyph, bytes);
  }
}

void emit_row_end(outbuf_t *out, bool colored, bool newline) {
  if (colored)
    emit_reset(out);
  if (newline)
    ob_putc(out, '\n');
}
