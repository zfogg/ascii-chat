#include <ascii-chat/video/ascii/simd/dispatch.h>
#include <stdio.h>
int main(void) {
  uint32_t features = platform_cpu_features();
  printf("selected=%s cpu_features=0x%x", simd_backend()->name, features);
#if defined(__aarch64__)
  if (features & CPU_FEATURE_SVE) {
    unsigned long bytes;
    __asm__ volatile(".arch_extension sve\n\tcntb %0" : "=r"(bytes));
    printf(" sve_vector_bytes=%lu", bytes);
  }
#endif
  puts("");
  return 0;
}
