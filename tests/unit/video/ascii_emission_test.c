#ifdef ASCII_EMISSION_STANDALONE
#include "../../simd/emission_assert.h"
#else
#include <criterion/criterion.h>
#endif
#include <ascii-chat/common.h>
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/ascii/ascii.h>
#include <ascii-chat/video/ascii/sgr.h>
#include <ascii-chat/video/ascii/simd/cells.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/ascii/output_buffer.h>
#include <ascii-chat/video/ascii/scalar/foreground.h>
#include <ascii-chat/video/ascii/scalar/background.h>
#include <ascii-chat/video/ascii/scalar/halfblock.h>
#include <ascii-chat/video/terminal/ansi.h>
#include <string.h>
#if defined(ASCII_EMISSION_STANDALONE) && defined(__aarch64__)
#include <sys/prctl.h>
#endif

#if SIMD_SUPPORT_SSE2
#include <ascii-chat/video/ascii/sse2.h>
#endif
#if SIMD_SUPPORT_SSSE3
#include <ascii-chat/video/ascii/ssse3.h>
#endif
#if SIMD_SUPPORT_AVX2
#include <ascii-chat/video/ascii/avx2.h>
#endif
#if SIMD_SUPPORT_NEON
#include <ascii-chat/video/ascii/neon.h>
#endif
#if SIMD_SUPPORT_SVE
#include <ascii-chat/video/ascii/sve.h>
#endif

typedef char *(*mono_fn)(const image_t *, const char *);
typedef char *(*color_fn)(const image_t *, bool, bool, const char *);
static const struct {
  const char *name;
  uint32_t feature;
  const simd_cells_t *luma;
  mono_fn mono;
  color_fn color;
} backends[] = {
    {"scalar", 0, &simd_cells_scalar, NULL, NULL},
#if SIMD_SUPPORT_SSE2
    {"sse2", CPU_FEATURE_SSE2, &simd_cells_sse2, render_ascii_mono_sse2, render_ascii_color_sse2},
#endif
#if SIMD_SUPPORT_SSSE3
    {"ssse3", CPU_FEATURE_SSSE3, &simd_cells_ssse3, render_ascii_mono_ssse3, render_ascii_color_ssse3},
#endif
#if SIMD_SUPPORT_AVX2
    {"avx2", CPU_FEATURE_AVX2, &simd_cells_avx2, render_ascii_mono_avx2, render_ascii_color_avx2},
#endif
#if SIMD_SUPPORT_NEON
    {"neon", CPU_FEATURE_NEON, &simd_cells_neon, render_ascii_mono_neon, render_ascii_color_neon},
#endif
#if SIMD_SUPPORT_SVE
    {"sve", CPU_FEATURE_SVE, &simd_cells_sve, render_ascii_mono_sve, render_ascii_color_sve},
#endif
};

typedef struct {
  char glyph[4];
  unsigned bytes;
  int fg, bg;
} terminal_cell_t;

