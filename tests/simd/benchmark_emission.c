/** Isolated before/after renderer benchmark. CSV goes to stdout. */
#include <ascii-chat/common.h>
#include <ascii-chat/video/ascii/scalar/foreground.h>
#include <ascii-chat/video/ascii/simd/dispatch.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/util/time.h>
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

static size_t allocations;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void *__wrap_malloc(size_t size) {
  ++allocations;
  return __real_malloc(size);
}
void *__wrap_calloc(size_t count, size_t size) {
  ++allocations;
  return __real_calloc(count, size);
}
void *__wrap_realloc(void *ptr, size_t size) {
  ++allocations;
  return __real_realloc(ptr, size);
}

static char *scalar(const image_t *image, bool background, bool indexed, const char *palette) {
  (void)background;
  return indexed ? image_print_256color(image, palette) : image_print_color(image, palette);
}
static const struct {
  const char *name;
  uint32_t feature;
  char *(*render)(const image_t *, bool, bool, const char *);
} variants[] = {
    {"scalar", 0, scalar},
#if SIMD_SUPPORT_SSE2
    {"sse2", CPU_FEATURE_SSE2, render_ascii_color_sse2},
#endif
#if SIMD_SUPPORT_SSSE3
    {"ssse3", CPU_FEATURE_SSSE3, render_ascii_color_ssse3},
#endif
#if SIMD_SUPPORT_AVX2
    {"avx2", CPU_FEATURE_AVX2, render_ascii_color_avx2},
#endif
#if SIMD_SUPPORT_NEON
    {"neon", CPU_FEATURE_NEON, render_ascii_color_neon},
#endif
#if SIMD_SUPPORT_SVE
    {"sve", CPU_FEATURE_SVE, render_ascii_color_sve},
#endif
};

int main(int argc, char **argv) {
  setbuf(stdout, NULL);
  enum { WIDTH = 161, HEIGHT = 61, FRAMES = 120 };
  rgb_pixel_t pixels[WIDTH * HEIGHT];
  image_t image = {.w = WIDTH, .h = HEIGHT, .pixels = pixels};
  uint32_t features = platform_cpu_features();
  printf("backend,pattern,color,ns_per_frame,allocations_per_frame,bytes_per_frame\n");
  for (size_t b = 0; b < sizeof(variants) / sizeof(variants[0]); ++b) {
    if ((features & variants[b].feature) != variants[b].feature)
      continue;
    if (argc == 2 && strcmp(argv[1], variants[b].name))
      continue;
    for (int pattern = 0; pattern < 2; ++pattern) {
      for (size_t i = 0; i < WIDTH * HEIGHT; ++i) {
        size_t value = pattern ? i : i / 40;
        pixels[i] = (rgb_pixel_t){(uint8_t)(value * 37), (uint8_t)(value * 71), (uint8_t)(value * 13)};
      }
      for (int indexed = 0; indexed < 2; ++indexed) {
        // Warm shared palette and color caches before counting allocations.
        char *warm = variants[b].render(&image, false, indexed, " .:+#@");
        SAFE_FREE(warm);
        allocations = 0;
        size_t bytes = 0;
        uint64_t start = time_get_ns();
        for (int frame = 0; frame < FRAMES; ++frame) {
          char *out = variants[b].render(&image, false, indexed, " .:+#@");
          if (!out)
            return 1;
          bytes += strlen(out);
          SAFE_FREE(out);
        }
        uint64_t elapsed = time_get_ns() - start;
        printf("%s,%s,%s,%llu,%.2f,%zu\n", variants[b].name, pattern ? "noise" : "runs", indexed ? "256" : "truecolor",
               (unsigned long long)(elapsed / FRAMES), (double)allocations / FRAMES, bytes / FRAMES);
      }
    }
  }
  simd_caches_destroy_all();
  return 0;
}
