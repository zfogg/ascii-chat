#include <ascii-chat/video/ascii/simd/dispatch.h>
#include <string.h>

static const simd_backend_t backends[] = {
#if SIMD_SUPPORT_SVE
    {"sve", CPU_FEATURE_SVE, simd_luminance_sve},
#endif
#if SIMD_SUPPORT_NEON
    {"neon", CPU_FEATURE_NEON, simd_luminance_neon},
#endif
#if SIMD_SUPPORT_AVX2
    {"avx2", CPU_FEATURE_AVX2, simd_luminance_avx2},
#endif
#if SIMD_SUPPORT_SSSE3
    {"ssse3", CPU_FEATURE_SSSE3, simd_luminance_ssse3},
#endif
#if SIMD_SUPPORT_SSE2
    {"sse2", CPU_FEATURE_SSE2, simd_luminance_sse2},
#endif
    {"scalar", 0, simd_luminance_scalar},
};

const simd_backend_t *simd_select_backend(uint32_t features) {
  for (size_t i = 0; i < sizeof(backends) / sizeof(backends[0]); i++) {
    if ((features & backends[i].required_features) == backends[i].required_features)
      return &backends[i];
  }
  return &backends[sizeof(backends) / sizeof(backends[0]) - 1];
}

const simd_backend_t *simd_backend(void) {
  static _Thread_local const simd_backend_t *selected;
  if (!selected) {
    uint32_t features = platform_cpu_features();
    selected = simd_select_backend(features);
    const char *requested = platform_simd_override();
    if (requested && requested[0] && strcmp(requested, "auto") != 0) {
      // A diagnostic override can never bypass the CPU/OS safety check.
      selected = simd_select_backend(0);
      for (size_t i = 0; i < sizeof(backends) / sizeof(backends[0]); i++) {
        if (strcmp(requested, backends[i].name) == 0 &&
            (features & backends[i].required_features) == backends[i].required_features) {
          selected = &backends[i];
          break;
        }
      }
    }
  }
  return selected;
}

void simd_luminance_scalar(const uint8_t *rgb, uint8_t *out, size_t count) {
  for (size_t i = 0; i < count; i++)
    out[i] = (uint8_t)((77u * rgb[3 * i] + 150u * rgb[3 * i + 1] + 29u * rgb[3 * i + 2] + 128u) >> 8);
}