// Independent interpreter for the emitted subset of CSI SGR and REP.
// It checks row geometry and state as well as complete UTF-8 glyphs.
static void decode(const char *text, terminal_cell_t *cells, int width, int height, bool colored) {
  int x = 0, y = 0, fg = -1, bg = -1;
  terminal_cell_t last = {0};
  const unsigned char *p = (const unsigned char *)text;
  while (*p) {
    if (*p == '\n') {
      cr_assert_eq(x, width);
      cr_assert_eq(fg, -1, "foreground must reset before newline");
      cr_assert_eq(bg, -1, "background must reset before newline");
      x = 0;
      ++y;
      ++p;
      continue;
    }
    if (*p == 27) {
      cr_assert(colored || p[1] == '[', "invalid escape");
      cr_assert_eq(*++p, '[');
      ++p;
      int args[16] = {0};
      unsigned n = 0;
      while ((*p >= '0' && *p <= '9') || *p == ';') {
        if (*p == ';') {
          cr_assert_lt(++n, 16);
        } else {
          args[n] = args[n] * 10 + *p - '0';
        }
        ++p;
      }
      ++n;
      if (*p == 'b') {
        cr_assert(last.bytes);
        for (int i = 0; i < args[0]; ++i) {
          cr_assert_lt(x, width, "REP crossed row boundary");
          cr_assert_lt(y, height);
          last.fg = fg;
          last.bg = bg;
          cells[y * width + x++] = last;
        }
      } else {
        cr_assert_eq(*p, 'm', "unexpected CSI final");
        cr_assert(colored, "monochrome output contains SGR");
        for (unsigned i = 0; i < n; ++i) {
          int code = args[i];
          if (code == 0) {
            fg = bg = -1;
          } else if (code == 38 || code == 48) {
            bool background = code == 48;
            cr_assert_lt(i + 2, n);
            int value;
            if (args[++i] == 5) {
              value = args[++i];
            } else {
              cr_assert_eq(args[i], 2);
              cr_assert_lt(i + 3, n);
              value = (args[i + 1] << 16) | (args[i + 2] << 8) | args[i + 3];
              i += 3;
            }
            if (background)
              bg = value;
            else
              fg = value;
          } else if (code >= 30 && code <= 37)
            fg = code - 30;
          else if (code >= 90 && code <= 97)
            fg = code - 90 + 8;
          else if (code >= 40 && code <= 47)
            bg = code - 40;
          else if (code >= 100 && code <= 107)
            bg = code - 100 + 8;
          else
            cr_assert_fail("unexpected SGR %d", code);
        }
      }
      ++p;
      continue;
    }
    unsigned bytes = *p < 128 ? 1 : ((*p & 0xe0) == 0xc0 ? 2 : ((*p & 0xf0) == 0xe0 ? 3 : 4));
    last = (terminal_cell_t){.bytes = bytes, .fg = fg, .bg = bg};
    for (unsigned i = 0; i < bytes; ++i) {
      cr_assert(p[i]);
      if (i)
        cr_assert_eq(p[i] & 0xc0, 0x80);
      last.glyph[i] = (char)p[i];
    }
    cr_assert_lt(x, width, "too many cells");
    cr_assert_lt(y, height);
    cells[y * width + x++] = last;
    p += bytes;
  }
  cr_assert_eq(x, width, "missing cells");
  cr_assert_eq(y, height - 1, "missing rows");
  cr_assert_eq(fg, -1);
  cr_assert_eq(bg, -1);
}

