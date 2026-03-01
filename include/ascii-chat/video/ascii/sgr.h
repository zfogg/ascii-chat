#pragma once

/**
 * @file video/ascii/sgr.h
 * @brief ANSI SGR (Select Graphic Rendition) color sequence generation
 * @ingroup video
 */

#include <stdint.h>
#include <stddef.h>

// SGR cache initialization
void prewarm_sgr256_fg_cache(void);
void prewarm_sgr256_cache(void);

// 256-color SGR generation
char *get_sgr256_fg_string(uint8_t fg, uint8_t *len_out);
char *get_sgr256_bg_string(uint8_t bg, uint8_t *len_out);
char *get_sgr256_fg_bg_string(uint8_t fg, uint8_t bg, uint8_t *len_out);

// Truecolor SGR generation
char *append_sgr_reset(char *dst);
char *append_sgr_truecolor_fg(char *dst, uint8_t r, uint8_t g, uint8_t b);
char *append_sgr_truecolor_bg(char *dst, uint8_t r, uint8_t g, uint8_t b);
char *append_sgr_truecolor_fg_bg(char *dst, uint8_t fr, uint8_t fg, uint8_t fb, uint8_t br, uint8_t bg, uint8_t bb);
