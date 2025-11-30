/**
 * @file crc32.c
 * @ingroup util
 * @brief ⚡ Hardware-accelerated CRC32 checksum with ARM64 and x86_64 CPU feature detection
 */

#include "crc32.h"
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

// Multi-architecture hardware acceleration support
#if defined(__aarch64__)
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

// Check if CRC32 instructions are available at runtime
static bool crc32_hw_available = false;
static atomic_bool crc32_hw_checked = false;

static void check_crc32_hw_support(void) {
  // Fast path: check if already initialized (atomic read)
  if (atomic_load(&crc32_hw_checked)) {
    return;
  }

  // Try to claim initialization (only one thread will succeed)
  bool expected = false;
  if (!atomic_compare_exchange_strong(&crc32_hw_checked, &expected, true)) {
    // Another thread is initializing or already initialized, wait for it
    // Spin briefly then check again (most common case: already initialized)
    while (!atomic_load(&crc32_hw_checked)) {
      // Brief spin wait - initialization is very fast
    }
    return;
  }

  // This thread won the race and will perform initialization

  // clang-format off
#ifdef ARCH_ARM64
// On Apple Silicon, CRC32 is always available
// On other ARM64 systems, we could check HWCAP_CRC32
#ifdef __APPLE__
  crc32_hw_available = true;
#else
  // For other ARM64 systems, we'd need to check auxiliary vector
  // For now, assume available (can be made more sophisticated)
  crc32_hw_available = true;
#endif
  // log_debug("ARM CRC32 hardware acceleration: %s", crc32_hw_available ? "enabled" : "disabled");
#elif defined(ARCH_X86_64)
  // Check for SSE4.2 support (includes CRC32 instruction)
#ifdef _WIN32
  int cpu_info[4];
  __cpuid(cpu_info, 1);
  // SSE4.2 is bit 20 of ECX
  crc32_hw_available = (cpu_info[2] & (1 << 20)) != 0;
#else
  unsigned int eax, ebx, ecx, edx;
  if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
    crc32_hw_available = (ecx & bit_SSE4_2) != 0;
  } else {
    crc32_hw_available = false;
  }
#endif
  // log_debug("Intel CRC32 hardware acceleration (SSE4.2): %s", crc32_hw_available ? "enabled" : "disabled");
#else
  crc32_hw_available = false;
  // log_debug("No hardware CRC32 acceleration available for this architecture");
#endif // clang-format on

  // Initialization complete - flag was already set to true by atomic_compare_exchange_strong above
  // The compare-exchange with memory_order_seq_cst ensures all writes to crc32_hw_available
  // are visible to other threads when they see crc32_hw_checked == true
}

#ifdef ARCH_ARM64
// ARM CRC32-C hardware implementation using Castagnoli polynomial
// IMPORTANT: Use __crc32cb (CRC32-C) NOT __crc32b (IEEE 802.3)
// __crc32cb uses the Castagnoli polynomial (0x1EDC6F41), matching:
//   - Intel _mm_crc32_* intrinsics
//   - Our software fallback asciichat_crc32_sw()
// Process byte-by-byte to ensure cross-platform consistency with x86
static uint32_t crc32_arm_hw(const void *data, size_t len) {
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFF;

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
static uint32_t crc32_intel_hw(const void *data, size_t len) {
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFF;

  // Process all bytes one at a time for guaranteed consistency
  for (size_t i = 0; i < len; i++) {
    crc = _mm_crc32_u8(crc, bytes[i]);
  }

  return ~crc;
}
#endif

// Multi-architecture hardware-accelerated CRC32
uint32_t asciichat_crc32_hw(const void *data, size_t len) {
  check_crc32_hw_support();

  if (!crc32_hw_available) {
    // DEBUG: Log fallback to software
    static bool logged_fallback = false;
    if (!logged_fallback) {
      fprintf(stderr, "[CRC32 DEBUG] Using software CRC32 (no hardware acceleration)\n");
      logged_fallback = true;
    }
    return asciichat_crc32_sw(data, len);
  }

#ifdef ARCH_ARM64
  static bool logged_arm = false;
  if (!logged_arm) {
    fprintf(stderr, "[CRC32 DEBUG] Using ARM64 hardware CRC32\n");
    logged_arm = true;
  }
  return crc32_arm_hw(data, len);
#elif defined(ARCH_X86_64)
  static bool logged_intel = false;
  if (!logged_intel) {
    fprintf(stderr, "[CRC32 DEBUG] Using Intel x86_64 hardware CRC32 (SSE4.2)\n");
    logged_intel = true;
  }
  return crc32_intel_hw(data, len);
#else
  return asciichat_crc32_sw(data, len);
#endif
}

bool crc32_hw_is_available(void) {
  check_crc32_hw_support();
  return crc32_hw_available;
}

// Software fallback implementation using CRC32-C (Castagnoli) polynomial
// This matches the hardware implementations (__crc32* and _mm_crc32_*)
uint32_t asciichat_crc32_sw(const void *data, size_t len) {
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFF;

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
