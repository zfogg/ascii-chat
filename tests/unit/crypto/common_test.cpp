#include <iterator>
#include <catch2/catch_test_macros.hpp>
#include <ascii-chat/common.h>
#include <catch2/catch_all.hpp>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include <ascii-chat/tests/logging.h>

// Use the enhanced macro to create complete test suite with basic quiet logging
// =============================================================================
// SAFE_MALLOC Tests
// =============================================================================

TEST_CASE("common/safe_malloc_basic", "[common]") {
  void *ptr;
  ptr = SAFE_MALLOC(1024, void *);
  REQUIRE((ptr) != nullptr);

  // Write to memory to ensure it's accessible
  memset(ptr, 0xAA, 1024);
  uint8_t *byte_ptr = (uint8_t *)ptr;
  REQUIRE((byte_ptr[0]) == (0xAA));
  REQUIRE((byte_ptr[1023]) == (0xAA));

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
  REQUIRE((ptr) != nullptr);

  // Verify memory is zeroed
  for (size_t i = 0; i < count; i++) {
    REQUIRE((ptr[i]) == (0));
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
  REQUIRE((ptr) != nullptr);

  // Write pattern to memory
  memset(ptr, 0xBB, 512);

  // Realloc to larger size
  ptr = SAFE_REALLOC(ptr, 1024, void *);
  REQUIRE((ptr) != nullptr);

  // Verify original data is preserved
  uint8_t *byte_ptr = (uint8_t *)ptr;
  for (int i = 0; i < 512; i++) {
    REQUIRE((byte_ptr[i]) == (0xBB));
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

TEST_CASE("common/error_code_definitions", "[common]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(std::begin(error_code_cases), std::end(error_code_cases)));
  auto *tc = &generated_case;
  // Error codes should be defined
  REQUIRE(tc->error_code != -999999);

  // Get error string
  const char *error_str = asciichat_error_string(static_cast<asciichat_error_t>(tc->error_code));
  REQUIRE((error_str) != nullptr);
  REQUIRE(strlen(error_str) > 0);
}

TEST_CASE("common/error_codes_are_distinct", "[common]") {
  // Test that all error codes are distinct from each other
  int codes[] = {ASCIICHAT_OK, ERROR_NETWORK, ERROR_MEMORY, ERROR_INVALID_PARAM, ERROR_BUFFER_FULL};
  size_t count = sizeof(codes) / sizeof(codes[0]);

  for (size_t i = 0; i < count; i++) {
    for (size_t j = i + 1; j < count; j++) {
      REQUIRE((codes[i]) != (codes[j]));
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

TEST_CASE("common/min_max_macros", "[common]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(std::begin(min_max_cases), std::end(min_max_cases)));
  auto *tc = &generated_case;
  int min_result = MIN(tc->a, tc->b);
  int max_result = MAX(tc->a, tc->b);

  REQUIRE((min_result) == (tc->expected_min));
  REQUIRE((max_result) == (tc->expected_max));
}

TEST_CASE("common/array_size_macro", "[common]") {
  int test_array[42];
  REQUIRE((ARRAY_SIZE(test_array)) == (42));

  char string_array[] = "hello";
  REQUIRE((ARRAY_SIZE(string_array)) == (6));
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
    REQUIRE((ptrs[i]) != nullptr);

    // Write unique pattern to each block
    memset(ptrs[i], 0x10 + i, 1024 * (i + 1));
  }

  // Verify patterns are intact
  for (int i = 0; i < 10; i++) {
    uint8_t *byte_ptr = (uint8_t *)ptrs[i];
    REQUIRE((byte_ptr[0]) == (0x10 + i));
    REQUIRE((byte_ptr[1023 * (i + 1)]) == (0x10 + i));
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

  REQUIRE((byte_ptr[0]) == (0xFF));
  REQUIRE((byte_ptr[large_size - 1]) == (0xEE));

  SAFE_FREE(ptr);
}

TEST_CASE("common/alignment_checks", "[common]") {
  void *ptr1, *ptr2, *ptr3;

  // Test that allocated memory is properly aligned
  ptr1 = SAFE_MALLOC(1, void *);
  ptr2 = SAFE_MALLOC(3, void *);
  ptr3 = SAFE_MALLOC(7, void *);

  REQUIRE((ptr1) != nullptr);
  REQUIRE((ptr2) != nullptr);
  REQUIRE((ptr3) != nullptr);

  // Check alignment (should be at least pointer-aligned)
  uintptr_t addr1 = (uintptr_t)ptr1;
  uintptr_t addr2 = (uintptr_t)ptr2;
  uintptr_t addr3 = (uintptr_t)ptr3;

  REQUIRE((addr1 % sizeof(void *)) == (0));
  REQUIRE((addr2 % sizeof(void *)) == (0));
  REQUIRE((addr3 % sizeof(void *)) == (0));

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

  REQUIRE(true);
}
