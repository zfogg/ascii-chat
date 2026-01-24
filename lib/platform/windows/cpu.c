/**
 * @file platform/windows/cpu.c
 * @brief Windows CPU feature detection implementation
 */

#include "../cpu.h"
#include <stdatomic.h>
#include <intrin.h>

/* CPU feature detection state */
static atomic_bool g_cpu_features_checked = false;
static bool g_cpu_crc32 = false;
static bool g_cpu_sse42 = false;
static bool g_cpu_avx2 = false;

static void check_cpu_features(void) {
  /* Fast path: check if already initialized */
  if (atomic_load(&g_cpu_features_checked)) {
    return;
  }

  /* Try to claim initialization (only one thread will succeed) */
  bool expected = false;
  if (!atomic_compare_exchange_strong(&g_cpu_features_checked, &expected, true)) {
    /* Another thread is initializing or already initialized, wait for it */
    int spin_count = 0;
    while (!atomic_load(&g_cpu_features_checked)) {
      spin_count++;
      if (spin_count > 100) {
        Sleep(1); /* Windows Sleep in ms */
        spin_count = 0;
      }
    }
    return;
  }

  /* This thread won the race and will perform initialization */

  int cpu_info[4];

  /* Check SSE4.2 and CRC32 support */
  __cpuid(cpu_info, 1);
  /* SSE4.2 is bit 20 of ECX register, includes CRC32 instruction */
  g_cpu_sse42 = (cpu_info[2] & (1 << 20)) != 0;
  g_cpu_crc32 = g_cpu_sse42;

  /* Check AVX2 support */
  __cpuid(cpu_info, 7);
  /* AVX2 is bit 5 of EBX register when calling CPUID with leaf 7 */
  g_cpu_avx2 = (cpu_info[1] & (1 << 5)) != 0;
}

bool cpu_has_crc32(void) {
  check_cpu_features();
  return g_cpu_crc32;
}

bool cpu_has_sse42(void) {
  check_cpu_features();
  return g_cpu_sse42;
}

bool cpu_has_avx2(void) {
  check_cpu_features();
  return g_cpu_avx2;
}

bool cpu_has_neon(void) {
  /* Windows on ARM64 might support NEON, but on x86-64 it doesn't */
  return false;
}
