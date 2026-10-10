#include <ascii-chat/platform/cpu.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#endif

#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

#if defined(__linux__) && (defined(__aarch64__) || defined(__arm__))
#include <sys/auxv.h>
#include <asm/hwcap.h>
#elif defined(__APPLE__) && defined(__aarch64__)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

uint32_t platform_cpu_features(void) {
  uint32_t features = 0;
#if defined(__x86_64__) || defined(__i386__)
  unsigned int eax, ebx, ecx, edx;
  if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx))
    return 0;
  if (edx & (1u << 26))
    features |= CPU_FEATURE_SSE2;
  if (ecx & (1u << 9))
    features |= CPU_FEATURE_SSSE3;
  if (ecx & (1u << 20))
    features |= CPU_FEATURE_CRC32;
  // XGETBV is legal only with XSAVE and OSXSAVE. AVX also needs OS-managed YMM state.
  const unsigned int avx_state = (1u << 26) | (1u << 27) | (1u << 28);
  if ((ecx & avx_state) == avx_state) {
    unsigned int xcr0_low, xcr0_high;
    __asm__ volatile("xgetbv" : "=a"(xcr0_low), "=d"(xcr0_high) : "c"(0));
    if ((xcr0_low & 6) == 6 && __get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx) && (ebx & (1u << 5)))
      features |= CPU_FEATURE_AVX2;
  }
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

const char *platform_simd_override(void) {
#ifdef _WIN32
  static _Thread_local char value[32];
  DWORD length = GetEnvironmentVariableA("ASCII_CHAT_SIMD", value, sizeof(value));
  return length >= sizeof(value) ? "invalid" : (length ? value : NULL);
#else
  return getenv("ASCII_CHAT_SIMD");
#endif
}
