#ifdef ASCII_EMISSION_STANDALONE
#include "../../simd/emission_assert.h"
#else
#include <criterion/criterion.h>
#endif
#include <ascii-chat/common.h>
#include <ascii-chat/video/ascii/render/common.h>
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
#include "../../../lib/video/ascii/glyph_masks.h"
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

static char *test_render_glyph_mode(const image_t *image, ascii_color_t color, const simd_cells_t *ops,
                                    const char *palette, bool background) {
  const ascii_render_options_t options = {
      .kind = ASCII_RENDERER_GLYPH, .color = color, .backend = ops, .palette = palette, .background = background};
  return ascii_render_image(image, &options);
}
static char *test_render_glyph(const image_t *image, ascii_color_t color, const simd_cells_t *ops,
                               const char *palette) {
  return test_render_glyph_mode(image, color, ops, palette, false);
}

Test(ascii_emission, renderer_api_dispatches_and_rejects_unknown_kind) {
  rgb_pixel_t pixels[64];
  for (int i = 0; i < 64; ++i)
    pixels[i] = (rgb_pixel_t){100, 100, 100};
  image_t glyph_image = {.w = 8, .h = 8, .pixels = pixels};
  image_t luminance_image = {.w = 1, .h = 1, .pixels = pixels};
  ascii_render_options_t options = {
      .kind = ASCII_RENDERER_LUMINANCE, .color = ASCII_COLOR_NONE, .palette = " .#@", .backend = &simd_cells_scalar};
  char *actual = ascii_render_image(&luminance_image, &options);
  char *expected = ascii_render(&luminance_image, options.palette, options.color, false, false, options.backend);
  cr_assert_not_null(actual);
  cr_assert_not_null(expected);
  cr_assert_str_eq(actual, expected);
  SAFE_FREE(actual);
  SAFE_FREE(expected);
  options.kind = ASCII_RENDERER_GLYPH;
  actual = ascii_render_image(&glyph_image, &options);
  cr_assert_not_null(actual);
  cr_assert(strchr(options.palette, actual[0]) != NULL);
  SAFE_FREE(actual);
  options.kind = (ascii_renderer_t)99;
  cr_assert_null(ascii_render_image(&glyph_image, &options));
  CLEAR_ERRNO();
}

Test(ascii_emission, monochrome_fallback_does_not_emit_color_resets) {
  simd_cells_t fallback = simd_cells_scalar;
  fallback.emit_mono = NULL;
  uint32_t glyphs[] = {'A', 'B'};
  uint32_t foreground[] = {1, UINT32_MAX};
  uint32_t background[] = {2, UINT32_MAX};
  outbuf_t output = {0};
  ascii_emit_prepared_row(&output, ASCII_COLOR_NONE, &fallback, glyphs, foreground, background, 2);
  ob_term(&output);
  cr_assert_str_eq(output.buf, "AB");
  SAFE_FREE(output.buf);
}

Test(ascii_emission, fine_block_geometry_preserves_single_pixel_edges) {
  rgb_pixel_t pixels[64] = {0};
  for (int x = 0; x < 8; ++x)
    pixels[56 + x] = (rgb_pixel_t){120, 120, 120};
  image_t image = {.w = 8, .h = 8, .pixels = pixels};
  char *output = test_render_glyph(&image, ASCII_COLOR_TRUE, &simd_cells_scalar, "_\xe2\x96\x81");
  cr_assert_not_null(output);
  terminal_cell_t cell = {0};
  decode(output, &cell, 1, 1, true);
  cr_assert_str_eq(cell.glyph, "\xe2\x96\x81");
  SAFE_FREE(output);
}

