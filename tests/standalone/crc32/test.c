// Standalone correctness and throughput checks against independent CRC32-C algorithms.
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/platform/cpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(HAVE_CRC32_HW) && defined(__x86_64__)
#include <immintrin.h>
__attribute__((target("sse4.2"), noinline)) static uint32_t bytewise_hw(const void *data, size_t len) {
  const uint8_t *bytes = data;
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++)
    crc = _mm_crc32_u8(crc, bytes[i]);
  return ~crc;
}
#define TEST_HW 1
#elif defined(HAVE_CRC32_HW) && defined(__aarch64__)
#include <arm_acle.h>
__attribute__((target("crc"), noinline)) static uint32_t bytewise_hw(const void *data, size_t len) {
  const uint8_t *bytes = data;
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++)
    crc = __crc32cb(crc, bytes[i]);
  return ~crc;
}
#define TEST_HW 1
#endif

__attribute__((noinline)) static uint32_t bitwise_reference(const void *data, size_t len) {
  const uint8_t *bytes = data;
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= bytes[i];
    for (unsigned bit = 0; bit < 8; bit++)
      crc = (crc >> 1) ^ ((crc & 1) ? 0x82F63B78u : 0);
  }
  return ~crc;
}

static void check(const void *data, size_t len, uint32_t expected) {
  if (asciichat_crc32(data, len) != expected || asciichat_crc32_sw(data, len) != expected) {
    fprintf(stderr, "CRC mismatch: len=%zu expected=%08x\n", len, expected);
    exit(1);
  }
}

static volatile uint32_t sink;

static void bench(const char *name, uint32_t (*fn)(const void *, size_t), const void *data, size_t len) {
  // Keep every call observable even when the reference body is visible to Clang.
  uint32_t (*volatile call)(const void *, size_t) = fn;
  size_t calls = 0;
  clock_t start = clock(), elapsed;
  do {
    for (size_t i = 0; i < 128; i++)
      sink = call(data, len);
    calls += 128;
    elapsed = clock() - start;
  } while (elapsed < CLOCKS_PER_SEC / 5);
  double seconds = (double)elapsed / CLOCKS_PER_SEC;
  printf("%-18s %6zu bytes: %9.1f MiB/s (%7.1f ns/call)\n", name, len, calls * (double)len / seconds / 1048576.0,
         seconds * 1e9 / calls);
}

int main(int argc, char **argv) {
  bool expected_hw = false;
#if defined(TEST_HW) && !defined(TEST_MASKED_CPU)
  expected_hw = (platform_cpu_features() & CPU_FEATURE_CRC32) != 0;
#endif
  if (crc32_hw_is_available() != expected_hw)
    return 2;
  check(NULL, 0, 0);
  check("123456789", 9, 0xe3069283u);
  check("Hello, World!", 13, 0x4d551068u);
  uint8_t zeros[32] = {0}, ones[32];
  memset(ones, 0xff, sizeof(ones));
  check(zeros, sizeof(zeros), 0x8a9136aau);
  check(ones, sizeof(ones), 0x62a8ab43u);

  uint32_t rng = 17;
  // Exact-size stack objects let ASan catch word reads extending beyond each tail.
  for (size_t offset = 0; offset < 16; offset++) {
    for (size_t len = 1; len <= 1024; len++) {
      uint8_t data[len + offset];
      for (size_t i = 0; i < sizeof(data); i++) {
        rng = rng * 1664525u + 1013904223u;
        data[i] = (uint8_t)(rng >> 24);
      }
      check(data + offset, len, bitwise_reference(data + offset, len));
    }
  }
  // Exercise every table index and the large-packet path.
  uint8_t large[65536 + 16];
  for (size_t i = 0; i < sizeof(large); i++)
    large[i] = (uint8_t)i;
  for (size_t i = 0; i < 256; i++)
    check(large + i, 1, bitwise_reference(large + i, 1));
  for (size_t i = 0; i < 16; i++)
    check(large + i, 65536, bitwise_reference(large + i, 65536));
  if (crc32_hw_is_available() != expected_hw)
    return 3;
#if defined(TEST_HW) && defined(TEST_MASKED_CPU)
  extern unsigned crc32_test_probes;
  if (crc32_test_probes != 1)
    return 4;
#endif
  printf("PASS: vectors, 16 alignments x 1024 lengths, large buffers; hardware=%d\n", expected_hw);

  if (argc == 2 && strcmp(argv[1], "--bench") == 0) {
    const size_t sizes[] = {0, 8, 64, 1400, 65536};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
      bench("bitwise reference", bitwise_reference, large + 1, sizes[i]);
      bench("table software", asciichat_crc32_sw, large + 1, sizes[i]);
#ifdef TEST_HW
      if (expected_hw)
        bench("bytewise hardware", bytewise_hw, large + 1, sizes[i]);
#endif
      bench("cached dispatch", asciichat_crc32_hw, large + 1, sizes[i]);
    }
  }
  return 0;
}
