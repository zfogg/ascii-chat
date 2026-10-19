#include <ascii-chat/common.h>
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/util/time.h>

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
static const struct {
  const char *name;
  uint32_t feature;
  const simd_cells_t *kernel;
} backends[] = {
    {"scalar", 0, &simd_cells_scalar},
#if SIMD_SUPPORT_SSE2
    {"sse2", CPU_FEATURE_SSE2, &simd_cells_sse2},
#endif
#if SIMD_SUPPORT_SSSE3
    {"ssse3", CPU_FEATURE_SSSE3, &simd_cells_ssse3},
#endif
#if SIMD_SUPPORT_AVX2
    {"avx2", CPU_FEATURE_AVX2, &simd_cells_avx2},
#endif
#if SIMD_SUPPORT_NEON
    {"neon", CPU_FEATURE_NEON, &simd_cells_neon},
#endif
#if SIMD_SUPPORT_SVE
    {"sve", CPU_FEATURE_SVE, &simd_cells_sve},
#endif
};
int main(void) {
  enum { WIDTH = 161, HEIGHT = 61, FRAMES = 120 };
  uint8_t rgb[WIDTH * HEIGHT * 3];
  printf("backend,pattern,color,ns_per_frame,allocations_per_frame,bytes_per_frame\n");
  const char *modes[] = {"none", "16", "256", "truecolor"};
  for (size_t b = 0; b < sizeof(backends) / sizeof(backends[0]); ++b) {
    if ((platform_cpu_features() & backends[b].feature) != backends[b].feature)
      continue;
    for (int pattern = 0; pattern < 2; ++pattern) {
      for (size_t i = 0; i < WIDTH * HEIGHT; ++i) {
        size_t v = pattern ? i : i / 40;
        rgb[3 * i] = (uint8_t)(v * 37);
        rgb[3 * i + 1] = (uint8_t)(v * 71);
        rgb[3 * i + 2] = (uint8_t)(v * 13);
      }
      for (ascii_color_t mode = ASCII_COLOR_NONE; mode <= ASCII_COLOR_TRUE; ++mode) {
        char *warm = ascii_render_halfblocks(rgb, WIDTH, HEIGHT, 0, mode, backends[b].kernel);
        SAFE_FREE(warm);
        allocations = 0;
        size_t bytes = 0;
        uint64_t start = time_get_ns();
        for (unsigned frame = 0; frame < FRAMES; ++frame) {
          char *out = ascii_render_halfblocks(rgb, WIDTH, HEIGHT, 0, mode, backends[b].kernel);
          if (!out)
            return 1;
          bytes += strlen(out);
          SAFE_FREE(out);
        }
        uint64_t elapsed = time_get_ns() - start;
        printf("%s,%s,%s,%llu,%.2f,%zu\n", backends[b].name, pattern ? "noise" : "runs", modes[mode],
               (unsigned long long)(elapsed / FRAMES), (double)allocations / FRAMES, bytes / FRAMES);
      }
    }
  }
  return 0;
}