Test(ascii_emission, stabilization_rejects_isoluminant_color_motion) {
  ascii_stabilizer_t state = {0};
  cr_assert_eq(ascii_stabilizer_prepare(&state, 1, 1, 1), ASCIICHAT_OK);
  state.elapsed_ns = 16000000;
  ascii_stabilizer_t *prior = ascii_stabilizer_set_target(&state);
  rgb_pixel_t pixels[2] = {{150, 0, 0}, {150, 0, 0}};
  image_t image = {.w = 1, .h = 1, .pixels = pixels};
  char *first = ascii_render(&image, " .#@", ASCII_COLOR_TRUE, false, false, &simd_cells_scalar);
  cr_assert_not_null(first);
  SAFE_FREE(first);
  pixels[0] = (rgb_pixel_t){0, 77, 0};
  char *next = ascii_render(&image, " .#@", ASCII_COLOR_TRUE, false, false, &simd_cells_scalar);
  cr_assert_not_null(next);
  cr_assert_eq(state.cells[0].color, 0x004d00);
  SAFE_FREE(next);
  ascii_stabilizer_reset(&state);
  cr_assert_eq(ascii_stabilizer_prepare(&state, 1, 2, 2), ASCIICHAT_OK);
  state.elapsed_ns = 16000000;
  pixels[0] = pixels[1] = (rgb_pixel_t){150, 0, 0};
  first = ascii_render_halfblocks((const uint8_t *)pixels, 1, 2, 3, ASCII_COLOR_TRUE, &simd_cells_scalar);
  cr_assert_not_null(first);
  SAFE_FREE(first);
  pixels[0] = pixels[1] = (rgb_pixel_t){0, 77, 0};
  next = ascii_render_halfblocks((const uint8_t *)pixels, 1, 2, 3, ASCII_COLOR_TRUE, &simd_cells_scalar);
  cr_assert_not_null(next);
  cr_assert_eq(state.cells[0].color, 0x004d00);
  cr_assert_eq(state.cells[1].color, 0x004d00);
  SAFE_FREE(next);
  ascii_stabilizer_set_target(prior);
  ascii_stabilizer_reset(&state);
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

Test(ascii_emission, temporal_history_tracks_motion_and_configuration) {
  ascii_stabilizer_t state = {0}, other = {0};
  uint8_t samples[16];
  memset(samples, 100, sizeof(samples));
  cr_assert_eq(ascii_stabilizer_prepare(&state, 2, 1, 1), ASCIICHAT_OK);
  cr_assert_eq(ascii_stabilizer_prepare(&other, 2, 1, 1), ASCIICHAT_OK);
  uint32_t color;
  cr_assert_eq(ascii_stabilizer_choose(&state.cells[0], samples, 'A', 100, UINT64_MAX, 0x646464, 16000000, &color),
               'A');
  cr_assert_eq(color, 0x646464);
  memset(samples, 101, sizeof(samples));
  cr_assert_eq(ascii_stabilizer_choose(&state.cells[0], samples, 'B', 100, 120, 0x686868, 16000000, &color), 'A');
  cr_assert(color > 0x646464 && color < 0x686868);
  cr_assert_eq(ascii_stabilizer_choose(&other.cells[0], samples, 'B', 100, UINT64_MAX, 0x686868, 16000000, &color),
               'B');
  cr_assert_eq(ascii_stabilizer_choose(&state.cells[0], samples, 'B', 100, 10000, 0x686868, 16000000, &color), 'B');
  memset(samples, 200, sizeof(samples));
  cr_assert_eq(ascii_stabilizer_choose(&state.cells[0], samples, 'C', 100, 101, 0xc8c8c8, 16000000, &color), 'C');
  cr_assert_eq(color, 0xc8c8c8);
  // A long interruption must not hold the previous glyph or color.
  cr_assert_eq(ascii_stabilizer_choose(&state.cells[0], samples, 'D', 100, 101, 0xc9c9c9, 300000000, &color), 'D');
  cr_assert_eq(color, 0xc9c9c9);
  cr_assert_eq(ascii_stabilizer_prepare(&state, 2, 1, 2), ASCIICHAT_OK);
  cr_assert(!state.cells[0].valid);
  memset(samples, 0, sizeof(samples));
  ascii_stabilizer_choose(&state.cells[0], samples, 'A', 100, UINT64_MAX, 0, 16000000, &color);
  samples[0] = 143;
  cr_assert_eq(ascii_stabilizer_choose(&state.cells[0], samples, 'B', 100, 101, 0, 16000000, &color), 'A');
  memset(&state.cells[0], 0, sizeof(state.cells[0]));
  samples[0] = 0;
  ascii_stabilizer_choose(&state.cells[0], samples, 'A', 100, UINT64_MAX, 0, 16000000, &color);
  samples[0] = 144;
  cr_assert_eq(ascii_stabilizer_choose(&state.cells[0], samples, 'B', 100, 101, 0, 16000000, &color), 'B');
  ascii_stabilizer_reset(&state);
  ascii_stabilizer_reset(&other);
}

Test(ascii_emission, temporal_stabilization_reduces_threshold_flicker) {
  for (int glyph = 0; glyph < 2; ++glyph) {
    rgb_pixel_t pixels[64];
    image_t image = {.w = glyph ? 8 : 1, .h = glyph ? 8 : 1, .pixels = pixels};
    const char *palette = " .#@";
    int boundary = -1;
    char previous = 0;
    for (int value = 0; value < 256; ++value) {
      for (int i = 0; i < image.w * image.h; ++i)
        pixels[i] = (rgb_pixel_t){value, value, value};
      char *output = glyph ? test_render_glyph(&image, ASCII_COLOR_NONE, &simd_cells_scalar, palette)
                           : ascii_render(&image, palette, ASCII_COLOR_NONE, false, false, &simd_cells_scalar);
      cr_assert_not_null(output);
      if (value && output[0] != previous) {
        boundary = value;
        SAFE_FREE(output);
        break;
      }
      previous = output[0];
      SAFE_FREE(output);
    }
    cr_assert(boundary > 0 && boundary < 255, "fixture must cross a glyph boundary");
    ascii_stabilizer_t state = {0};
    cr_assert_eq(ascii_stabilizer_prepare(&state, 1, 1, 1), ASCIICHAT_OK);
    state.elapsed_ns = 16666667;
    ascii_stabilizer_t *prior = ascii_stabilizer_set_target(&state);
    for (int i = 0; i < image.w * image.h; ++i)
      pixels[i] = (rgb_pixel_t){boundary - 1, boundary - 1, boundary - 1};
    char *initial = glyph ? test_render_glyph(&image, ASCII_COLOR_NONE, &simd_cells_scalar, palette)
                          : ascii_render(&image, palette, ASCII_COLOR_NONE, false, false, &simd_cells_scalar);
    cr_assert_not_null(initial);
    for (int frame = 0; frame < 6; ++frame) {
      int value = boundary - (frame % 2);
      for (int i = 0; i < image.w * image.h; ++i)
        pixels[i] = (rgb_pixel_t){value, value, value};
      char *output = glyph ? test_render_glyph(&image, ASCII_COLOR_NONE, &simd_cells_scalar, palette)
                           : ascii_render(&image, palette, ASCII_COLOR_NONE, false, false, &simd_cells_scalar);
      cr_assert_not_null(output);
      cr_assert_eq(output[0], initial[0], "stationary threshold jitter should not switch glyphs");
      SAFE_FREE(output);
    }
    ascii_stabilizer_set_target(prior);
    SAFE_FREE(initial);
    ascii_stabilizer_reset(&state);
  }
}

Test(ascii_emission, temporal_background_and_halfblock_modes) {
  const ascii_color_t modes[] = {ASCII_COLOR_NONE, ASCII_COLOR_16, ASCII_COLOR_256, ASCII_COLOR_TRUE};
  for (size_t b = 0; b < sizeof(backends) / sizeof(backends[0]); ++b) {
    if ((platform_cpu_features() & backends[b].feature) != backends[b].feature)
      continue;
    const simd_cells_t *ops = backends[b].luma;
    for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); ++m) {
      rgb_pixel_t pixel = {100, 100, 100};
      image_t image = {.w = 1, .h = 1, .pixels = &pixel};
      ascii_stabilizer_t state = {0};
      cr_assert_eq(ascii_stabilizer_prepare(&state, 1, 1, 1), ASCIICHAT_OK);
      state.elapsed_ns = 16666667;
      char *baseline = ascii_render(&image, " .#@", modes[m], true, false, ops);
      cr_assert_not_null(baseline);
      ascii_stabilizer_t *prior = ascii_stabilizer_set_target(&state);
      char *first = ascii_render(&image, " .#@", modes[m], true, false, ops);
      cr_assert_not_null(first);
      cr_assert_str_eq(first, baseline, "first background frame changed on %s", backends[b].name);
      pixel = (rgb_pixel_t){104, 104, 104};
      char *next = ascii_render(&image, " .#@", modes[m], true, false, ops);
      cr_assert_not_null(next);
      cr_assert(state.cells[0].color > 0x646464);
      cr_assert_lt(state.cells[0].color, 0x686868);
      if (modes[m] == ASCII_COLOR_TRUE) {
        terminal_cell_t cell = {0};
        decode(next, &cell, 1, 1, true);
        cr_assert_eq(cell.bg, state.cells[0].color);
      }
      SAFE_FREE(baseline);
      SAFE_FREE(first);
      SAFE_FREE(next);
      ascii_stabilizer_set_target(prior);
      ascii_stabilizer_reset(&state);

      uint8_t pixels[] = {100, 100, 100, 150, 150, 150, 200, 200, 200};
      cr_assert_eq(ascii_stabilizer_prepare(&state, 1, 3, 2), ASCIICHAT_OK);
      state.elapsed_ns = 16666667;
      baseline = ascii_render_halfblocks(pixels, 1, 3, 3, modes[m], ops);
      cr_assert_not_null(baseline);
      prior = ascii_stabilizer_set_target(&state);
      first = ascii_render_halfblocks(pixels, 1, 3, 3, modes[m], ops);
      cr_assert_not_null(first);
      cr_assert_str_eq(first, baseline, "first half-block frame changed on %s", backends[b].name);
      for (size_t i = 0; i < sizeof(pixels); ++i)
        pixels[i] += 4;
      next = ascii_render_halfblocks(pixels, 1, 3, 3, modes[m], ops);
      cr_assert_not_null(next);
      cr_assert(state.cells[0].color > 0x646464);
      cr_assert_lt(state.cells[0].color, 0x686868);
      cr_assert(state.cells[1].color > 0x969696);
      cr_assert_lt(state.cells[1].color, 0x9a9a9a);
      cr_assert(state.cells[2].color > 0xc8c8c8);
      cr_assert_lt(state.cells[2].color, 0xcccccc);
      if (modes[m] == ASCII_COLOR_TRUE) {
        terminal_cell_t cells[2] = {0};
        decode(next, cells, 1, 2, true);
        cr_assert_eq(cells[0].fg, state.cells[0].color);
        cr_assert_eq(cells[0].bg, state.cells[1].color);
        cr_assert_eq(cells[1].fg, state.cells[2].color);
        cr_assert_eq(cells[1].bg, state.cells[2].color);
      }
      pixels[0] = pixels[1] = pixels[2] = 240;
      char *motion = ascii_render_halfblocks(pixels, 1, 3, 3, modes[m], ops);
      cr_assert_not_null(motion);
      cr_assert_eq(state.cells[0].color, 0xf0f0f0, "motion must not be held back");
      SAFE_FREE(baseline);
      SAFE_FREE(first);
      SAFE_FREE(next);
      SAFE_FREE(motion);
      ascii_stabilizer_set_target(prior);
      ascii_stabilizer_reset(&state);
    }
  }
}

