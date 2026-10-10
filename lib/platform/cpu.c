#include <ascii-chat/platform/cpu.h>

#if defined(__linux__) && (defined(__aarch64__) || defined(__arm__))
#include <sys/auxv.h>
#include <asm/hwcap.h>
#elif defined(_WIN32) && defined(__aarch64__)
#include <windows.h>
#elif defined(__APPLE__) && defined(__aarch64__)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

uint32_t platform_cpu_features(void) {
  uint32_t features = 0;
#if defined(__x86_64__) || defined(__i386__)
  // Clang's detector includes OSXSAVE/XCR0 checks before advertising AVX2.
  __builtin_cpu_init();
  if (__builtin_cpu_supports("sse2"))
    features |= CPU_FEATURE_SSE2;
  if (__builtin_cpu_supports("ssse3"))
    features |= CPU_FEATURE_SSSE3;
  if (__builtin_cpu_supports("avx2"))
    features |= CPU_FEATURE_AVX2;
  if (__builtin_cpu_supports("sse4.2"))
    features |= CPU_FEATURE_CRC32;
#elif defined(__linux__) && defined(__aarch64__)
  unsigned long hwcap = getauxval(AT_HWCAP);
  if (hwcap & HWCAP_ASIMD)
    features |= CPU_FEATURE_NEON;
  if (hwcap & HWCAP_SVE)
    features |= CPU_FEATURE_SVE;
  if (hwcap & HWCAP_CRC32)
    features |= CPU_FEATURE_CRC32;
#elif defined(__linux__) && defined(__arm__)
  if (getauxval(AT_HWCAP) & HWCAP_NEON)
    features |= CPU_FEATURE_NEON;
#elif defined(__APPLE__) && defined(__aarch64__)
  features |= CPU_FEATURE_NEON;
  int crc32 = 0;
  size_t size = sizeof(crc32);
  if (sysctlbyname("hw.optional.armv8_crc32", &crc32, &size, NULL, 0) == 0 && crc32)
    features |= CPU_FEATURE_CRC32;
#elif defined(_WIN32) && defined(__aarch64__)
  features |= CPU_FEATURE_NEON;
  if (IsProcessorFeaturePresent(PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE))
    features |= CPU_FEATURE_CRC32;
#endif
  return features;
}