Test(ascii_emission, all_backends_terminal_cells, .timeout = 120) {
  const int widths[] = {1, 2, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 129, 257};
  const char *palettes[] = {" .:+#@", " \xc2\xb7\xe2\x96\x91\xe2\x96\x88", "@"};
  uint32_t features = platform_cpu_features();
  for (size_t wi = 0; wi < sizeof(widths) / sizeof(widths[0]); ++wi) {
    int width = widths[wi];
    image_t fixture = {.w = width, .h = 3};
    fixture.pixels = SAFE_CALLOC((size_t)width * 3, sizeof(rgb_pixel_t), rgb_pixel_t *);
    image_t *image = &fixture;
    for (int pattern = 0; pattern < 3; ++pattern) {
      for (int i = 0; i < width * 3; ++i) {
        // Noise, long identical runs, and alternating colors across row ends.
        int value = pattern == 0 ? i : pattern == 1 ? i / 40 : i % 2;
        image->pixels[i] = (rgb_pixel_t){(uint8_t)(value * 37), (uint8_t)(value * 71), (uint8_t)(value * 13)};
      }
      for (size_t pi = 0; pi < sizeof(palettes) / sizeof(palettes[0]); ++pi) {
        for (ascii_color_t mode = ASCII_COLOR_NONE; mode <= ASCII_COLOR_TRUE; ++mode) {
          for (int bg = 0; bg <= (mode != ASCII_COLOR_NONE); ++bg) {
            for (int dither = 0; dither <= (mode == ASCII_COLOR_16); ++dither) {
              char *reference = ascii_render(image, palettes[pi], mode, bg, dither, &simd_cells_scalar);
              cr_assert_not_null(reference);
              terminal_cell_t *expected = SAFE_CALLOC((size_t)width * 3, sizeof(*expected), terminal_cell_t *);
              terminal_cell_t *actual = SAFE_CALLOC((size_t)width * 3, sizeof(*actual), terminal_cell_t *);
              decode(reference, expected, width, 3, mode != ASCII_COLOR_NONE);
              // Assert geometry, glyph selection, and color against source cells.
              utf8_palette_cache_t *palette_cache = get_utf8_palette_cache(palettes[pi]);
              for (int i = 0; i < width * 3; ++i) {
                rgb_pixel_t p = image->pixels[i];
                uint8_t luma = (uint8_t)((77u * p.r + 150u * p.g + 29u * p.b + 128u) >> 8);
                const utf8_char_t *glyph = &palette_cache->cache64[luma >> 2];
                cr_assert_eq(expected[i].bytes, glyph->byte_len);
                cr_assert_eq(memcmp(expected[i].glyph, glyph->utf8_bytes, glyph->byte_len), 0);
                if (mode == ASCII_COLOR_TRUE) {
                  int color = (p.r << 16) | (p.g << 8) | p.b;
                  cr_assert_eq(bg ? expected[i].bg : expected[i].fg, color);
                  if (bg)
                    cr_assert_eq(expected[i].fg, luma < 128 ? 0xffffff : 0);
                } else if (mode == ASCII_COLOR_256) {
                  cr_assert_eq(bg ? expected[i].bg : expected[i].fg, rgb_to_256color(p.r, p.g, p.b));
                  if (bg)
                    cr_assert_eq(expected[i].fg, luma < 128 ? 15 : 0);
                } else if (mode == ASCII_COLOR_16 && !dither) {
                  cr_assert_eq(bg ? expected[i].bg : expected[i].fg, rgb_to_16color(p.r, p.g, p.b));
                }
              }
              for (size_t b = 0; b < sizeof(backends) / sizeof(backends[0]); ++b) {
                if ((features & backends[b].feature) != backends[b].feature)
                  continue;
                char *result;
                if (mode == ASCII_COLOR_NONE && backends[b].mono)
                  result = backends[b].mono(image, palettes[pi]);
                else if ((mode == ASCII_COLOR_TRUE || mode == ASCII_COLOR_256) && backends[b].color)
                  result = backends[b].color(image, bg, mode == ASCII_COLOR_256, palettes[pi]);
                else
                  result = ascii_render(image, palettes[pi], mode, bg, dither, backends[b].luma);
                cr_assert_not_null(result, "%s", backends[b].name);
                decode(result, actual, width, 3, mode != ASCII_COLOR_NONE);
                cr_assert_eq(memcmp(expected, actual, (size_t)width * 3 * sizeof(*actual)), 0,
                             "%s width=%d mode=%d bg=%d dither=%d", backends[b].name, width, mode, bg, dither);
                SAFE_FREE(result);
              }
              SAFE_FREE(actual);
              SAFE_FREE(expected);
              SAFE_FREE(reference);
            }
          }
        }
      }
    }
    SAFE_FREE(fixture.pixels);
  }
}

