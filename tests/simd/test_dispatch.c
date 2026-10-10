#include <ascii-chat/video/ascii/simd/dispatch.h>
#include <stdio.h>
#include <string.h>

static const simd_backend_t variants[] = {
    {"scalar", 0, simd_luminance_scalar},
#if SIMD_SUPPORT_SSE2
    {"sse2", CPU_FEATURE_SSE2, simd_luminance_sse2},
#endif
#if SIMD_SUPPORT_SSSE3
    {"ssse3", CPU_FEATURE_SSSE3, simd_luminance_ssse3},
#endif
#if SIMD_SUPPORT_AVX2
    {"avx2", CPU_FEATURE_AVX2, simd_luminance_avx2},
#endif
#if SIMD_SUPPORT_NEON
    {"neon", CPU_FEATURE_NEON, simd_luminance_neon},
#endif
#if SIMD_SUPPORT_SVE
    {"sve", CPU_FEATURE_SVE, simd_luminance_sve},
#endif
};

int main(int argc, char **argv) {
  uint32_t features = platform_cpu_features();
  const simd_backend_t *selected = simd_backend();
  printf("features=0x%x selected=%s\n", features, selected->name);
  if (argc == 2 && strcmp(argv[1], selected->name) != 0)
    return 1;
  if (simd_backend() != selected || strcmp(simd_select_backend(0)->name, "scalar") != 0)
    return 2;
  // All synthetic capability combinations must choose the highest compiled legal backend.
  for (uint32_t mask = 0; mask < 64; mask++) {
    const simd_backend_t *expected = &variants[0];
    for (size_t v = 1; v < sizeof(variants) / sizeof(variants[0]); v++) {
      if ((mask & variants[v].required_features) == variants[v].required_features)
        expected = &variants[v];
    }
    if (strcmp(simd_select_backend(mask)->name, expected->name) != 0)
      return 3;
  }
  uint8_t rgb[3 * 1025 + 3], out[1025 + 2], expected[1025];
  uint32_t rng = 17;
  for (size_t i = 0; i < sizeof(rgb); i++) {
    rng = rng * 1664525u + 1013904223u;
    rgb[i] = (uint8_t)(rng >> 24);
  }
  for (size_t v = 0; v < sizeof(variants) / sizeof(variants[0]); v++) {
    const simd_backend_t *backend = &variants[v];
    if ((features & backend->required_features) != backend->required_features)
      continue;
    // Every tail size, unaligned buffers, zero count, and output sentinels.
    for (size_t n = 0; n <= 1025; n++) {
      memset(out, 0xa5, sizeof(out));
      simd_luminance_scalar(rgb + 1, expected, n);
      backend->luminance(rgb + 1, out + 1, n);
      if (out[0] != 0xa5 || out[n + 1] != 0xa5 || memcmp(expected, out + 1, n) != 0) {
        fprintf(stderr, "%s: mismatch at count=%zu\n", backend->name, n);
        return 4;
      }
    }
    // Exhaust the complete RGB24 domain; catches overflow and lane ordering errors.
    for (uint32_t base = 0; base < (1u << 24); base += 1024) {
      for (uint32_t j = 0; j < 1024; j++) {
        uint32_t pixel = base + j;
        rgb[3 * j] = (uint8_t)(pixel >> 16);
        rgb[3 * j + 1] = (uint8_t)(pixel >> 8);
        rgb[3 * j + 2] = (uint8_t)pixel;
      }
      simd_luminance_scalar(rgb, expected, 1024);
      backend->luminance(rgb, out, 1024);
      if (memcmp(expected, out, 1024) != 0)
        return 5;
    }
    printf("%s: tails, guards, unaligned and all 16777216 RGB values passed\n", backend->name);
  }
  return 0;
}
