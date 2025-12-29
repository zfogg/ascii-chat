#include <criterion/theories.h>

#include "tests/common.h"
#include "buffer_pool.h"

// Use the enhanced macro to create complete test suite with basic quiet logging
TEST_SUITE_WITH_QUIET_LOGGING(buffer_pool);

// =============================================================================
// Buffer Pool Creation and Destruction Tests
// =============================================================================

Test(buffer_pool, creation_and_destruction) {
  buffer_pool_t *pool = buffer_pool_create(BUFFER_POOL_MAX_BYTES, BUFFER_POOL_SHRINK_DELAY_MS);
  cr_assert_not_null(pool, "Buffer pool creation should succeed");

  // New unified pool design - no separate pools
  // Just verify basic stats are initialized
  size_t current_bytes = 0, used_bytes = 0, free_bytes = 0;
  buffer_pool_get_stats(pool, &current_bytes, &used_bytes, &free_bytes);
  cr_assert_eq(used_bytes, 0, "Initial used bytes should be zero");

  buffer_pool_destroy(pool);
}

Test(buffer_pool, multiple_creation_destruction) {
  // Test multiple create/destroy cycles
  for (int i = 0; i < 5; i++) {
    buffer_pool_t *pool = buffer_pool_create(BUFFER_POOL_MAX_BYTES, BUFFER_POOL_SHRINK_DELAY_MS);
    cr_assert_not_null(pool, "Pool creation %d should succeed", i);
    buffer_pool_destroy(pool);
  }
}

Test(buffer_pool, null_destruction_safety) {
  // Destroying NULL pool should be safe
  buffer_pool_destroy(NULL);
}

// =============================================================================
// Global Buffer Pool Tests
// =============================================================================

Test(buffer_pool, global_pool_initialization) {
  // Initialize global pool
  buffer_pool_init_global();

  buffer_pool_t *global_pool = buffer_pool_get_global();
  cr_assert_not_null(global_pool, "Global pool should be available");

  // Cleanup
  buffer_pool_cleanup_global();

  // After cleanup, global pool behavior is implementation-defined
  // Just ensure cleanup doesn't crash - no assertions about return value
  global_pool = buffer_pool_get_global();
  (void)global_pool; // Avoid unused variable warning
}

Test(buffer_pool, multiple_global_init_cleanup) {
  // Multiple init/cleanup cycles should be safe
  for (int i = 0; i < 3; i++) {
    buffer_pool_init_global();
    buffer_pool_t *pool = buffer_pool_get_global();
    cr_assert_not_null(pool, "Global pool should be available in cycle %d", i);
    buffer_pool_cleanup_global();
  }
}

// =============================================================================
// Buffer Allocation and Deallocation Tests
// =============================================================================

// Theory: Buffer allocation roundtrip property - allocate -> write -> read -> free
TheoryDataPoints(buffer_pool, allocation_roundtrip_property) = {
    DataPoints(size_t,
               512,    // Small
               1024,   // Small
               32768,  // Medium
               65536,  // Medium
               131072, // Large
               262144, // Large
               655360, // XLarge
               1048576 // XLarge (1MB)
               ),
};

Theory((size_t size), buffer_pool, allocation_roundtrip_property) {
  cr_assume(size > 0 && size <= 1048576);

  buffer_pool_t *pool = buffer_pool_create(BUFFER_POOL_MAX_BYTES, BUFFER_POOL_SHRINK_DELAY_MS);
  cr_assume(pool != NULL);

  void *buf = buffer_pool_alloc(pool, size);
  cr_assert_not_null(buf, "Allocation should succeed for size %zu", size);

  unsigned char test_pattern = (unsigned char)((size ^ 0xAB) & 0xFF);
  memset(buf, test_pattern, size);

  unsigned char *test_buf = (unsigned char *)buf;
  cr_assert_eq(test_buf[0], test_pattern, "Buffer start should be readable for size %zu", size);
  if (size > 1) {
    cr_assert_eq(test_buf[size / 2], test_pattern, "Buffer middle should be readable for size %zu", size);
    cr_assert_eq(test_buf[size - 1], test_pattern, "Buffer end should be readable for size %zu", size);
  }

  buffer_pool_free(pool, buf, size);
  buffer_pool_destroy(pool);
}