Test(ascii_emission, temporal_halfblock_reduces_monochrome_flicker) {
  for (size_t b = 0; b < sizeof(backends) / sizeof(backends[0]); ++b) {
    if ((platform_cpu_features() & backends[b].feature) != backends[b].feature)
      continue;
    uint8_t pixels[6];
    char previous[8] = {0};
    int boundary = -1;
    for (int value = 0; value < 256; ++value) {
      memset(pixels, value, sizeof(pixels));
      char *output = ascii_render_halfblocks(pixels, 1, 2, 3, ASCII_COLOR_NONE, backends[b].luma);
      cr_assert_not_null(output);
      if (value && strcmp(output, previous) != 0) {
        boundary = value;
        SAFE_FREE(output);
        break;
      }
      SAFE_STRNCPY(previous, output, sizeof(previous));
      SAFE_FREE(output);
    }
    cr_assert(boundary > 0 && boundary < 255);
    ascii_stabilizer_t state = {0};
    cr_assert_eq(ascii_stabilizer_prepare(&state, 1, 2, 1), ASCIICHAT_OK);
    state.elapsed_ns = 16666667;
    ascii_stabilizer_t *prior = ascii_stabilizer_set_target(&state);
    memset(pixels, boundary - 1, sizeof(pixels));
    char *initial = ascii_render_halfblocks(pixels, 1, 2, 3, ASCII_COLOR_NONE, backends[b].luma);
    cr_assert_not_null(initial);
    for (int frame = 0; frame < 6; ++frame) {
      memset(pixels, boundary - (frame % 2), sizeof(pixels));
      char *next = ascii_render_halfblocks(pixels, 1, 2, 3, ASCII_COLOR_NONE, backends[b].luma);
      cr_assert_not_null(next);
      cr_assert_str_eq(next, initial, "monochrome half-block jitter on %s", backends[b].name);
      SAFE_FREE(next);
    }
    SAFE_FREE(initial);
    ascii_stabilizer_set_target(prior);
    ascii_stabilizer_reset(&state);
  }
}