Test(ascii_emission, simd_pixel_stages_and_tails, .timeout = 120) {
  uint8_t rgb[3 * 258 + 1], luma[258];
  uint32_t packed[258], expected[258], actual[260], table[64], fg[258], bg[258];
  for (size_t i = 0; i < sizeof(rgb); ++i)
    rgb[i] = (uint8_t)(i * 37 + i / 5);
  for (size_t i = 0; i < 258; ++i)
    luma[i] = (uint8_t)(i * 43);
  for (size_t i = 0; i < 64; ++i)
    table[i] = 0xf0800000u + (uint32_t)i * 0x10203u;
  for (size_t b = 0; b < sizeof(backends) / sizeof(backends[0]); ++b) {
    if ((platform_cpu_features() & backends[b].feature) != backends[b].feature)
      continue;
    const simd_cells_t *ops = backends[b].luma;
    if (b)
      cr_assert(ops != &simd_cells_scalar);
    for (size_t n = 0; n <= 257; ++n) {
      actual[0] = actual[n + 1] = 0xdeadbeef;
      simd_pack_scalar(rgb + 1, expected, n);
      ops->pack(rgb + 1, actual + 1, n);
      cr_assert_eq(memcmp(expected, actual + 1, n * sizeof(*expected)), 0);
      cr_assert_eq(actual[0], 0xdeadbeef);
      cr_assert_eq(actual[n + 1], 0xdeadbeef);
      memcpy(packed, expected, n * sizeof(*packed));
      simd_lookup_scalar(luma, table, expected, n);
      ops->lookup(luma, table, actual + 1, n);
      cr_assert_eq(memcmp(expected, actual + 1, n * sizeof(*expected)), 0);
      for (unsigned colors = 16; colors <= 256; colors *= 16) {
        simd_quantize_scalar(packed, expected, n, colors);
        ops->quantize(packed, actual + 1, n, colors);
        cr_assert_eq(memcmp(expected, actual + 1, n * sizeof(*expected)), 0);
      }
      cr_assert_eq(actual[0], 0xdeadbeef);
      cr_assert_eq(actual[n + 1], 0xdeadbeef);
      for (size_t cut = 1; cut <= n; ++cut) {
        for (size_t j = 0; j < n; ++j) {
          packed[j] = 0x80706050;
          fg[j] = 0x112233;
          bg[j] = UINT32_MAX;
        }
        if (cut < n) {
          if (cut % 3 == 0)
            packed[cut] ^= 1;
          else if (cut % 3 == 1)
            fg[cut] ^= 1;
          else
            bg[cut] ^= 1;
        }
        cr_assert_eq(ops->run(packed, fg, bg, n), cut);
      }
    }
    // Cartesian color boundaries exercise exact nearest-palette ties.
    const uint8_t edges[] = {0,   1,   47,  48,  63,  64,  95,  115, 116, 127, 128, 135,
                             155, 156, 175, 192, 195, 196, 215, 235, 236, 254, 255};
    size_t used = 0;
    for (size_t r = 0; r < sizeof(edges); ++r)
      for (size_t g = 0; g < sizeof(edges); ++g)
        for (size_t blue = 0; blue < sizeof(edges); ++blue) {
          packed[used++] = ((uint32_t)edges[r] << 16) | ((uint32_t)edges[g] << 8) | edges[blue];
          if (used == 258 || (r + 1 == sizeof(edges) && g + 1 == sizeof(edges) && blue + 1 == sizeof(edges))) {
            for (unsigned colors = 16; colors <= 256; colors *= 16) {
              simd_quantize_scalar(packed, expected, used, colors);
              ops->quantize(packed, actual, used, colors);
              cr_assert_eq(memcmp(expected, actual, used * sizeof(*expected)), 0);
            }
            used = 0;
          }
        }
  }
}

Test(ascii_emission, all_simd_halfblock_modes, .timeout = 120) {
  const int widths[] = {1, 2, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 129, 257};
  for (size_t wi = 0; wi < sizeof(widths) / sizeof(widths[0]); ++wi) {
    int width = widths[wi], stride = width * 3 + 7;
    for (int height = 1; height <= 5; ++height) {
      uint8_t *rgb = SAFE_MALLOC((size_t)stride * height, uint8_t *);
      memset(rgb, 0xa5, (size_t)stride * height);
      for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
          uint8_t *pixel = rgb + (size_t)y * stride + 3 * x;
          unsigned value = (unsigned)(x / 23 + y * 7);
          pixel[0] = x % 7 == 0 ? 0 : (uint8_t)(value * 37);
          pixel[1] = x % 7 == 0 ? 0 : (uint8_t)(value * 71);
          pixel[2] = x % 7 == 0 ? 0 : (uint8_t)(value * 13);
        }
      size_t cells_count = (size_t)width * ((height + 1) / 2);
      terminal_cell_t *expected = SAFE_CALLOC(cells_count, sizeof(*expected), terminal_cell_t *);
      terminal_cell_t *actual = SAFE_CALLOC(cells_count, sizeof(*actual), terminal_cell_t *);
      for (ascii_color_t mode = ASCII_COLOR_NONE; mode <= ASCII_COLOR_TRUE; ++mode) {
        char *reference = ascii_render_halfblocks(rgb, width, height, stride, mode, &simd_cells_scalar);
        cr_assert_not_null(reference);
        decode(reference, expected, width, (height + 1) / 2, mode != ASCII_COLOR_NONE);
        for (int y = 0; y < height; y += 2)
          for (int x = 0; x < width; ++x) {
            const uint8_t *top = rgb + (size_t)y * stride + 3 * x;
            const uint8_t *bottom = y + 1 < height ? top + stride : top;
            terminal_cell_t cell = expected[(y / 2) * width + x];
            unsigned yt = (76u * top[0] + 150u * top[1] + 29u * top[2]) >> 8;
            unsigned yb = (76u * bottom[0] + 150u * bottom[1] + 29u * bottom[2]) >> 8;
            bool padding = mode == ASCII_COLOR_NONE ? yt < 16 && yb < 16
                                                    : !(top[0] | top[1] | top[2] | bottom[0] | bottom[1] | bottom[2]);
            if (padding) {
              cr_assert_eq(cell.glyph[0], ' ');
              cr_assert_eq(cell.fg, -1);
              cr_assert_eq(cell.bg, -1);
            } else if (mode == ASCII_COLOR_TRUE) {
              cr_assert_eq(cell.fg, (top[0] << 16) | (top[1] << 8) | top[2]);
              cr_assert_eq(cell.bg, (bottom[0] << 16) | (bottom[1] << 8) | bottom[2]);
            } else if (mode != ASCII_COLOR_NONE) {
              int fg = mode == ASCII_COLOR_16 ? rgb_to_16color(top[0], top[1], top[2])
                                              : rgb_to_256color(top[0], top[1], top[2]);
              int bg = mode == ASCII_COLOR_16 ? rgb_to_16color(bottom[0], bottom[1], bottom[2])
                                              : rgb_to_256color(bottom[0], bottom[1], bottom[2]);
              cr_assert_eq(cell.fg, fg);
              cr_assert_eq(cell.bg, bg);
            }
          }
        for (size_t b = 0; b < sizeof(backends) / sizeof(backends[0]); ++b) {
          if ((platform_cpu_features() & backends[b].feature) != backends[b].feature)
            continue;
          char *result = ascii_render_halfblocks(rgb, width, height, stride, mode, backends[b].luma);
          cr_assert_not_null(result);
          decode(result, actual, width, (height + 1) / 2, mode != ASCII_COLOR_NONE);
          cr_assert_eq(memcmp(expected, actual, cells_count * sizeof(*expected)), 0);
          SAFE_FREE(result);
        }
        SAFE_FREE(reference);
      }
      SAFE_FREE(actual);
      SAFE_FREE(expected);
      SAFE_FREE(rgb);
    }
  }
}