Test(buffer_pool, zero_size_allocation) {
  buffer_pool_t *pool = buffer_pool_create(BUFFER_POOL_MAX_BYTES, BUFFER_POOL_SHRINK_DELAY_MS);
  cr_assert_not_null(pool, "Pool creation should succeed");

  void *buf = buffer_pool_alloc(pool, 0);
  // Zero size allocation may return NULL or a valid pointer - both are acceptable
  // Just ensure it doesn't crash

  if (buf != NULL) {
    buffer_pool_free(pool, buf, 0);
  }

  buffer_pool_destroy(pool);
}

Test(buffer_pool, null_pool_allocation) {
  void *buf = buffer_pool_alloc(NULL, 1024);
  // Should handle NULL pool gracefully (uses global pool)

  if (buf != NULL) {
    // If it returned something, we should be able to free it
    buffer_pool_free(NULL, buf, 1024);
  }
}

// =============================================================================
// Buffer Pool Efficiency Tests
// =============================================================================

// Theory: Pool reuse property - freed buffers can be reallocated
TheoryDataPoints(buffer_pool, pool_reuse_property) = {
    DataPoints(size_t, 512, 1024, 2048, 4096, 8192),
};

Theory((size_t size), buffer_pool, pool_reuse_property) {
  cr_assume(size > 0 && size <= 8192);

  buffer_pool_t *pool = buffer_pool_create(BUFFER_POOL_MAX_BYTES, BUFFER_POOL_SHRINK_DELAY_MS);
  cr_assume(pool != NULL);

  void *buffers[5];
  for (int cycle = 0; cycle < 2; cycle++) {
    for (int i = 0; i < 5; i++) {
      buffers[i] = buffer_pool_alloc(pool, size);
      cr_assert_not_null(buffers[i], "Allocation %d should succeed in cycle %d for size %zu", i, cycle, size);
      memset(buffers[i], (unsigned char)(i + cycle * 10), size);
    }

    for (int i = 0; i < 5; i++) {
      buffer_pool_free(pool, buffers[i], size);
    }
  }

  buffer_pool_destroy(pool);
}

Test(buffer_pool, mixed_size_allocation) {
  buffer_pool_t *pool = buffer_pool_create(BUFFER_POOL_MAX_BYTES, BUFFER_POOL_SHRINK_DELAY_MS);
  cr_assert_not_null(pool, "Pool creation should succeed");

  void *small = buffer_pool_alloc(pool, 512);
  void *medium = buffer_pool_alloc(pool, 32768);
  void *large = buffer_pool_alloc(pool, 131072);
  void *xlarge = buffer_pool_alloc(pool, 655360);

  cr_assert_not_null(small, "Small buffer allocation should succeed");
  cr_assert_not_null(medium, "Medium buffer allocation should succeed");
  cr_assert_not_null(large, "Large buffer allocation should succeed");
  cr_assert_not_null(xlarge, "XLarge buffer allocation should succeed");

  // Write test patterns
  memset(small, 0xAA, 512);
  memset(medium, 0xBB, 32768);
  memset(large, 0xCC, 131072);
  memset(xlarge, 0xDD, 655360);

  // Verify patterns
  cr_assert_eq(((char *)small)[0], (char)0xAA, "Small buffer pattern should be correct");
  cr_assert_eq(((char *)medium)[0], (char)0xBB, "Medium buffer pattern should be correct");
  cr_assert_eq(((char *)large)[0], (char)0xCC, "Large buffer pattern should be correct");
  cr_assert_eq(((char *)xlarge)[0], (char)0xDD, "XLarge buffer pattern should be correct");

  buffer_pool_free(pool, small, 512);
  buffer_pool_free(pool, medium, 32768);
  buffer_pool_free(pool, large, 131072);
  buffer_pool_free(pool, xlarge, 655360);

  buffer_pool_destroy(pool);
}

