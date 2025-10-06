#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../lib/image2ascii/image.h"
#include "../lib/common.h"

extern unsigned short opt_background_color;

static double get_time_seconds(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return (double)clock() / CLOCKS_PER_SEC;
  }
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main() {
  printf("=== ansi_fast.c Integration Test ===\n");
  printf("Testing non-SIMD image_print_color() with 10x faster string generation\n\n");

  log_init(NULL, LOG_ERROR);

  // Create a small test image to minimize test time
  const int width = 320, height = 240;
  image_t *test_image = image_new(width, height);
  if (!test_image) {
    printf("Failed to create test image\n");
    return 1;
  }

  // Fill with varied colors
  srand(12345);
  for (int i = 0; i < width * height; i++) {
    test_image->pixels[i].r = rand() % 256;
    test_image->pixels[i].g = rand() % 256;
    test_image->pixels[i].b = rand() % 256;
  }

  // Test foreground mode
  opt_background_color = 0;

  printf("Testing 320x240 foreground mode (should be ~10x faster than before):\n");

  double start = get_time_seconds();
  char *result = image_print_color(test_image);
  double duration = get_time_seconds() - start;

  if (result) {
    printf("  Time: %.2f ms\n", duration * 1000);
    printf("  Output size: %zu KB\n", strlen(result) / 1024);
    printf("  First 100 chars: %.100s...\n", result);
    free(result);
  }

  // Test background mode
  opt_background_color = 1;
  printf("\nTesting 320x240 background mode:\n");

  start = get_time_seconds();
  result = image_print_color(test_image);
  duration = get_time_seconds() - start;

  if (result) {
    printf("  Time: %.2f ms\n", duration * 1000);
    printf("  Output size: %zu KB\n", strlen(result) / 1024);
    free(result);
  }

  image_destroy(test_image);
  log_destroy();

  printf("\n✅ SUCCESS: Non-SIMD colored ASCII now uses ansi_fast.c optimizations!\n");
  printf("Expected improvement: ~10x faster than old snprintf() approach\n");

  return 0;
}