#ifndef ASCII_EMISSION_STANDALONE
Test(ascii_emission, capability_conversion_rejects_unsupported_explicit_glyph_palette) {
  rgb_pixel_t pixel = {100, 100, 100};
  image_t image = {.w = 1, .h = 1, .pixels = &pixel};
  terminal_capabilities_t caps = {
      .text_renderer = ASCII_RENDERER_GLYPH, .render_mode = RENDER_MODE_FOREGROUND, .palette_explicit = true};
  char *result =
      ascii_convert_with_buffers_stabilized(&image, 1, 1, &caps, false, true, "\xe2\x98\x83", false, NULL, NULL, NULL);
  cr_assert_null(result, "unsupported glyph palettes must not fall back to luminance rendering");
  asciichat_error_context_t context;
  cr_assert(HAS_ERRNO(&context));
  cr_assert_eq(context.code, ERROR_INVALID_PARAM);
  CLEAR_ERRNO();
}

Test(ascii_emission, capability_conversion_stabilizes_all_render_modes) {
  rgb_pixel_t pixels[4];
  image_t image = {.w = 2, .h = 2, .pixels = pixels};
  terminal_capabilities_t caps = {.color_level = TERM_COLOR_TRUECOLOR};
  ascii_stabilizer_t history = {.fixed_step_ns = 16666667};
  const render_mode_t modes[] = {RENDER_MODE_FOREGROUND, RENDER_MODE_BACKGROUND, RENDER_MODE_HALF_BLOCK};
  const ascii_renderer_t renderers[] = {ASCII_RENDERER_LUMINANCE, ASCII_RENDERER_GLYPH};
  for (size_t r = 0; r < sizeof(renderers) / sizeof(renderers[0]); ++r) {
    caps.text_renderer = renderers[r];
    for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); ++m) {
      caps.render_mode = modes[m];
      for (size_t i = 0; i < 4; ++i)
        pixels[i] = (rgb_pixel_t){100, 100, 100};
      char *baseline =
          ascii_convert_with_buffers_stabilized(&image, 2, 2, &caps, false, true, " .#@", false, NULL, NULL, NULL);
      char *first =
          ascii_convert_with_buffers_stabilized(&image, 2, 2, &caps, false, true, " .#@", false, NULL, NULL, &history);
      cr_assert_not_null(baseline);
      cr_assert_not_null(first);
      cr_assert_str_eq(first, baseline, "first frame must be unchanged for renderer %d and mode %d", renderers[r],
                       modes[m]);
      cr_assert_eq(history.width, 2);
      cr_assert_eq(history.height, modes[m] == RENDER_MODE_HALF_BLOCK ? 4 : 2);
      cr_assert(history.cells[0].valid);
      for (size_t i = 0; i < 4; ++i)
        pixels[i] = (rgb_pixel_t){104, 104, 104};
      char *next =
          ascii_convert_with_buffers_stabilized(&image, 2, 2, &caps, false, true, " .#@", false, NULL, NULL, &history);
      cr_assert_not_null(next);
      if (renderers[r] == ASCII_RENDERER_LUMINANCE || modes[m] == RENDER_MODE_HALF_BLOCK) {
        cr_assert(history.cells[0].color > 0x646464);
        cr_assert_lt(history.cells[0].color, 0x686868);
      }
      SAFE_FREE(baseline);
      SAFE_FREE(first);
      SAFE_FREE(next);
    }
  }
  ascii_stabilizer_reset(&history);
}
#endif

Test(ascii_emission, glyph_background_matches_luminance_color_policy) {
  const rgb_pixel_t colors[] = {{0, 0, 0}, {255, 255, 255}, {255, 0, 0}, {0, 255, 0}};
  rgb_pixel_t coarse_pixels[4];
  rgb_pixel_t fine_pixels[32 * 8];
  for (int x = 0; x < 4; ++x)
    coarse_pixels[x] = colors[x];
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 32; ++x)
      fine_pixels[y * 32 + x] = colors[x / 8];
  image_t coarse = {.w = 4, .h = 1, .pixels = coarse_pixels};
  image_t fine = {.w = 32, .h = 8, .pixels = fine_pixels};
  const ascii_color_t modes[] = {ASCII_COLOR_TRUE, ASCII_COLOR_256, ASCII_COLOR_16};
  for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); ++m) {
    char *luminance = ascii_render(&coarse, "@", modes[m], true, false, &simd_cells_scalar);
    char *glyph = test_render_glyph_mode(&fine, modes[m], &simd_cells_scalar, "@", true);
    cr_assert_not_null(luminance);
    cr_assert_not_null(glyph);
    terminal_cell_t expected[4] = {0}, actual[4] = {0};
    decode(luminance, expected, 4, 1, true);
    decode(glyph, actual, 4, 1, true);
    for (int i = 0; i < 4; ++i) {
      cr_assert_eq(actual[i].bg, expected[i].bg, "background for color %d and mode %d", i, modes[m]);
      cr_assert_eq(actual[i].fg, expected[i].fg, "foreground for color %d and mode %d", i, modes[m]);
    }
    SAFE_FREE(luminance);
    SAFE_FREE(glyph);
  }
}