// =============================================================================
// Buffer Pool Statistics Tests
// =============================================================================

Test(buffer_pool, statistics_tracking) {
  buffer_pool_t *pool = buffer_pool_create(BUFFER_POOL_MAX_BYTES, BUFFER_POOL_SHRINK_DELAY_MS);
  cr_assert_not_null(pool, "Pool creation should succeed");

  size_t current_bytes = 0, used_bytes = 0, free_bytes = 0;
  buffer_pool_get_stats(pool, &current_bytes, &used_bytes, &free_bytes);
  size_t initial_used = used_bytes;

  // Allocate some buffers
  void *buf1 = buffer_pool_alloc(pool, 1024);
  void *buf2 = buffer_pool_alloc(pool, 32768);

  buffer_pool_get_stats(pool, &current_bytes, &used_bytes, &free_bytes);

  // Used bytes should have increased
  cr_assert_gt(used_bytes, initial_used, "Used bytes should increase after allocation");

  buffer_pool_free(pool, buf1, 1024);
  buffer_pool_free(pool, buf2, 32768);
  buffer_pool_destroy(pool);
}

Test(buffer_pool, log_statistics) {
  buffer_pool_t *pool = buffer_pool_create(BUFFER_POOL_MAX_BYTES, BUFFER_POOL_SHRINK_DELAY_MS);
  cr_assert_not_null(pool, "Pool creation should succeed");

  // Allocate some buffers to generate stats
  void *buf1 = buffer_pool_alloc(pool, 1024);
  void *buf2 = buffer_pool_alloc(pool, 32768);

  // This should not crash
  buffer_pool_log_stats(pool, "test_pool");

  buffer_pool_free(pool, buf1, 1024);
  buffer_pool_free(pool, buf2, 32768);
  buffer_pool_destroy(pool);
}

// =============================================================================
// Global Buffer Pool Convenience Functions Tests
// =============================================================================

Test(buffer_pool, global_convenience_functions) {
  // Initialize global pool
  buffer_pool_init_global();

  // Test convenience macros
  void *buf = POOL_ALLOC(1024);
  cr_assert_not_null(buf, "Global buffer allocation should succeed");

  // Test pattern
  memset(buf, 0x99, 1024);
  char *test_buf = (char *)buf;
  cr_assert_eq((unsigned char)test_buf[0], 0x99, "Global buffer should be writable");

  POOL_FREE(buf, 1024);

  // Cleanup
  buffer_pool_cleanup_global();
}

Test(buffer_pool, global_multiple_allocations) {
  buffer_pool_init_global();

  void *buffers[5];

  // Allocate multiple buffers using global pool
  for (int i = 0; i < 5; i++) {
    buffers[i] = POOL_ALLOC(2048);
    cr_assert_not_null(buffers[i], "Global allocation %d should succeed", i);
    memset(buffers[i], i + 0x10, 2048);
  }

  // Verify patterns
  for (int i = 0; i < 5; i++) {
    char *buf = (char *)buffers[i];
    cr_assert_eq((unsigned char)buf[0], (unsigned char)(i + 0x10), "Global buffer %d pattern should be correct", i);
  }

  // Free all
  for (int i = 0; i < 5; i++) {
    POOL_FREE(buffers[i], 2048);
  }

  buffer_pool_cleanup_global();
}

// =============================================================================
// Pool Exhaustion and Fallback Tests
// =============================================================================

