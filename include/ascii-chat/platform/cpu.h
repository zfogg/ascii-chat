#pragma once

#include <stdint.h>

/** CPU features usable by this process, including operating system support. */
enum {
  CPU_FEATURE_SSE2 = 1u << 0,
  CPU_FEATURE_SSSE3 = 1u << 1,
  CPU_FEATURE_AVX2 = 1u << 2,
  CPU_FEATURE_NEON = 1u << 3,
  CPU_FEATURE_SVE = 1u << 4,
  CPU_FEATURE_CRC32 = 1u << 5,
};

uint32_t platform_cpu_features(void);