Test(ascii_emission, glyph_indexed_dither_is_stable_and_spatial) {
  rgb_pixel_t pixels[32 * 32];
  for (size_t i = 0; i < 32 * 32; ++i)
    pixels[i] = (rgb_pixel_t){100, 100, 100};
  image_t image = {.w = 32, .h = 32, .pixels = pixels};
  const ascii_color_t modes[] = {ASCII_COLOR_16, ASCII_COLOR_256};
  for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); ++m) {
    char *first = test_render_glyph(&image, modes[m], &simd_cells_scalar, "@");
    char *second = test_render_glyph(&image, modes[m], &simd_cells_scalar, "@");
    cr_assert_not_null(first);
    cr_assert_not_null(second);
    cr_assert_str_eq(first, second, "stationary frames must use the same dither pattern");
    terminal_cell_t cells[16] = {0};
    decode(first, cells, 4, 4, true);
    bool varied = false;
    for (int i = 1; i < 16; ++i)
      varied |= cells[i].fg != cells[0].fg;
    cr_assert(varied, "ordered dithering must distribute quantization levels spatially in mode %d", modes[m]);
    SAFE_FREE(first);
    SAFE_FREE(second);
  }
}

Test(ascii_emission, glyph_matching_lifts_dark_detail_by_default) {
  rgb_pixel_t pixels[64];
  for (int i = 0; i < 64; ++i)
    pixels[i] = (rgb_pixel_t){40, 40, 40};
  image_t image = {.w = 8, .h = 8, .pixels = pixels};
  char *output = test_render_glyph(&image, ASCII_COLOR_NONE, &simd_cells_scalar, NULL);
  cr_assert_not_null(output);
  cr_assert_eq(output[0], '~', "dark glyph density should reflect the automatic luminance lift");
  SAFE_FREE(output);
  for (int background = 0; background <= 1; ++background) {
    output = test_render_glyph_mode(&image, ASCII_COLOR_TRUE, &simd_cells_scalar, "@", background);
    cr_assert_not_null(output);
    terminal_cell_t cell = {0};
    decode(output, &cell, 1, 1, true);
    cr_assert_eq(background ? cell.bg : cell.fg, background ? 0x282828 : 0x949494,
                 "automatic luminance lift must preserve the renderer color calculation");
    SAFE_FREE(output);
  }
}

Test(ascii_emission, optimized_glyph_scoring_matches_reference) {
  rgb_pixel_t pixels[64];
  image_t image = {.w = 8, .h = 8, .pixels = pixels};
  uint32_t rng = 1234567;
  for (int sample = 0; sample < 500; ++sample) {
    int sums[16] = {0};
    for (int y = 0; y < 8; ++y) {
      for (int x = 0; x < 8; ++x) {
        rng = rng * 1664525u + 1013904223u;
        uint8_t v = (uint8_t)(rng >> 24);
        pixels[y * 8 + x] = (rgb_pixel_t){v, v, v};
        sums[(y / 2) * 4 + x / 2] += v;
      }
    }
    int luminance[16];
    for (int i = 0; i < 16; ++i) {
      // The renderer averages each channel before computing luminance.
      int v = sums[i] / 4;
      luminance[i] = (77 * v + 150 * v + 29 * v) >> 8;
      luminance[i] += (luminance[i] * (255 - luminance[i]) + 256) / 512;
    }
    int mean = 0;
    for (int i = 0; i < 16; ++i)
      mean += luminance[i];
    mean /= 16;
    int best = -1;
    int64_t best_error = INT64_MAX;
    for (int g = 0; g < 95; ++g) {
      int mask_sum = 0;
      for (int i = 0; i < 16; ++i)
        mask_sum += glyph_masks[g][i];
      int tone = mean - mask_sum * 255 / GLYPH_MASK_MAX_SUM;
      int64_t error = 16 * tone * tone;
      for (int i = 0; i < 16; ++i) {
        int delta = (luminance[i] - mean) - ((int)glyph_masks[g][i] - mask_sum / 16);
        error += (int64_t)delta * delta;
      }
      if (error < best_error) {
        best = g;
        best_error = error;
      }
    }
    char *output = test_render_glyph(&image, ASCII_COLOR_NONE, &simd_cells_scalar, NULL);
    cr_assert_not_null(output);
    cr_assert_eq((unsigned char)output[0], (unsigned)(best + 32), "sample=%d", sample);
    SAFE_FREE(output);
  }
}

Test(ascii_emission, optimized_luminance_matches_generic_hysteresis) {
  rgb_pixel_t pixel = {0};
  image_t image = {.w = 1, .h = 1, .pixels = &pixel};
  ascii_frame_t frame;
  cr_assert_eq(ascii_frame_init(&frame, &image, " .#@"), ASCIICHAT_OK);
  ascii_stabilizer_t state = {0};
  cr_assert_eq(ascii_stabilizer_prepare(&state, 1, 1, 1), ASCIICHAT_OK);
  state.elapsed_ns = 16666667;
  ascii_stabilizer_t *prior = ascii_stabilizer_set_target(&state);
  for (unsigned old = 0; old < 256; old += 3) {
    for (unsigned current = 0; current < 256; current += 5) {
      unsigned old_bin = old >> 2, bin = current >> 2;
      ascii_temporal_cell_t reference = {
          .glyph = frame.table[old_bin], .color = 0x646464, .source_color = old * 0x010101u, .valid = true};
      memset(reference.samples, old, sizeof(reference.samples));
      state.cells[0] = reference;
      uint64_t best = UINT64_MAX, previous = UINT64_MAX;
      for (unsigned i = 0; i < 64; ++i) {
        int delta = (int)current - ((int)i * 4 + 2);
        uint64_t error = (uint64_t)(delta * delta);
        if (frame.table[i] == frame.table[bin] && error < best)
          best = error;
        if (frame.table[i] == reference.glyph && error < previous)
          previous = error;
      }
      uint8_t samples[16];
      memset(samples, current, sizeof(samples));
      uint32_t expected_color;
      uint32_t expected = ascii_stabilizer_choose(&reference, samples, frame.table[bin], best, previous,
                                                  current * 0x010101u, state.elapsed_ns, &expected_color);
      pixel = (rgb_pixel_t){current, current, current};
      char *output = ascii_render(&image, " .#@", ASCII_COLOR_TRUE, false, false, &simd_cells_scalar);
      cr_assert_not_null(output);
      cr_assert_eq(state.cells[0].glyph, expected, "old=%u current=%u", old, current);
      cr_assert_eq(state.cells[0].color, expected_color, "old=%u current=%u", old, current);
      SAFE_FREE(output);
    }
  }
  ascii_stabilizer_set_target(prior);
  char *frame_output = ascii_frame_finish(&frame);
  SAFE_FREE(frame_output);
  ascii_stabilizer_reset(&state);
}