Test(buffer_pool, many_allocations) {
  buffer_pool_t *pool = buffer_pool_create(BUFFER_POOL_MAX_BYTES, BUFFER_POOL_SHRINK_DELAY_MS);
  cr_assert_not_null(pool, "Pool creation should succeed");

  // Allocate many buffers
  void *buffers[100];
  int allocated_count = 0;

  for (int i = 0; i < 100; i++) {
    buffers[i] = buffer_pool_alloc(pool, 1024);
    if (buffers[i] != NULL) {
      allocated_count++;
      memset(buffers[i], i & 0xFF, 1024);
    } else {
      break;
    }
  }

  cr_assert_gt(allocated_count, 0, "Should allocate at least some buffers");

  // Free all allocated buffers
  for (int i = 0; i < allocated_count; i++) {
    if (buffers[i] != NULL) {
      buffer_pool_free(pool, buffers[i], 1024);
    }
  }

  buffer_pool_destroy(pool);
}

Test(buffer_pool, very_large_allocation) {
  buffer_pool_t *pool = buffer_pool_create(BUFFER_POOL_MAX_BYTES, BUFFER_POOL_SHRINK_DELAY_MS);
  cr_assert_not_null(pool, "Pool creation should succeed");

  // Allocate buffer larger than pool max single size (should use malloc fallback)
  size_t huge_size = BUFFER_POOL_MAX_SINGLE_SIZE + 1024;
  void *huge_buf = buffer_pool_alloc(pool, huge_size);

  if (huge_buf != NULL) {
    // Test we can write to it
    memset(huge_buf, 0x77, 4096); // Just test first 4KB
    char *test_buf = (char *)huge_buf;
    cr_assert_eq((unsigned char)test_buf[0], 0x77, "Huge buffer should be writable");

    buffer_pool_free(pool, huge_buf, huge_size);
  }
  // Note: huge allocation may fail on constrained systems - that's OK

  buffer_pool_destroy(pool);
}

// =============================================================================
// Thread Safety Stress Tests
// =============================================================================

// Theory: Stress test property - rapid alloc/free cycles should work
TheoryDataPoints(buffer_pool, stress_allocation_property) = {
    DataPoints(size_t, 256, 1024, 4096, 16384),
};

Theory((size_t size), buffer_pool, stress_allocation_property) {
  cr_assume(size > 0 && size <= 16384);

  buffer_pool_t *pool = buffer_pool_create(BUFFER_POOL_MAX_BYTES, BUFFER_POOL_SHRINK_DELAY_MS);
  cr_assume(pool != NULL);

  void *buffers[10];

  for (int cycle = 0; cycle < 10; cycle++) {
    for (int i = 0; i < 10; i++) {
      buffers[i] = buffer_pool_alloc(pool, size);
      if (buffers[i] != NULL) {
        memset(buffers[i], (unsigned char)cycle, size);
      }
    }

    for (int i = 0; i < 10; i++) {
      if (buffers[i] != NULL) {
        buffer_pool_free(pool, buffers[i], size);
        buffers[i] = NULL;
      }
    }
  }

  buffer_pool_destroy(pool);
}

// =============================================================================
// Edge Cases and Error Handling
// =============================================================================

Test(buffer_pool, free_null_buffer) {
  buffer_pool_t *pool = buffer_pool_create(BUFFER_POOL_MAX_BYTES, BUFFER_POOL_SHRINK_DELAY_MS);
  cr_assert_not_null(pool, "Pool creation should succeed");

  // Freeing NULL should be safe
  buffer_pool_free(pool, NULL, 1024);

  buffer_pool_destroy(pool);
}

Test(buffer_pool, shrink_pool) {
  buffer_pool_t *pool = buffer_pool_create(BUFFER_POOL_MAX_BYTES, BUFFER_POOL_SHRINK_DELAY_MS);
  cr_assert_not_null(pool, "Pool creation should succeed");

  // Allocate and free some buffers
  void *buf1 = buffer_pool_alloc(pool, 1024);
  void *buf2 = buffer_pool_alloc(pool, 2048);
  buffer_pool_free(pool, buf1, 1024);
  buffer_pool_free(pool, buf2, 2048);

  // Shrink should not crash
  buffer_pool_shrink(pool);

  buffer_pool_destroy(pool);
}
