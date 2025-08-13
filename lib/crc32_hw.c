#include "crc32_hw.h"
#include "common.h"

#ifdef __aarch64__
#include <arm_acle.h>

// Check if CRC32 instructions are available at runtime
static bool crc32_hw_available = false;
static bool crc32_hw_checked = false;

static void check_crc32_hw_support(void) {
  if (crc32_hw_checked)
    return;

// On Apple Silicon, CRC32 is always available
// On other ARM64 systems, we could check HWCAP_CRC32
#ifdef __APPLE__
  crc32_hw_available = true;
#else
  // For other ARM64 systems, we'd need to check auxiliary vector
  // For now, assume available (can be made more sophisticated)
  crc32_hw_available = true;
#endif

  crc32_hw_checked = true;
  log_debug("ARM CRC32 hardware acceleration: %s", crc32_hw_available ? "enabled" : "disabled");
}

// Hardware-accelerated CRC32 using ARM CRC32 instructions
uint32_t asciichat_crc32_hw(const void *data, size_t len) {
  check_crc32_hw_support();

  if (!crc32_hw_available) {
    return asciichat_crc32_sw(data, len);
  }

  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFF;
  size_t i = 0;

  // Process 8 bytes at a time using CRC32X (64-bit)
  while (i + 8 <= len) {
    uint64_t chunk;
    memcpy(&chunk, bytes + i, 8);
    crc = __crc32d(crc, chunk);
    i += 8;
  }

  // Process 4 bytes using CRC32W (32-bit)
  if (i + 4 <= len) {
    uint32_t chunk;
    memcpy(&chunk, bytes + i, 4);
    crc = __crc32w(crc, chunk);
    i += 4;
  }

  // Process 2 bytes using CRC32H (16-bit)
  if (i + 2 <= len) {
    uint16_t chunk;
    memcpy(&chunk, bytes + i, 2);
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

bool crc32_hw_is_available(void) {
  check_crc32_hw_support();
  return crc32_hw_available;
}

#else
// Non-ARM64 fallback
uint32_t asciichat_crc32_hw(const void *data, size_t len) {
  return asciichat_crc32_sw(data, len);
}

bool crc32_hw_is_available(void) {
  return false;
}
#endif

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