Test(ascii_emission, unicode_run_and_row_contract) {
  outbuf_t out = {.cap = 1, .buf = SAFE_MALLOC(1, char *)};
  emit_glyph_run(&out, "\xe2\x96\x88", 3, 4);
  emit_row_end(&out, true, true);
  emit_glyph_run(&out, "@", 1, 2);
  emit_row_end(&out, false, false);
  ob_term(&out);
  cr_assert_str_eq(out.buf, "\xe2\x96\x88\033[3b\033[0m\n@@");
  SAFE_FREE(out.buf);
}

Test(ascii_emission, halfblock_odd_height_padding_and_colors) {
  uint8_t rgb[] = {255, 0, 0, 0, 0, 0, 0, 0, 255, 0, 0, 0, 0, 255, 0, 0, 0, 0};
  char *result = rgb_to_truecolor_halfblocks_scalar(rgb, 2, 3, 6);
  terminal_cell_t cells[4] = {0};
  decode(result, cells, 2, 2, true);
  cr_assert_eq(cells[0].fg, 0xff0000);
  cr_assert_eq(cells[0].bg, 0x0000ff);
  cr_assert_eq(cells[2].fg, 0x00ff00);
  cr_assert_eq(cells[2].bg, 0x00ff00);
  cr_assert_eq(cells[1].glyph[0], ' ');
  cr_assert_eq(cells[1].fg, -1);
  cr_assert_eq(cells[1].bg, -1);
#if SIMD_SUPPORT_NEON
  if (platform_cpu_features() & CPU_FEATURE_NEON) {
    char *neon = rgb_to_truecolor_halfblocks_neon(rgb, 2, 3, 6);
    cr_assert_str_eq(result, neon);
    SAFE_FREE(neon);
  }
#endif
  SAFE_FREE(result);
}

