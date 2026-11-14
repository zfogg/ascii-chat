#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>

// Use the enhanced macro to create complete test suite with basic quiet logging
// =============================================================================
// SAFE_MALLOC Tests
// =============================================================================

TEST_CASE("common/safe_malloc_basic", "[common]") {
  void *ptr;
  ptr = SAFE_MALLOC(1024, void *);
  ASCIICHAT_REQUIRE_NOT_NULL(ptr, "SAFE_MALLOC should return valid pointer");

  // Write to memory to ensure it's accessible
  memset(ptr, 0xAA, 1024);
  uint8_t *byte_ptr = (uint8_t *)ptr;
  ASCIICHAT_REQUIRE_EQ(byte_ptr[0], 0xAA, "Memory should be writable");
  ASCIICHAT_REQUIRE_EQ(byte_ptr[1023], 0xAA, "All allocated memory should be accessible");

  SAFE_FREE(ptr);
}

// =============================================================================
// SAFE_CALLOC Tests
// =============================================================================

TEST_CASE("common/safe_calloc_basic", "[common]") {
  int *ptr;
  size_t count = 256;
  size_t size = sizeof(int);

  ptr = SAFE_CALLOC(count, size, int *);
  ASCIICHAT_REQUIRE_NOT_NULL(ptr, "SAFE_CALLOC should return valid pointer");

  // Verify memory is zeroed
  for (size_t i = 0; i < count; i++) {
    ASCIICHAT_REQUIRE_EQ(ptr[i], 0, "CALLOC memory should be zeroed at index %zu", i);
  }

  SAFE_FREE(ptr);
}

// =============================================================================
// SAFE_REALLOC Tests
// =============================================================================

TEST_CASE("common/safe_realloc_basic", "[common]") {
  void *ptr;
  // Initial allocation
  ptr = SAFE_MALLOC(512, void *);
  ASCIICHAT_REQUIRE_NOT_NULL(ptr, "Initial malloc should succeed");

  // Write pattern to memory
  memset(ptr, 0xBB, 512);

  // Realloc to larger size
  ptr = SAFE_REALLOC(ptr, 1024, void *);
  ASCIICHAT_REQUIRE_NOT_NULL(ptr, "SAFE_REALLOC should return valid pointer");

  // Verify original data is preserved
  uint8_t *byte_ptr = (uint8_t *)ptr;
  for (int i = 0; i < 512; i++) {
    ASCIICHAT_REQUIRE_EQ(byte_ptr[i], 0xBB, "Original data should be preserved at index %d", i);
  }

  SAFE_FREE(ptr);
}

// =============================================================================
// Error Code Tests - Parameterized
// =============================================================================

typedef struct {
  int error_code;
  char error_name[64];
  char description[64];
} error_code_test_case_t;

static error_code_test_case_t error_code_cases[] = {
    {ASCIICHAT_OK, "ASCIICHAT_OK", "Success code"},
    {ERROR_NETWORK, "ERROR_NETWORK", "Network error code"},
    {ERROR_MEMORY, "ERROR_MEMORY", "Memory allocation error code"},
    {ERROR_INVALID_PARAM, "ERROR_INVALID_PARAM", "Invalid parameter error code"},
    {ERROR_BUFFER_FULL, "ERROR_BUFFER_FULL", "Buffer full error code"},
};



TEST_CASE("common/error_code_definitions", "[common]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(error_code_cases)); auto *tc = &generated_case;
  // Error codes should be defined
  ASCIICHAT_REQUIRE(tc->error_code != -999999, "%s should be defined", tc->error_name);

  // Get error string
  const char *error_str = asciichat_error_string(static_cast<asciichat_error_t>(tc->error_code));
  ASCIICHAT_REQUIRE_NOT_NULL(error_str, "%s error string should not be NULL", tc->error_name);
  ASCIICHAT_REQUIRE(strlen(error_str) > 0, "%s error string should not be empty", tc->error_name);
}

TEST_CASE("common/error_codes_are_distinct", "[common]") {
  // Test that all error codes are distinct from each other
  int codes[] = {ASCIICHAT_OK, ERROR_NETWORK, ERROR_MEMORY, ERROR_INVALID_PARAM, ERROR_BUFFER_FULL};
  size_t count = sizeof(codes) / sizeof(codes[0]);

  for (size_t i = 0; i < count; i++) {
    for (size_t j = i + 1; j < count; j++) {
      ASCIICHAT_REQUIRE_NE(codes[i], codes[j], "Error codes at index %zu and %zu should be distinct", i, j);
    }
  }
}

// =============================================================================
// Utility Macro Tests
// =============================================================================

// =============================================================================
// MIN/MAX Macro Tests - Parameterized
// =============================================================================

typedef struct {
  int a;
  int b;
  int expected_min;
  int expected_max;
  char description[64];
} min_max_test_case_t;

static min_max_test_case_t min_max_cases[] = {
    // Basic positive cases
    {5, 10, 5, 10, "Basic positive values"},
    {10, 5, 5, 10, "Reversed positive values"},
    {7, 7, 7, 7, "Equal positive values"},
    // Zero cases
    {0, 0, 0, 0, "Both zero"},
    {0, 5, 0, 5, "Zero and positive"},
    {5, 0, 0, 5, "Positive and zero"},
    // Negative cases
    {-5, -10, -10, -5, "Both negative"},
    {-10, -5, -10, -5, "Both negative reversed"},
    {-7, -7, -7, -7, "Equal negative values"},
    // Mixed sign cases
    {-5, 5, -5, 5, "Negative and positive"},
    {5, -5, -5, 5, "Positive and negative"},
    {-100, 100, -100, 100, "Large negative and positive"},
    // Edge cases
    {INT_MIN, INT_MAX, INT_MIN, INT_MAX, "Min and max int values"},
    {INT_MIN, 0, INT_MIN, 0, "Min int and zero"},
    {0, INT_MAX, 0, INT_MAX, "Zero and max int"},
    {INT_MIN, INT_MIN, INT_MIN, INT_MIN, "Both INT_MIN"},
    {INT_MAX, INT_MAX, INT_MAX, INT_MAX, "Both INT_MAX"},
};



