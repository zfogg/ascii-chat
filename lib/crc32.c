#include "crc32.h"
#include "common.h"
#include <string.h>

// Multi-architecture hardware acceleration support
#if defined(__aarch64__)
#include <arm_acle.h>
#define ARCH_ARM64
#elif defined(__x86_64__) && defined(HAVE_CRC32_HW)
#include <immintrin.h>
#include <cpuid.h>
#define ARCH_X86_64
#endif

// Check if CRC32 instructions are available at runtime
static bool crc32_hw_available = false;
static bool crc32_hw_checked = false;

static void check_crc32_hw_support(void) {
  if (crc32_hw_checked)
    return;

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
  unsigned int eax, ebx, ecx, edx;
  if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
    crc32_hw_available = (ecx & bit_SSE4_2) != 0;
  } else {
    crc32_hw_available = false;
  }
  // log_debug("Intel CRC32 hardware acceleration (SSE4.2): %s", crc32_hw_available ? "enabled" : "disabled");
#else
  crc32_hw_available = false;
  // log_debug("No hardware CRC32 acceleration available for this architecture");
#endif

  crc32_hw_checked = true;
}

#ifdef ARCH_ARM64
// ARM CRC32 hardware implementation
// NOTE: ARM CRC32 intrinsics process multi-byte values as little-endian integers.
// To match byte-by-byte processing, we need to process bytes individually.
// Using __crc32d/__crc32w on memcpy'd data gives different results due to byte ordering.
static uint32_t crc32_arm_hw(const void *data, size_t len) {
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFF;
  size_t i = 0;

  // Process 8 bytes at a time
  // ARM __crc32d processes bytes in reverse order within the 64-bit word
  // So we need to reverse the byte order to match sequential byte processing
  while (i + 8 <= len) {
    uint64_t chunk = 0;
    // Load bytes in reverse order for ARM's CRC32D instruction
    for (int j = 0; j < 8; j++) {
      chunk |= ((uint64_t)bytes[i + j]) << (j * 8);
    }
    crc = __crc32d(crc, chunk);
    i += 8;
  }

  // Process 4 bytes
  if (i + 4 <= len) {
    uint32_t chunk = 0;
    for (int j = 0; j < 4; j++) {
      chunk |= ((uint32_t)bytes[i + j]) << (j * 8);
    }
    crc = __crc32w(crc, chunk);
    i += 4;
  }

  // Process 2 bytes
  if (i + 2 <= len) {
    uint16_t chunk = 0;
    for (int j = 0; j < 2; j++) {
      chunk |= ((uint16_t)bytes[i + j]) << (j * 8);
    }
    crc = __crc32h(crc, chunk);
    i += 2;
  }

  // Process remaining bytes
  while (i < len) {
    crc = __crc32b(crc, bytes[i]);
    i++;
  }

  return ~crc;
}
#endif

#ifdef ARCH_X86_64
// Intel CRC32 hardware implementation using SSE4.2
// NOTE: Intel CRC32 intrinsics process multi-byte values as little-endian integers.
// Explicit byte loading to ensure consistent behavior across platforms.
static uint32_t crc32_intel_hw(const void *data, size_t len) {
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFF;
  size_t i = 0;

  // Process 8 bytes at a time
  while (i + 8 <= len) {
    uint64_t chunk = 0;
    // Load bytes in little-endian order
    for (int j = 0; j < 8; j++) {
      chunk |= ((uint64_t)bytes[i + j]) << (j * 8);
    }
    crc = (uint32_t)_mm_crc32_u64(crc, chunk);
    i += 8;
  }

  // Process 4 bytes
  if (i + 4 <= len) {
    uint32_t chunk = 0;
    for (int j = 0; j < 4; j++) {
      chunk |= ((uint32_t)bytes[i + j]) << (j * 8);
    }
    crc = _mm_crc32_u32(crc, chunk);
    i += 4;
  }

  // Process 2 bytes
  if (i + 2 <= len) {
    uint16_t chunk = 0;
    for (int j = 0; j < 2; j++) {
      chunk |= ((uint16_t)bytes[i + j]) << (j * 8);
    }
    crc = _mm_crc32_u16(crc, chunk);
    i += 2;
  }

  // Process remaining bytes
  while (i < len) {
    crc = _mm_crc32_u8(crc, bytes[i]);
    i++;
  }

  return ~crc;
}
#endif

// Multi-architecture hardware-accelerated CRC32
uint32_t asciichat_crc32_hw(const void *data, size_t len) {
  check_crc32_hw_support();

  if (!crc32_hw_available) {
    return asciichat_crc32_sw(data, len);
  }

#ifdef ARCH_ARM64
  return crc32_arm_hw(data, len);
#elif defined(ARCH_X86_64)
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
