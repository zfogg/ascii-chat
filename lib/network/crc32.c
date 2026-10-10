/**
 * @file crc32.c
 * @ingroup util
 * @brief ⚡ Hardware-accelerated CRC32 checksum with ARM64 and x86_64 CPU feature detection
 */

#include <ascii-chat/network/crc32.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/cpu.h>
#include <string.h>
#include <stdio.h>
#include <ascii-chat/atomic.h>

// Multi-architecture hardware acceleration support
#if defined(__aarch64__) && defined(HAVE_CRC32_HW)
#include <arm_acle.h>
#define ARCH_ARM64
#elif defined(__x86_64__) && defined(HAVE_CRC32_HW)
#include <immintrin.h>
#ifdef _WIN32
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#define ARCH_X86_64
#endif

// Cache per thread to avoid publishing a partially initialized feature result.
static _Thread_local bool crc32_hw_available;
static _Thread_local bool crc32_hw_checked;
static void check_crc32_hw_support(void) {
  if (!crc32_hw_checked) {
#if defined(ARCH_ARM64) || defined(ARCH_X86_64)
    crc32_hw_available = (platform_cpu_features() & CPU_FEATURE_CRC32) != 0;
#endif
    crc32_hw_checked = true;
  }
}

#ifdef ARCH_ARM64
// ARM CRC32-C hardware implementation using Castagnoli polynomial
// IMPORTANT: Use __crc32cb (CRC32-C) NOT __crc32b (IEEE 802.3)
// __crc32cb uses the Castagnoli polynomial (0x1EDC6F41), matching:
//   - Intel _mm_crc32_* intrinsics
//   - Our software fallback asciichat_crc32_sw()
// Process byte-by-byte to ensure cross-platform consistency with x86
__attribute__((target("crc"), noinline)) static uint32_t crc32_arm_hw(uint32_t previous, const void *data, size_t len) {
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t crc = ~previous;

  // Process all bytes one at a time for guaranteed consistency
  // Use CRC32-C intrinsics (__crc32cb) not CRC32 (__crc32b)
  for (size_t i = 0; i < len; i++) {
    crc = __crc32cb(crc, bytes[i]);
  }

  return ~crc;
}
#endif

#ifdef ARCH_X86_64
// Intel CRC32 hardware implementation using SSE4.2
// Process byte-by-byte to ensure cross-platform consistency with ARM
__attribute__((target("sse4.2"), noinline)) static uint32_t crc32_intel_hw(uint32_t previous, const void *data,
                                                                           size_t len) {
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t crc = ~previous;

  // Process all bytes one at a time for guaranteed consistency
  for (size_t i = 0; i < len; i++) {
    crc = _mm_crc32_u8(crc, bytes[i]);
  }

  return ~crc;
}
#endif

// Multi-architecture hardware-accelerated CRC32
uint32_t asciichat_crc32_update(uint32_t previous, const void *data, size_t len) {
  check_crc32_hw_support();

  if (!crc32_hw_available) {
    // DEBUG: Log fallback to software
    static _Thread_local bool logged_fallback = false;
    if (!logged_fallback) {
      log_debug("Using software CRC32 (no hardware acceleration)");
      logged_fallback = true;
    }
    return asciichat_crc32_sw_update(previous, data, len);
  }

#ifdef ARCH_ARM64
  static _Thread_local bool logged_arm = false;
  if (!logged_arm) {
    log_debug("Using ARM64 hardware CRC32");
    logged_arm = true;
  }
  return crc32_arm_hw(previous, data, len);
#elif defined(ARCH_X86_64)
  static _Thread_local bool logged_intel = false;
  if (!logged_intel) {
    log_debug("Using Intel x86_64 hardware CRC32 (SSE4.2)");
    logged_intel = true;
  }
  return crc32_intel_hw(previous, data, len);
#else
  return asciichat_crc32_sw_update(previous, data, len);
#endif
}

bool crc32_hw_is_available(void) {
  check_crc32_hw_support();
  return crc32_hw_available;
}

// Software fallback implementation using CRC32-C (Castagnoli) polynomial
// This matches the hardware implementations (__crc32* and _mm_crc32_*)
uint32_t asciichat_crc32_sw_update(uint32_t previous, const void *data, size_t len) {
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t crc = ~previous;

  // CRC32-C (Castagnoli) polynomial: 0x1EDC6F41
  // Reversed (for LSB-first): 0x82F63B78
  for (size_t i = 0; i < len; i++) {
    crc ^= bytes[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0x82F63B78; // CRC32-C polynomial (reversed)
      } else {
        crc >>= 1;
      }
    }
  }

  return ~crc;
}

uint32_t asciichat_crc32_hw(const void *data, size_t len) {
  return asciichat_crc32_update(0, data, len);
}
uint32_t asciichat_crc32_sw(const void *data, size_t len) {
  return asciichat_crc32_sw_update(0, data, len);
}