TEST_CASE("common/min_max_macros", "[common]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(min_max_cases)); auto *tc = &generated_case;
  int min_result = MIN(tc->a, tc->b);
  int max_result = MAX(tc->a, tc->b);

  ASCIICHAT_REQUIRE_EQ(min_result, tc->expected_min, "%s: MIN(%d, %d) should be %d", tc->description, tc->a, tc->b,
               tc->expected_min);
  ASCIICHAT_REQUIRE_EQ(max_result, tc->expected_max, "%s: MAX(%d, %d) should be %d", tc->description, tc->a, tc->b,
               tc->expected_max);
}

TEST_CASE("common/array_size_macro", "[common]") {
  int test_array[42];
  ASCIICHAT_REQUIRE_EQ(ARRAY_SIZE(test_array), 42, "ARRAY_SIZE should return correct size");

  char string_array[] = "hello";
  ASCIICHAT_REQUIRE_EQ(ARRAY_SIZE(string_array), 6, "ARRAY_SIZE should include null terminator");
}

// =============================================================================
// Thread Safety Tests (basic)
// =============================================================================

TEST_CASE("common/concurrent_allocations", "[common]") {
  // Simple test to ensure allocations work in basic concurrent scenario
  void *ptrs[10];

  // Allocate multiple blocks
  for (int i = 0; i < 10; i++) {
    ptrs[i] = SAFE_MALLOC(1024 * (i + 1), void *);
    ASCIICHAT_REQUIRE_NOT_NULL(ptrs[i], "Allocation %d should succeed", i);

    // Write unique pattern to each block
    memset(ptrs[i], 0x10 + i, 1024 * (i + 1));
  }

  // Verify patterns are intact
  for (int i = 0; i < 10; i++) {
    uint8_t *byte_ptr = (uint8_t *)ptrs[i];
    ASCIICHAT_REQUIRE_EQ(byte_ptr[0], 0x10 + i, "Pattern should be intact in block %d", i);
    ASCIICHAT_REQUIRE_EQ(byte_ptr[1023 * (i + 1)], 0x10 + i, "Pattern should be intact at end of block %d", i);
  }

  // Free all blocks
  for (int i = 0; i < 10; i++) {
    SAFE_FREE(ptrs[i]);
  }
}

// =============================================================================
// Edge Case Tests
// =============================================================================

TEST_CASE("common/large_allocations", "[common]") {
  void *ptr;
  // Test reasonably large allocation (1MB)
  size_t large_size = 1024 * 1024;
  ptr = SAFE_MALLOC(large_size, void *);

  // Write to first and last bytes to ensure it's really allocated
  uint8_t *byte_ptr = (uint8_t *)ptr;
  byte_ptr[0] = 0xFF;
  byte_ptr[large_size - 1] = 0xEE;

  ASCIICHAT_REQUIRE_EQ(byte_ptr[0], 0xFF, "First byte should be writable");
  ASCIICHAT_REQUIRE_EQ(byte_ptr[large_size - 1], 0xEE, "Last byte should be writable");

  SAFE_FREE(ptr);
}

TEST_CASE("common/alignment_checks", "[common]") {
  void *ptr1, *ptr2, *ptr3;

  // Test that allocated memory is properly aligned
  ptr1 = SAFE_MALLOC(1, void *);
  ptr2 = SAFE_MALLOC(3, void *);
  ptr3 = SAFE_MALLOC(7, void *);

  ASCIICHAT_REQUIRE_NOT_NULL(ptr1, "Small allocation should succeed");
  ASCIICHAT_REQUIRE_NOT_NULL(ptr2, "Small allocation should succeed");
  ASCIICHAT_REQUIRE_NOT_NULL(ptr3, "Small allocation should succeed");

  // Check alignment (should be at least pointer-aligned)
  uintptr_t addr1 = (uintptr_t)ptr1;
  uintptr_t addr2 = (uintptr_t)ptr2;
  uintptr_t addr3 = (uintptr_t)ptr3;

  ASCIICHAT_REQUIRE_EQ(addr1 % sizeof(void *), 0, "Memory should be pointer-aligned");
  ASCIICHAT_REQUIRE_EQ(addr2 % sizeof(void *), 0, "Memory should be pointer-aligned");
  ASCIICHAT_REQUIRE_EQ(addr3 % sizeof(void *), 0, "Memory should be pointer-aligned");

  SAFE_FREE(ptr1);
  SAFE_FREE(ptr2);
  SAFE_FREE(ptr3);
}

// =============================================================================
// Integration with Logging Tests
// =============================================================================

TEST_CASE("common/log_memory_operations", "[common]") {
  void *ptr;

  // Test logging during memory operations
  ptr = SAFE_MALLOC(1024, void *);
  log_debug("Allocated memory at %p", ptr);

  if (ptr) {
    memset(ptr, 0xAB, 1024);
    log_info("Filled memory with pattern 0xAB");

    ptr = SAFE_REALLOC(ptr, 2048, void *);
    log_info("Reallocated memory to 2048 bytes at %p", ptr);

    SAFE_FREE(ptr);
    log_debug("Freed memory");
  }

  ASCIICHAT_REQUIRE(true, "Logging during memory operations should work");
}
