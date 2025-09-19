#include "image2ascii/output_buffer.h"

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
  if (ob->cap == 0) {
    // Always allocate at least default capacity on first call
    size_t ncap = 4096;
    while (ncap < ob->len + need)
      ncap = (ncap * 3) / 2;
    SAFE_REALLOC(ob->buf, ncap, char *);
    ob->cap = ncap;
  } else if (ob->len + need > ob->cap) {
    // Expand existing buffer
    size_t ncap = ob->cap;
    while (ncap < ob->len + need)
      ncap = (ncap * 3) / 2;
    SAFE_REALLOC(ob->buf, ncap, char *);
    ob->cap = ncap;
  }
}

void ob_putc(outbuf_t *ob, char c) {
  if (!ob)
    return;
  ob_reserve(ob, 1);
  ob->buf[ob->len++] = c;
}

void ob_write(outbuf_t *ob, const char *s, size_t n) {
  if (!ob)
    return;
  ob_reserve(ob, n);
  SAFE_MEMCPY(ob->buf + ob->len, n, s, n);
  ob->len += n;
}

void ob_term(outbuf_t *ob) {
  if (!ob)
    return;
  ob_putc(ob, '\0');
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

// Truecolor SGR emission (foreground)
void emit_set_truecolor_fg(outbuf_t *ob, uint8_t r, uint8_t g, uint8_t b) {
  if (!ob)
    return;
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
void emit_set_truecolor_bg(outbuf_t *ob, uint8_t r, uint8_t g, uint8_t b) {
  if (!ob)
    return;
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

void emit_reset(outbuf_t *ob) {
  if (!ob)
    return;
  ob_putc(ob, 0x1b);
  ob_putc(ob, '[');
  ob_putc(ob, '0');
  ob_putc(ob, 'm');
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

// 256-color palette mapping (RGB to ANSI 256 color index)
static uint8_t __attribute__((unused)) rgb_to_256color(uint8_t r, uint8_t g, uint8_t b) {
  // Use existing project function for consistency
  return (uint8_t)(16 + 36 * (r / 51) + 6 * (g / 51) + (b / 51));
}

// 256-color SGR emission (foreground)
void emit_set_256_color_fg(outbuf_t *ob, uint8_t color_idx) {
  if (!ob)
    return;
  // ESC[38;5;Nm
  ob_putc(ob, 0x1b);
  ob_putc(ob, '[');
  ob_write(ob, "38;5;", 5);
  ob_u8(ob, color_idx);
  ob_putc(ob, 'm');
}

// 256-color SGR emission (background)
void emit_set_256_color_bg(outbuf_t *ob, uint8_t color_idx) {
  if (!ob)
    return;
  // ESC[48;5;Nm
  ob_putc(ob, 0x1b);
  ob_putc(ob, '[');
  ob_write(ob, "48;5;", 5);
  ob_u8(ob, color_idx);
  ob_putc(ob, 'm');
}

void emit_set_fg(outbuf_t *ob, uint8_t r, uint8_t g, uint8_t b) {
  if (!ob)
    return;
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

void emit_set_bg(outbuf_t *ob, uint8_t r, uint8_t g, uint8_t b) {
  if (!ob)
    return;
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
