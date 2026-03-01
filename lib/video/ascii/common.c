/**
 * @file video/ascii/common.c
 * @ingroup video
 * @brief Common ASCII rendering utilities and initialization
 */

#include <string.h>
#include <ascii-chat/common.h>
#include <ascii-chat/video/palette.h>
#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/log/log.h>

// Global dec3 cache for fast 3-digit decimal conversion
global_dec3_cache_t g_dec3_cache = {.dec3_initialized = false};

/**
 * @brief Initialize dec3 cache for fast decimal-to-ASCII conversion
 * Used for RGB values in SGR color sequences
 */
void init_dec3(void) {
  if (g_dec3_cache.dec3_initialized)
    return;

  for (int v = 0; v < 256; ++v) {
    int d2 = v / 100;     // 0..2
    int r = v - d2 * 100; // 0..99
    int d1 = r / 10;      // 0..9
    int d0 = r - d1 * 10; // 0..9

    if (d2) {
      g_dec3_cache.dec3_table[v].len = 3;
      g_dec3_cache.dec3_table[v].s[0] = '0' + d2;
      g_dec3_cache.dec3_table[v].s[1] = '0' + d1;
      g_dec3_cache.dec3_table[v].s[2] = '0' + d0;
    } else if (d1) {
      g_dec3_cache.dec3_table[v].len = 2;
      g_dec3_cache.dec3_table[v].s[0] = '0' + d1;
      g_dec3_cache.dec3_table[v].s[1] = '0' + d0;
    } else {
      g_dec3_cache.dec3_table[v].len = 1;
      g_dec3_cache.dec3_table[v].s[0] = '0' + d0;
    }
  }
  g_dec3_cache.dec3_initialized = true;
}

// Default luminance palette for legacy functions
char g_default_luminance_palette[256];
static lifecycle_t g_default_palette_lc = LIFECYCLE_INIT;

static void do_init_default_luminance_palette(void) {
  const size_t len = DEFAULT_ASCII_PALETTE_LEN;
  for (int i = 0; i < 256; i++) {
    size_t palette_index = (i * (len - 1) + 127) / 255;
    if (palette_index >= len) {
      palette_index = len - 1;
    }
    g_default_luminance_palette[i] = DEFAULT_ASCII_PALETTE[palette_index];
  }
}

void init_default_luminance_palette(void) {
  if (!lifecycle_init(&g_default_palette_lc, "default_palette")) {
    return;
  }
  do_init_default_luminance_palette();
}

void ascii_init(void) {
  init_dec3();
  init_default_luminance_palette();
}
