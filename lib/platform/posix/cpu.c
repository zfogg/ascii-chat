/**
 * @file platform/posix/cpu.c
 * @brief POSIX CPU feature detection implementation
 */

#include "../cpu.h"
#include <stdatomic.h>

/* CPU feature detection state */
static atomic_bool g_cpu_features_checked = false;
static bool g_cpu_crc32 = false;
static bool g_cpu_sse42 = false;
static bool g_cpu_avx2 = false;
static bool g_cpu_neon = false;

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
        platform_sleep_usec(1);
        spin_count = 0;
      }
    }
    return;
  }

  /* This thread won the race and will perform initialization */

#if defined(__aarch64__)
  /* ARM64: CRC32 and NEON support */
  g_cpu_crc32 = true; /* CRC32 always available on modern ARM64 */
  g_cpu_neon = true;  /* NEON always available on ARM64 */
#elif defined(__x86_64__)
  /* x86-64: Use CPUID to detect features */
#ifdef _WIN32
  int cpu_info[4];
  __cpuid(cpu_info, 1);
  /* SSE4.2 is bit 20 of ECX, includes CRC32 */
  g_cpu_sse42 = (cpu_info[2] & (1 << 20)) != 0;
  g_cpu_crc32 = g_cpu_sse42;

  /* AVX2 is bit 5 of EBX when calling CPUID(7) */
  __cpuid(cpu_info, 7);
  g_cpu_avx2 = (cpu_info[1] & (1 << 5)) != 0;
#else
  unsigned int eax, ebx, ecx, edx;

  /* Check SSE4.2 */
  if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
    g_cpu_sse42 = (ecx & bit_SSE4_2) != 0;
    g_cpu_crc32 = g_cpu_sse42;
  }

  /* Check AVX2 */
  if (__get_cpuid(7, &eax, &ebx, &ecx, &edx)) {
    g_cpu_avx2 = (ebx & (1 << 5)) != 0;
  }
#endif
#endif
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
  check_cpu_features();
  return g_cpu_neon;
}