Test(ascii_emission, sgr_encoders_and_compatibility) {
  const uint8_t values[] = {0, 1, 9, 10, 99, 100, 255};
  for (size_t r = 0; r < sizeof(values); ++r) {
    for (size_t g = 0; g < sizeof(values); ++g) {
      for (size_t b = 0; b < sizeof(values); ++b) {
        char actual[40], expected[40];
        memset(actual, '#', sizeof(actual));
        char *end = append_sgr_truecolor_fg_bg(actual, values[r], values[g], values[b], 255, 100, 0);
        int length =
            snprintf(expected, sizeof(expected), "\033[38;2;%u;%u;%u;48;2;255;100;0m", values[r], values[g], values[b]);
        cr_assert_eq(end - actual, length);
        cr_assert_eq(memcmp(actual, expected, (size_t)length), 0);
        cr_assert_eq(*end, '#');
        char compatible[40];
        cr_assert_eq(append_truecolor_fg_bg(compatible, values[r], values[g], values[b], 255, 100, 0) - compatible,
                     length);
        cr_assert_eq(memcmp(compatible, actual, (size_t)length), 0);
      }
    }
  }
  for (int index = 0; index < 256; ++index) {
    char sequence[16], expected[16];
    char *end = append_256color_bg(sequence, (uint8_t)index);
    int length = snprintf(expected, sizeof(expected), "\033[48;5;%dm", index);
    cr_assert_eq(end - sequence, length);
    cr_assert_eq(memcmp(sequence, expected, (size_t)length), 0);
  }
}

Test(ascii_emission, capability_dispatch_preserves_background) {
  rgb_pixel_t pixels[2] = {{240, 50, 20}, {20, 50, 240}};
  image_t image = {.pixels = pixels, .w = 2, .h = 1};
  const terminal_color_mode_t modes[] = {TERM_COLOR_16, TERM_COLOR_256, TERM_COLOR_TRUECOLOR};
  for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
    terminal_capabilities_t caps = {.color_level = modes[i], .render_mode = RENDER_MODE_BACKGROUND};
    char *text = image_print_with_capabilities(&image, &caps, "@");
    cr_assert(text != NULL);
    terminal_cell_t cells[2] = {0};
    decode(text, cells, 2, 1, true);
    for (size_t x = 0; x < 2; ++x) {
      cr_assert(cells[x].bg != -1, "Background color lost for mode %d", modes[i]);
      cr_assert(cells[x].fg != -1, "Contrasting foreground missing for mode %d", modes[i]);
    }
    SAFE_FREE(text);
  }
}

Test(ascii_emission, rejects_invalid_input) {
  rgb_pixel_t pixel = {0};
  image_t image = {.pixels = &pixel, .w = -1, .h = 1};
  cr_assert_null(ascii_render(&image, "@", ASCII_COLOR_TRUE, false, false, &simd_cells_scalar));
  uint8_t half_pixel[3] = {0};
  cr_assert_null(ascii_render_halfblocks(half_pixel, 1, 1, 0, ASCII_COLOR_TRUE, NULL));
  cr_assert_null(ascii_render_halfblocks(half_pixel, 1, 1, 2, ASCII_COLOR_TRUE, &simd_cells_scalar));
  image.w = 1;
  cr_assert_null(ascii_render(&image, "", ASCII_COLOR_TRUE, false, false, &simd_cells_scalar));
  cr_assert_null(ascii_render(&image, "@", ASCII_COLOR_TRUE, false, false, NULL));
}

#ifdef ASCII_EMISSION_STANDALONE
int main(int argc, char **argv) {
#if defined(__aarch64__)
  if (argc == 2) {
    int length = atoi(argv[1]);
    cr_assert_eq(prctl(PR_SVE_SET_VL, length, 0, 0, 0), length);
    printf("SVE vector length: %d bytes\n", prctl(PR_SVE_GET_VL, 0, 0, 0, 0) & PR_SVE_VL_LEN_MASK);
  }
#else
  (void)argc;
  (void)argv;
#endif
  for (size_t b = 0; b < sizeof(backends) / sizeof(backends[0]); ++b) {
    if ((platform_cpu_features() & backends[b].feature) == backends[b].feature)
      printf("Testing %s terminal emission\n", backends[b].name);
  }
  ascii_emission_all_backends_terminal_cells();
  ascii_emission_simd_pixel_stages_and_tails();
  ascii_emission_all_simd_halfblock_modes();
  ascii_emission_unicode_run_and_row_contract();
  ascii_emission_halfblock_odd_height_padding_and_colors();
  ascii_emission_sgr_encoders_and_compatibility();
  ascii_emission_rejects_invalid_input();
  ascii_emission_capability_dispatch_preserves_background();
  simd_caches_destroy_all();
  return 0;
}
#endif
