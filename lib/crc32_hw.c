#include "crc32_hw.h"
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
static uint32_t crc32_arm_hw(const void *data, size_t len) {
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFF;
  size_t i = 0;

  // Process 8 bytes at a time using CRC32X (64-bit)
  while (i + 8 <= len) {
    uint64_t chunk;
    SAFE_MEMCPY(&chunk, 8, bytes + i, 8);
    crc = __crc32d(crc, chunk);
    i += 8;
  }

  // Process 4 bytes using CRC32W (32-bit)
  if (i + 4 <= len) {
    uint32_t chunk;
    SAFE_MEMCPY(&chunk, 4, bytes + i, 4);
    crc = __crc32w(crc, chunk);
    i += 4;
  }

  // Process 2 bytes using CRC32H (16-bit)
  if (i + 2 <= len) {
    uint16_t chunk;
    SAFE_MEMCPY(&chunk, 2, bytes + i, 2);
    crc = __crc32h(crc, chunk);
    i += 2;
  }

  // Process remaining bytes using CRC32B (8-bit)
  while (i < len) {
    crc = __crc32b(crc, bytes[i]);
    i++;
  }

  return ~crc;
}
#endif

#ifdef ARCH_X86_64
// Intel CRC32 hardware implementation using SSE4.2
static uint32_t crc32_intel_hw(const void *data, size_t len) {
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFF;
  size_t i = 0;

  // Process 8 bytes at a time using CRC32Q (64-bit)
  while (i + 8 <= len) {
    uint64_t chunk;
    SAFE_MEMCPY(&chunk, 8, bytes + i, 8);
    crc = (uint32_t)_mm_crc32_u64(crc, chunk);
    i += 8;
  }

  // Process 4 bytes using CRC32L (32-bit)
  if (i + 4 <= len) {
    uint32_t chunk;
    SAFE_MEMCPY(&chunk, 4, bytes + i, 4);
    crc = _mm_crc32_u32(crc, chunk);
    i += 4;
  }

  // Process 2 bytes using CRC32W (16-bit)
  if (i + 2 <= len) {
    uint16_t chunk;
    SAFE_MEMCPY(&chunk, 2, bytes + i, 2);
    crc = _mm_crc32_u16(crc, chunk);
    i += 2;
  }

  // Process remaining bytes using CRC32B (8-bit)
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

// Software fallback implementation (original)
uint32_t asciichat_crc32_sw(const void *data, size_t len) {
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFF;

  for (size_t i = 0; i < len; i++) {
    crc ^= bytes[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0xEDB88320;
      } else {
        crc >>= 1;
      }
    }
  }

  return ~crc;
}
