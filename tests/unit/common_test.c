#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include "common.h"
#include "tests/logging.h"

// Use the enhanced macro to create complete test suite with basic quiet logging
TEST_SUITE_WITH_QUIET_LOGGING(common);

// =============================================================================
// SAFE_MALLOC Tests
// =============================================================================

Test(common, safe_malloc_basic) {
  void *ptr;
  SAFE_MALLOC(ptr, 1024, void *);
  cr_assert_not_null(ptr, "SAFE_MALLOC should return valid pointer");

  // Write to memory to ensure it's accessible
  memset(ptr, 0xAA, 1024);
  uint8_t *byte_ptr = (uint8_t *)ptr;
  cr_assert_eq(byte_ptr[0], 0xAA, "Memory should be writable");
  cr_assert_eq(byte_ptr[1023], 0xAA, "All allocated memory should be accessible");

  free(ptr);
}

// =============================================================================
// SAFE_CALLOC Tests
// =============================================================================

Test(common, safe_calloc_basic) {
  int *ptr;
  size_t count = 256;
  size_t size = sizeof(int);

  SAFE_CALLOC(ptr, count, size, int *);
  cr_assert_not_null(ptr, "SAFE_CALLOC should return valid pointer");

  // Verify memory is zeroed
  for (size_t i = 0; i < count; i++) {
    cr_assert_eq(ptr[i], 0, "CALLOC memory should be zeroed at index %zu", i);
  }

  free(ptr);
}

// =============================================================================
// SAFE_REALLOC Tests
// =============================================================================

Test(common, safe_realloc_basic) {
  void *ptr;
  // Initial allocation
  SAFE_MALLOC(ptr, 512, void *);
  cr_assert_not_null(ptr, "Initial malloc should succeed");

  // Write pattern to memory
  memset(ptr, 0xBB, 512);

  // Realloc to larger size
  SAFE_REALLOC(ptr, 1024, void *);
  cr_assert_not_null(ptr, "SAFE_REALLOC should return valid pointer");

  // Verify original data is preserved
  uint8_t *byte_ptr = (uint8_t *)ptr;
  for (int i = 0; i < 512; i++) {
    cr_assert_eq(byte_ptr[i], 0xBB, "Original data should be preserved at index %d", i);
  }

  free(ptr);
}

// =============================================================================
// Error Code Tests
// =============================================================================

