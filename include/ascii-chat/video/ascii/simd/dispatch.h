#pragma once

#include <stddef.h>
#include <stdint.h>
#include <ascii-chat/platform/cpu.h>

/** RGB24 to rounded BT.601 luminance. Exactly count pixels are read/written. */
typedef void (*simd_luminance_fn)(const uint8_t *rgb, uint8_t *out, size_t count);
typedef struct {
  const char *name;
  uint32_t required_features;
  simd_luminance_fn luminance;
} simd_backend_t;

/** Select only from compiled implementations whose requirements are satisfied. */
const simd_backend_t *simd_select_backend(uint32_t features);
/** Immutable dispatch table cached per thread; no startup constructors required. */
const simd_backend_t *simd_backend(void);
void simd_luminance_scalar(const uint8_t *rgb, uint8_t *out, size_t count);
void simd_luminance_sse2(const uint8_t *rgb, uint8_t *out, size_t count);
void simd_luminance_ssse3(const uint8_t *rgb, uint8_t *out, size_t count);
void simd_luminance_avx2(const uint8_t *rgb, uint8_t *out, size_t count);
void simd_luminance_neon(const uint8_t *rgb, uint8_t *out, size_t count);
void simd_luminance_sve(const uint8_t *rgb, uint8_t *out, size_t count);