Test(ascii_emission, temporal_first_frame_preserves_dithered_colors) {
  rgb_pixel_t pixels[16];
  for (int i = 0; i < 16; ++i)
    pixels[i] = (rgb_pixel_t){(uint8_t)(50 + i * 10), (uint8_t)(80 + i * 5), (uint8_t)(110 + i * 3)};
  image_t image = {.w = 4, .h = 4, .pixels = pixels};
  char *baseline = ascii_render(&image, " .#@", ASCII_COLOR_16, false, true, &simd_cells_scalar);
  cr_assert_not_null(baseline);
  ascii_stabilizer_t state = {0};
  cr_assert_eq(ascii_stabilizer_prepare(&state, 4, 4, 1), ASCIICHAT_OK);
  state.elapsed_ns = 16666667;
  ascii_stabilizer_t *prior = ascii_stabilizer_set_target(&state);
  char *stabilized = ascii_render(&image, " .#@", ASCII_COLOR_16, false, true, &simd_cells_scalar);
  ascii_stabilizer_set_target(prior);
  cr_assert_not_null(stabilized);
  cr_assert_str_eq(baseline, stabilized);
  SAFE_FREE(baseline);
  SAFE_FREE(stabilized);
  ascii_stabilizer_reset(&state);
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

Test(ascii_emission, glyph_matching_uses_shape_and_rejects_invalid_sizes) {
#if SIMD_SUPPORT_SVE
  if (simd_backend()->required_features == CPU_FEATURE_SVE)
    cr_assert(simd_cells_backend()->glyph_shape_correlation == glyph_shape_correlation_sve,
              "SVE dispatch must select SVE glyph correlation");
#endif
  rgb_pixel_t pixels[16 * 8] = {{0}};
  image_t image = {.w = 16, .h = 8, .pixels = pixels};
  char *black = test_render_glyph(&image, ASCII_COLOR_NONE, &simd_cells_scalar, NULL);
  cr_assert_not_null(black);
  cr_assert_str_eq(black, "  ");
  SAFE_FREE(black);

  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 16; ++x) {
      if ((x < 8 && x >= 3 && x <= 4) || (x >= 8 && y >= 3 && y <= 4))
        pixels[y * 16 + x] = (rgb_pixel_t){255, 255, 255};
    }
  }
  char *shapes = test_render_glyph(&image, ASCII_COLOR_NONE, &simd_cells_scalar, NULL);
  cr_assert_not_null(shapes);
  cr_assert_eq(strlen(shapes), 2);
  cr_assert(shapes[0] != shapes[1], "perpendicular strokes should select different glyphs");
  SAFE_FREE(shapes);
  // Compare the selected glyphs across all CPU backends on varied patches.
  rgb_pixel_t varied_pixels[128 * 64];
  for (int i = 0; i < 128 * 64; ++i)
    varied_pixels[i] = (rgb_pixel_t){(uint8_t)(i * 73), (uint8_t)(i * 37), (uint8_t)(i * 19)};
  image_t varied_image = {.w = 128, .h = 64, .pixels = varied_pixels};
  char *scalar_glyphs = test_render_glyph(&varied_image, ASCII_COLOR_NONE, &simd_cells_scalar, NULL);
  cr_assert_not_null(scalar_glyphs);
  terminal_cell_t expected_glyphs[16 * 8] = {0};
  decode(scalar_glyphs, expected_glyphs, 16, 8, false);
  simd_cells_t fallback = simd_cells_scalar;
  fallback.glyph_shape_correlation = NULL;
  char *fallback_glyphs = test_render_glyph(&varied_image, ASCII_COLOR_NONE, &fallback, NULL);
  cr_assert_not_null(fallback_glyphs);
  cr_assert_str_eq(fallback_glyphs, scalar_glyphs);
  SAFE_FREE(fallback_glyphs);
  for (size_t b = 0; b < sizeof(backends) / sizeof(backends[0]); ++b) {
    if ((platform_cpu_features() & backends[b].feature) != backends[b].feature)
      continue;
    for (int seed = 0; seed < 256; ++seed) {
      int16_t samples[16], mask[16];
      for (int i = 0; i < 16; ++i) {
        samples[i] = (int16_t)(((seed * 73 + i * 127) % 511) - 255);
        mask[i] = (int16_t)(((seed * 137 + i * 61) % 511) - 255);
      }
      cr_assert_eq(backends[b].luma->glyph_shape_correlation(samples, mask),
                   glyph_shape_correlation_scalar(samples, mask), "glyph dot product differs for backend %s",
                   backends[b].name);
    }
    char *output = test_render_glyph(&varied_image, ASCII_COLOR_NONE, backends[b].luma, NULL);
    cr_assert_not_null(output);
    terminal_cell_t actual_glyphs[16 * 8] = {0};
    decode(output, actual_glyphs, 16, 8, false);
    for (size_t i = 0; i < 16 * 8; ++i)
      cr_assert_str_eq(actual_glyphs[i].glyph, expected_glyphs[i].glyph, "backend %s", backends[b].name);
    SAFE_FREE(output);
  }
  SAFE_FREE(scalar_glyphs);
  char *restricted = test_render_glyph(&image, ASCII_COLOR_NONE, &simd_cells_scalar, "@#");
  cr_assert_not_null(restricted);
  cr_assert_eq(strlen(restricted), 2);
  cr_assert(strspn(restricted, "@#") == 2, "glyphs must come from the requested palette");
  SAFE_FREE(restricted);
  const char *invalid_palettes[] = {"", "@\x01", "@\x7f", "\xe2\x98\x83"};
  for (size_t i = 0; i < sizeof(invalid_palettes) / sizeof(invalid_palettes[0]); ++i) {
    cr_assert(!ascii_render_palette_supported(ASCII_RENDERER_GLYPH, invalid_palettes[i]));
    cr_assert_null(test_render_glyph(&image, ASCII_COLOR_NONE, &simd_cells_scalar, invalid_palettes[i]));
  }

  cr_assert(ascii_render_palette_supported(ASCII_RENDERER_GLYPH, " \xe2\x96\x88"));
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 16; ++x)
      pixels[y * 16 + x] = x < 8 ? (rgb_pixel_t){0, 0, 0} : (rgb_pixel_t){255, 255, 255};
  char *blocks = test_render_glyph(&image, ASCII_COLOR_NONE, &simd_cells_scalar, " \xe2\x96\x88");
  cr_assert_not_null(blocks);
  terminal_cell_t block_cells[2] = {0};
  decode(blocks, block_cells, 2, 1, false);
  cr_assert_str_eq(block_cells[0].glyph, " ");
  cr_assert_str_eq(block_cells[1].glyph, "\xe2\x96\x88");
  SAFE_FREE(blocks);

  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 16; ++x) {
      bool bright = x < 8 ? y < 4 : y >= 4;
      uint8_t value = bright ? 255 : 0;
      pixels[y * 16 + x] = (rgb_pixel_t){value, value, value};
    }
  blocks = test_render_glyph(&image, ASCII_COLOR_NONE, &simd_cells_scalar, "\xe2\x96\x80\xe2\x96\x84");
  cr_assert_not_null(blocks);
  decode(blocks, block_cells, 2, 1, false);
  cr_assert_str_eq(block_cells[0].glyph, "\xe2\x96\x80");
  cr_assert_str_eq(block_cells[1].glyph, "\xe2\x96\x84");
  SAFE_FREE(blocks);

  // Equal-luminance colors still have a visible boundary whose orientation
  // should influence the selected glyph.
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 16; ++x) {
      bool red = x < 8 ? y < 4 : x < 12;
      pixels[y * 16 + x] = red ? (rgb_pixel_t){150, 0, 0} : (rgb_pixel_t){0, 77, 0};
    }
  char *chromatic = test_render_glyph(&image, ASCII_COLOR_TRUE, &simd_cells_scalar, "\xe2\x96\x80\xe2\x96\x8c");
  cr_assert_not_null(chromatic);
  decode(chromatic, block_cells, 2, 1, true);
  cr_assert_str_eq(block_cells[0].glyph, "\xe2\x96\x80");
  cr_assert_str_eq(block_cells[1].glyph, "\xe2\x96\x8c");
  SAFE_FREE(chromatic);

  memset(pixels, 0, sizeof(pixels));
  for (int y = 0; y < 8; ++y)
    for (int x = 3; x <= 4; ++x)
      pixels[y * 16 + x] = (rgb_pixel_t){255, 255, 255};
  pixels[0] = (rgb_pixel_t){80, 110, 140};
  char *color = test_render_glyph(&image, ASCII_COLOR_TRUE, &simd_cells_scalar, "\xe2\x96\x88");
  cr_assert_not_null(color);
  terminal_cell_t cells[2] = {0};
  decode(color, cells, 2, 1, true);
  int red_ink = (cells[0].fg >> 16) & 255;
  int blue_ink = cells[0].fg & 255;
  cr_assert(red_ink >= 128 && blue_ink - red_ink <= 8, "glyph color must reflect the entire cell, not its first pixel");
  SAFE_FREE(color);

  for (int i = 0; i < 16 * 8; ++i)
    pixels[i] = (rgb_pixel_t){100, 100, 100};
  pixels[0] = (rgb_pixel_t){0, 255, 0};
  color = test_render_glyph(&image, ASCII_COLOR_TRUE, &simd_cells_scalar, "\xe2\x96\x88");
  cr_assert_not_null(color);
  decode(color, cells, 2, 1, true);
  int green_ink = (cells[0].fg >> 8) & 255;
  red_ink = (cells[0].fg >> 16) & 255;
  cr_assert(green_ink - red_ink <= 5, "one noisy pixel must not dominate the glyph color");
  SAFE_FREE(color);

  rgb_pixel_t split_pixels[64];
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 8; ++x)
      split_pixels[y * 8 + x] = x < 4 ? (rgb_pixel_t){255, 0, 0} : (rgb_pixel_t){0, 0, 255};
  image_t split = {.w = 8, .h = 8, .pixels = split_pixels};
  rgb_pixel_t flat_pixels[64];
  for (int i = 0; i < 64; ++i)
    flat_pixels[i] = (rgb_pixel_t){40, 80, 140};
  image_t flat = {.w = 8, .h = 8, .pixels = flat_pixels};
  char *flat_output = test_render_glyph_mode(&flat, ASCII_COLOR_TRUE, &simd_cells_scalar, NULL, true);
  cr_assert_not_null(flat_output);
  terminal_cell_t flat_cell[1] = {0};
  decode(flat_output, flat_cell, 1, 1, true);
  cr_assert_neq(flat_cell[0].glyph[0], 'B');
  cr_assert_neq(flat_cell[0].glyph[0], ' ');
  cr_assert_eq(flat_cell[0].bg, 0x28508c);
  cr_assert_eq(flat_cell[0].fg, 0xffffff);
  SAFE_FREE(flat_output);
  char *scalar_fitted = test_render_glyph_mode(&varied_image, ASCII_COLOR_TRUE, &simd_cells_scalar, NULL, true);
  cr_assert_not_null(scalar_fitted);
  for (size_t b = 0; b < sizeof(backends) / sizeof(backends[0]); ++b) {
    if ((platform_cpu_features() & backends[b].feature) != backends[b].feature)
      continue;
    char *other_fitted = test_render_glyph_mode(&varied_image, ASCII_COLOR_TRUE, backends[b].luma, NULL, true);
    cr_assert_not_null(other_fitted);
    cr_assert_str_eq(other_fitted, scalar_fitted, "fitted glyph output differs for backend %s", backends[b].name);
    SAFE_FREE(other_fitted);
  }
  SAFE_FREE(scalar_fitted);

  ascii_stabilizer_t fitted_history = {0};
  cr_assert_eq(ascii_stabilizer_prepare(&fitted_history, 1, 1, 7), ASCIICHAT_OK);
  fitted_history.elapsed_ns = 16000000;
  ascii_stabilizer_t *old_target = ascii_stabilizer_set_target(&fitted_history);
  char *first_fitted = test_render_glyph_mode(&split, ASCII_COLOR_TRUE, &simd_cells_scalar, "\xe2\x96\x8c", true);
  cr_assert_not_null(first_fitted);
  SAFE_FREE(first_fitted);
  for (int y = 0; y < 8; ++y)
    for (int x = 4; x < 8; ++x)
      split_pixels[y * 8 + x].b = 195;
  char *next_fitted = test_render_glyph_mode(&split, ASCII_COLOR_TRUE, &simd_cells_scalar, "\xe2\x96\x8c", true);
  cr_assert_not_null(next_fitted);
  terminal_cell_t stable_cell[1] = {0};
  decode(next_fitted, stable_cell, 1, 1, true);
  cr_assert_eq(stable_cell[0].bg & 255, 97, "substantial color motion must not blend the old background");
  SAFE_FREE(next_fitted);
  ascii_stabilizer_set_target(old_target);
  ascii_stabilizer_reset(&fitted_history);

  for (int i = 0; i < 64; ++i)
    split_pixels[i] = (rgb_pixel_t){150, 0, 0};
  cr_assert_eq(ascii_stabilizer_prepare(&fitted_history, 1, 1, 7), ASCIICHAT_OK);
  fitted_history.elapsed_ns = 16000000;
  old_target = ascii_stabilizer_set_target(&fitted_history);
  first_fitted = test_render_glyph_mode(&split, ASCII_COLOR_TRUE, &simd_cells_scalar, "@", true);
  cr_assert_not_null(first_fitted);
  SAFE_FREE(first_fitted);
  for (int i = 0; i < 64; ++i)
    split_pixels[i] = (rgb_pixel_t){0, 77, 0};
  next_fitted = test_render_glyph_mode(&split, ASCII_COLOR_TRUE, &simd_cells_scalar, "@", true);
  cr_assert_not_null(next_fitted);
  decode(next_fitted, stable_cell, 1, 1, true);
  cr_assert_eq(stable_cell[0].fg, 0xffffff, "dark backgrounds require white ink");
  cr_assert_eq((stable_cell[0].bg >> 16) & 255, 0, "isoluminant color changes must not blend old background");
  cr_assert_eq((stable_cell[0].bg >> 8) & 255, 77);
  SAFE_FREE(next_fitted);
  ascii_stabilizer_set_target(old_target);
  ascii_stabilizer_reset(&fitted_history);

  rgb_pixel_t colored_pixels[16 * 16];
  const rgb_pixel_t colors[4] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}};
  for (int y = 0; y < 16; ++y)
    for (int x = 0; x < 16; ++x)
      colored_pixels[y * 16 + x] = colors[(y / 8) * 2 + x / 8];
  image_t colored_image = {.w = 16, .h = 16, .pixels = colored_pixels};
  const ascii_color_t color_modes[] = {ASCII_COLOR_TRUE, ASCII_COLOR_256, ASCII_COLOR_16};
  for (size_t m = 0; m < sizeof(color_modes) / sizeof(color_modes[0]); ++m) {
    char *output = test_render_glyph(&colored_image, color_modes[m], &simd_cells_scalar, "@");
    cr_assert_not_null(output);
    terminal_cell_t result[4] = {0};
    decode(output, result, 2, 2, true);
    for (int i = 0; i < 4; ++i) {
      rgb_pixel_t p = colors[i];
      int expected = color_modes[m] == ASCII_COLOR_TRUE  ? (p.r << 16) | (p.g << 8) | p.b
                     : color_modes[m] == ASCII_COLOR_256 ? rgb_to_256color(p.r, p.g, p.b)
                                                         : rgb_to_16color(p.r, p.g, p.b);
      cr_assert_eq(result[i].glyph[0], '@');
      cr_assert_eq(result[i].fg, expected, "incorrect foreground for cell %d and mode %d", i, color_modes[m]);
    }
    SAFE_FREE(output);
  }
  image.w = 15;
  cr_assert_null(test_render_glyph(&image, ASCII_COLOR_NONE, &simd_cells_scalar, NULL));
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
  ascii_emission_temporal_history_tracks_motion_and_configuration();
  ascii_emission_temporal_stabilization_reduces_threshold_flicker();
  ascii_emission_temporal_background_and_halfblock_modes();
  ascii_emission_temporal_halfblock_reduces_monochrome_flicker();
  ascii_emission_optimized_glyph_scoring_matches_reference();
  ascii_emission_optimized_luminance_matches_generic_hysteresis();
  ascii_emission_temporal_first_frame_preserves_dithered_colors();
  ascii_emission_capability_dispatch_preserves_background();
  ascii_emission_glyph_matching_uses_shape_and_rejects_invalid_sizes();
  simd_caches_destroy_all();
  return 0;
}
#endif