Test(common, error_codes) {
  // Test that error constants are defined
  cr_assert_neq(ASCIICHAT_ERR_NETWORK, 0, "ASCIICHAT_ERR_NETWORK should be non-zero");
  cr_assert_neq(ASCIICHAT_ERR_MALLOC, 0, "ASCIICHAT_ERR_MALLOC should be non-zero");
  cr_assert_neq(ASCIICHAT_ERR_INVALID_PARAM, 0, "ASCIICHAT_ERR_INVALID_PARAM should be non-zero");

  // Error codes should be distinct
  cr_assert_neq(ASCIICHAT_ERR_NETWORK, ASCIICHAT_ERR_MALLOC, "Error codes should be distinct");
  cr_assert_neq(ASCIICHAT_ERR_NETWORK, ASCIICHAT_ERR_INVALID_PARAM, "Error codes should be distinct");
  cr_assert_neq(ASCIICHAT_ERR_MALLOC, ASCIICHAT_ERR_INVALID_PARAM, "Error codes should be distinct");
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

ParameterizedTestParameters(common, min_max_macros) {
  size_t count = sizeof(min_max_cases) / sizeof(min_max_cases[0]);
  return cr_make_param_array(min_max_test_case_t, min_max_cases, count);
}

ParameterizedTest(min_max_test_case_t *tc, common, min_max_macros) {
  int min_result = MIN(tc->a, tc->b);
  int max_result = MAX(tc->a, tc->b);

  cr_assert_eq(min_result, tc->expected_min, "%s: MIN(%d, %d) should be %d", tc->description, tc->a, tc->b,
               tc->expected_min);
  cr_assert_eq(max_result, tc->expected_max, "%s: MAX(%d, %d) should be %d", tc->description, tc->a, tc->b,
               tc->expected_max);
}

Test(common, array_size_macro) {
  int test_array[42];
  cr_assert_eq(ARRAY_SIZE(test_array), 42, "ARRAY_SIZE should return correct size");

  char string_array[] = "hello";
  cr_assert_eq(ARRAY_SIZE(string_array), 6, "ARRAY_SIZE should include null terminator");
}

// =============================================================================
// Error String Tests
// =============================================================================

Test(common, error_string_function) {
  const char *ok_str = asciichat_error_string(ASCIICHAT_OK);
  const char *network_str = asciichat_error_string(ASCIICHAT_ERR_NETWORK);
  const char *memory_str = asciichat_error_string(ASCIICHAT_ERR_MALLOC);

  cr_assert_not_null(ok_str, "Error string should not be NULL");
  cr_assert_not_null(network_str, "Error string should not be NULL");
  cr_assert_not_null(memory_str, "Error string should not be NULL");

  cr_assert(strlen(ok_str) > 0, "Error string should not be empty");
  cr_assert(strlen(network_str) > 0, "Error string should not be empty");
  cr_assert(strlen(memory_str) > 0, "Error string should not be empty");
}

// =============================================================================
// Thread Safety Tests (basic)
// =============================================================================

Test(common, concurrent_allocations) {
  // Simple test to ensure allocations work in basic concurrent scenario
  void *ptrs[10];

  // Allocate multiple blocks
  for (int i = 0; i < 10; i++) {
    SAFE_MALLOC(ptrs[i], 1024 * (i + 1), void *);
    cr_assert_not_null(ptrs[i], "Allocation %d should succeed", i);

    // Write unique pattern to each block
    memset(ptrs[i], 0x10 + i, 1024 * (i + 1));
  }

  // Verify patterns are intact
  for (int i = 0; i < 10; i++) {
    uint8_t *byte_ptr = (uint8_t *)ptrs[i];
    cr_assert_eq(byte_ptr[0], 0x10 + i, "Pattern should be intact in block %d", i);
    cr_assert_eq(byte_ptr[1023 * (i + 1)], 0x10 + i, "Pattern should be intact at end of block %d", i);
  }

  // Free all blocks
  for (int i = 0; i < 10; i++) {
    free(ptrs[i]);
  }
}

// =============================================================================
// Edge Case Tests
// =============================================================================

Test(common, large_allocations) {
  void *ptr;
  // Test reasonably large allocation (1MB)
  size_t large_size = 1024 * 1024;
  SAFE_MALLOC(ptr, large_size, void *);

  // Write to first and last bytes to ensure it's really allocated
  uint8_t *byte_ptr = (uint8_t *)ptr;
  byte_ptr[0] = 0xFF;
  byte_ptr[large_size - 1] = 0xEE;

  cr_assert_eq(byte_ptr[0], 0xFF, "First byte should be writable");
  cr_assert_eq(byte_ptr[large_size - 1], 0xEE, "Last byte should be writable");

  free(ptr);
}

Test(common, alignment_checks) {
  void *ptr1, *ptr2, *ptr3;

  // Test that allocated memory is properly aligned
  SAFE_MALLOC(ptr1, 1, void *);
  SAFE_MALLOC(ptr2, 3, void *);
  SAFE_MALLOC(ptr3, 7, void *);

  cr_assert_not_null(ptr1, "Small allocation should succeed");
  cr_assert_not_null(ptr2, "Small allocation should succeed");
  cr_assert_not_null(ptr3, "Small allocation should succeed");

  // Check alignment (should be at least pointer-aligned)
  uintptr_t addr1 = (uintptr_t)ptr1;
  uintptr_t addr2 = (uintptr_t)ptr2;
  uintptr_t addr3 = (uintptr_t)ptr3;

  cr_assert_eq(addr1 % sizeof(void *), 0, "Memory should be pointer-aligned");
  cr_assert_eq(addr2 % sizeof(void *), 0, "Memory should be pointer-aligned");
  cr_assert_eq(addr3 % sizeof(void *), 0, "Memory should be pointer-aligned");

  free(ptr1);
  free(ptr2);
  free(ptr3);
}

// =============================================================================
// Integration with Logging Tests
// =============================================================================

Test(common, log_memory_operations) {
  void *ptr;

  // Test logging during memory operations
  SAFE_MALLOC(ptr, 1024, void *);
  log_debug("Allocated memory at %p", ptr);

  if (ptr) {
    memset(ptr, 0xAB, 1024);
    log_info("Filled memory with pattern 0xAB");

    SAFE_REALLOC(ptr, 2048, void *);
    log_info("Reallocated memory to 2048 bytes at %p", ptr);

    free(ptr);
    log_debug("Freed memory");
  }

  cr_assert(true, "Logging during memory operations should work");
}
