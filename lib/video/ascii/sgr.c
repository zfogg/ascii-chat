/** @file video/ascii/sgr.c
 * Shared ANSI SGR encoders for scalar, SIMD, and compatibility entry points.
 */
#include <ascii-chat/common.h>
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/ascii/sgr.h>
#include <ascii-chat/util/number.h>
#include <string.h>

/**
 * @brief Pre-computed 256-color ANSI SGR sequence
 */
typedef struct {
  char seq[12]; ///< ANSI sequence string (max 11 bytes for "\e[38;5;NNNm")
  uint8_t len;  ///< Length of sequence string
} sgr256_seq_t;

static _Thread_local sgr256_seq_t sgr256_fg_cache[256];
static _Thread_local sgr256_seq_t sgr256_bg_cache[256];
static _Thread_local bool sgr256_cache_initialized = false;

// Build and cache all 256 foreground and background color sequences
static void init_sgr256_cache(void) {
  if (sgr256_cache_initialized)
    return;

  // Cache foreground colors: \e[38;5;NNNm
  for (int i = 0; i < 256; i++) {
    char *p = sgr256_fg_cache[i].seq;
    *p++ = '\033';
    *p++ = '[';
    *p++ = '3';
    *p++ = '8';
    *p++ = ';';
    *p++ = '5';
    *p++ = ';';
    p = write_u8(p, (uint8_t)i);
    *p++ = 'm';
    sgr256_fg_cache[i].len = (uint8_t)(p - sgr256_fg_cache[i].seq);
  }

  // Cache background colors: \e[48;5;NNNm
  for (int i = 0; i < 256; i++) {
    char *p = sgr256_bg_cache[i].seq;
    *p++ = '\033';
    *p++ = '[';
    *p++ = '4';
    *p++ = '8';
    *p++ = ';';
    *p++ = '5';
    *p++ = ';';
    p = write_u8(p, (uint8_t)i);
    *p++ = 'm';
    sgr256_bg_cache[i].len = (uint8_t)(p - sgr256_bg_cache[i].seq);
  }

  sgr256_cache_initialized = true;
}

// Generate "\e[38;5;NNN;48;5;NNNm" (foreground + background)
static inline char *build_sgr256_fgbg(char *buf, uint8_t fg, uint8_t bg, uint8_t *len_out) {
  char *p = buf;
  *p++ = '\033';
  *p++ = '[';
  *p++ = '3';
  *p++ = '8';
  *p++ = ';';
  *p++ = '5';
  *p++ = ';';
  p = write_u8(p, fg);
  *p++ = ';';
  *p++ = '4';
  *p++ = '8';
  *p++ = ';';
  *p++ = '5';
  *p++ = ';';
  p = write_u8(p, bg);
  *p++ = 'm';
  *len_out = (uint8_t)(p - buf);
  return buf;
}

// Public API wrappers
void prewarm_sgr256_fg_cache(void) {
  init_sgr256_cache();
}

void prewarm_sgr256_cache(void) {
  init_sgr256_cache();
}

// Fast SGR generation for SIMD implementations - uses cached sequences
char *get_sgr256_fg_string(uint8_t fg, uint8_t *len_out) {
  if (!sgr256_cache_initialized) {
    init_sgr256_cache();
  }
  *len_out = sgr256_fg_cache[fg].len;
  return sgr256_fg_cache[fg].seq;
}

char *get_sgr256_bg_string(uint8_t bg, uint8_t *len_out) {
  if (!sgr256_cache_initialized) {
    init_sgr256_cache();
  }
  *len_out = sgr256_bg_cache[bg].len;
  return sgr256_bg_cache[bg].seq;
}

char *get_sgr256_fg_bg_string(uint8_t fg, uint8_t bg, uint8_t *len_out) {
  // For FG+BG, still build on-demand since we'd need 256*256 cache
  static __thread char buf[32];
  return build_sgr256_fgbg(buf, fg, bg, len_out);
}

char *append_sgr_reset(char *dst) {
  memcpy(dst, "\033[0m", 4);
  return dst + 4;
}

static char *append_rgb_components(char *dst, uint8_t r, uint8_t g, uint8_t b) {
  dst = write_u8(dst, r);
  *dst++ = ';';
  dst = write_u8(dst, g);
  *dst++ = ';';
  return write_u8(dst, b);
}

static char *append_rgb_sgr(char *dst, bool background, uint8_t r, uint8_t g, uint8_t b) {
  memcpy(dst, background ? "\033[48;2;" : "\033[38;2;", 7);
  dst = append_rgb_components(dst + 7, r, g, b);
  *dst++ = 'm';
  return dst;
}

char *append_sgr_truecolor_fg(char *dst, uint8_t r, uint8_t g, uint8_t b) {
  return append_rgb_sgr(dst, false, r, g, b);
}

char *append_sgr_truecolor_bg(char *dst, uint8_t r, uint8_t g, uint8_t b) {
  return append_rgb_sgr(dst, true, r, g, b);
}

char *append_sgr_truecolor_fg_bg(char *dst, uint8_t fr, uint8_t fg, uint8_t fb, uint8_t br, uint8_t bg, uint8_t bb) {
  memcpy(dst, "\033[38;2;", 7);
  dst = append_rgb_components(dst + 7, fr, fg, fb);
  memcpy(dst, ";48;2;", 6);
  dst = append_rgb_components(dst + 6, br, bg, bb);
  *dst++ = 'm';
  return dst;
}

char *image_print_color_simd(image_t *image, bool background, bool indexed, const char *palette) {
  return ascii_render(image, palette, indexed ? ASCII_COLOR_256 : ASCII_COLOR_TRUE, background, false,
                      simd_backend()->luminance);
}
