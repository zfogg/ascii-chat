#include "common.h"
#include "image2ascii/simd/common.h"
#include "ascii_simd.h"

// Build 64-entry glyph LUT for vqtbl4q_u8 and other architecture's instrinsics (UTF-8 aware)
void build_ramp64(uint8_t ramp64[RAMP64_SIZE], const char *ascii_chars) {
  if (!ascii_chars) {
    // Fallback to space character
    for (int i = 0; i < RAMP64_SIZE; i++) {
      ramp64[i] = ' ';
    }
    return;
  }

  // Build character boundary map for UTF-8 support
  // First, find all character start positions
  int char_starts[256]; // More than enough for any reasonable palette
  int char_count = 0;

  const char *p = ascii_chars;
  while (*p && char_count < 255) {
    char_starts[char_count] = (int)(p - ascii_chars); // Byte offset of this character
    char_count++;

    // Skip to next UTF-8 character
    if ((*p & 0x80) == 0) {
      // ASCII character (1 byte)
      p++;
    } else if ((*p & 0xE0) == 0xC0) {
      // 2-byte UTF-8 character
      p += 2;
    } else if ((*p & 0xF0) == 0xE0) {
      // 3-byte UTF-8 character
      p += 3;
    } else if ((*p & 0xF8) == 0xF0) {
      // 4-byte UTF-8 character
      p += 4;
    } else {
      // Invalid UTF-8, skip 1 byte
      p++;
    }
  }

  if (char_count == 0) {
    // No valid characters found, use space
    for (int i = 0; i < RAMP64_SIZE; i++) {
      ramp64[i] = ' ';
    }
    return;
  }

  // Now build the ramp64 lookup using character indices, not byte indices
  for (int i = 0; i < RAMP64_SIZE; i++) {
    // Map 0-63 to 0-(char_count-1) using proper character indexing
    int char_idx = (i * (char_count - 1) + (RAMP64_SIZE - 1) / 2) / (RAMP64_SIZE - 1);
    if (char_idx >= char_count) {
      char_idx = char_count - 1;
    }

    // Get the first byte of the character at this character index
    int byte_offset = char_starts[char_idx];
    ramp64[i] = (uint8_t)ascii_chars[byte_offset];
  }
}

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

void ob_putc(outbuf_t *ob, char c) {
  ob_reserve(ob, 1);
  ob->buf[ob->len++] = c;
}

void ob_write(outbuf_t *ob, const char *s, size_t n) {
  ob_reserve(ob, n);
  memcpy(ob->buf + ob->len, s, n);
  ob->len += n;
}

void ob_term(outbuf_t *ob) {
  ob_putc(ob, '\0');
}

// Fast decimal for uint8_t (0..255)
void ob_u8(outbuf_t *ob, uint8_t v) {
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
  ob_putc(ob, 0x1b);
  ob_putc(ob, '[');
  ob_putc(ob, '0');
  ob_putc(ob, 'm');
}

// REP profitability calculation
bool rep_is_profitable(uint32_t runlen) {
  if (runlen <= 1)
    return false;
  uint32_t k = runlen - 1;
  return k > (uint32_t)(digits_u32(k) + 3);
}

void emit_rep(outbuf_t *ob, uint32_t extra) {
  // ESC [ extra b
  ob_putc(ob, 0x1b);
  ob_putc(ob, '[');
  ob_u32(ob, extra);
  ob_putc(ob, 'b');
}

// 256-color palette mapping (RGB to ANSI 256 color index)
static uint8_t rgb_to_256color(uint8_t r, uint8_t g, uint8_t b) {
  // Use existing project function for consistency
  return (uint8_t)(16 + 36 * (r / 51) + 6 * (g / 51) + (b / 51));
}

// 256-color SGR emission (foreground)
void emit_set_256_color_fg(outbuf_t *ob, uint8_t color_idx) {
  // ESC[38;5;Nm
  ob_putc(ob, 0x1b);
  ob_putc(ob, '[');
  ob_write(ob, "38;5;", 5);
  ob_u8(ob, color_idx);
  ob_putc(ob, 'm');
}

// 256-color SGR emission (background)
void emit_set_256_color_bg(outbuf_t *ob, uint8_t color_idx) {
  // ESC[48;5;Nm
  ob_putc(ob, 0x1b);
  ob_putc(ob, '[');
  ob_write(ob, "48;5;", 5);
  ob_u8(ob, color_idx);
  ob_putc(ob, 'm');
}

void emit_set_fg(outbuf_t *ob, uint8_t r, uint8_t g, uint8_t b) {
  // ESC[38;2;R;G;Bm
  ob->buf ? (void)0 : (void)0; // hint: ob_* already handles reserve
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
