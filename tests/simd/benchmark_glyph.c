/* Renderer-only benchmark; input is a 640x256 RGB24 image for an 80x32 viewport. */
#include <ascii-chat/common.h>
#include <ascii-chat/video/ascii/render/render.h>
#include <ascii-chat/video/ascii/simd/cells.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double seconds(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  FILE *f = fopen(argv[1], "rb");
  if (!f)
    return 2;
  rgb_pixel_t *high = SAFE_CALLOC(640 * 256, sizeof(*high), rgb_pixel_t *);
  rgb_pixel_t *low = SAFE_CALLOC(80 * 32, sizeof(*low), rgb_pixel_t *);
  if (!high || !low || fread(high, sizeof(*high), 640 * 256, f) != 640 * 256)
    return 2;
  fclose(f);
  for (int y = 0; y < 32; ++y)
    for (int x = 0; x < 80; ++x)
      low[y * 80 + x] = high[y * 8 * 640 + x * 8];
  image_t source = {.w = 640, .h = 256, .pixels = high};
  image_t baseline = {.w = 80, .h = 32, .pixels = low};
  const simd_cells_t *ops = simd_cells_backend();
#if SIMD_SUPPORT_AVX2
  printf("backend: %s\n", ops == &simd_cells_avx2 ? "AVX2" : "other");
#endif
  for (int variant = 0; variant < 2; ++variant) {
    size_t bytes = 0;
    const int rounds = 200;
    double begin = seconds();
    for (int i = 0; i < rounds + 5; ++i) {
      if (i == 5)
        begin = seconds();
      char *out = variant ? ascii_render_image(&source, &(ascii_render_options_t){.kind = ASCII_RENDERER_GLYPH,
                                                                                  .color = ASCII_COLOR_TRUE,
                                                                                  .backend = ops})
                          : ascii_render(&baseline, "   ...',;:clodxkO0KXNWM", ASCII_COLOR_TRUE, false, false, ops);
      if (!out || !*out)
        return 3;
      bytes += strlen(out);
      SAFE_FREE(out);
    }
    printf("%s: %.3f ms/frame (%d measured frames, %zu bytes incl. warmup)\n", variant ? "glyph" : "baseline",
           (seconds() - begin) * 1000 / rounds, rounds, bytes);
  }
  SAFE_FREE(high);
  SAFE_FREE(low);
  return 0;
}
