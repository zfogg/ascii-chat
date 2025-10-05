#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "image2ascii/output_buffer.h"
#include "common.h"
#include "tests/logging.h"

// Use the enhanced macro to create complete test suite with default log levels
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVEL(output_buffer);

/* ============================================================================
 * Basic Buffer Operations Tests
 * ============================================================================ */

Test(output_buffer, ob_reserve_basic) {
  outbuf_t ob = {0};

  ob_reserve(&ob, 100);
  cr_assert_not_null(ob.buf);
  cr_assert(ob.cap >= 100);
  cr_assert_eq(ob.len, 0);

  free(ob.buf);
}

Test(output_buffer, ob_reserve_zero) {
  outbuf_t ob = {0};

  ob_reserve(&ob, 0);
  cr_assert_not_null(ob.buf);
  cr_assert(ob.cap >= 4096); // Default capacity
  cr_assert_eq(ob.len, 0);

  free(ob.buf);
}

Test(output_buffer, ob_reserve_expansion) {
  outbuf_t ob = {0};

  // Reserve small amount first
  ob_reserve(&ob, 100);
  size_t initial_cap = ob.cap;

  // Reserve much larger amount
  ob_reserve(&ob, 10000);
  cr_assert_gt(ob.cap, initial_cap);
  cr_assert(ob.cap >= 10000);

  free(ob.buf);
}

Test(output_buffer, ob_putc_basic) {
  outbuf_t ob = {0};

  ob_putc(&ob, 'A');
  cr_assert_eq(ob.len, 1);
  cr_assert_eq(ob.buf[0], 'A');

  ob_putc(&ob, 'B');
  cr_assert_eq(ob.len, 2);
  cr_assert_eq(ob.buf[1], 'B');

  free(ob.buf);
}

Test(output_buffer, ob_putc_multiple) {
  outbuf_t ob = {0};

  for (int i = 0; i < 100; i++) {
    ob_putc(&ob, 'A' + (i % 26));
  }

  cr_assert_eq(ob.len, 100);
  cr_assert(ob.cap >= 100);

  free(ob.buf);
}

Test(output_buffer, ob_write_basic) {
  outbuf_t ob = {0};
  const char *data = "Hello World";

  ob_write(&ob, data, strlen(data));
  cr_assert_eq(ob.len, strlen(data));
  cr_assert_eq(memcmp(ob.buf, data, strlen(data)), 0);

  free(ob.buf);
}

Test(output_buffer, ob_write_empty) {
  outbuf_t ob = {0};

  ob_write(&ob, "", 0);
  cr_assert_eq(ob.len, 0);

  free(ob.buf);
}

Test(output_buffer, ob_write_large) {
  outbuf_t ob = {0};
  char data[1000];

  for (int i = 0; i < 1000; i++) {
    data[i] = 'A' + (i % 26);
  }

  ob_write(&ob, data, 1000);
  cr_assert_eq(ob.len, 1000);
  cr_assert_eq(memcmp(ob.buf, data, 1000), 0);

  free(ob.buf);
}

Test(output_buffer, ob_term_basic) {
  outbuf_t ob = {0};

  ob_putc(&ob, 'H');
  ob_putc(&ob, 'i');
  ob_term(&ob);

  cr_assert_eq(ob.len, 3);
  cr_assert_eq(ob.buf[2], '\0');
  cr_assert_str_eq(ob.buf, "Hi");

  free(ob.buf);
}

/* ============================================================================
 * Number Formatting Tests
 * ============================================================================ */

// Parameterized test for ob_u8 with different values
typedef struct {
  uint8_t value;
  char expected_output[8];
  char description[64];
} ob_u8_test_case_t;

static ob_u8_test_case_t ob_u8_cases[] = {
    {0, "0", "Zero value"},
    {5, "5", "Single digit value"},
    {42, "42", "Double digit value"},
    {255, "255", "Maximum uint8_t value"},
    {9, "9", "Boundary: single to double digit"},
    {10, "10", "Boundary: first double digit"},
    {99, "99", "Boundary: double to triple digit"},
    {100, "100", "Boundary: first triple digit"},
};

ParameterizedTestParameters(output_buffer, ob_u8_values) {
  return cr_make_param_array(ob_u8_test_case_t, ob_u8_cases, sizeof(ob_u8_cases) / sizeof(ob_u8_cases[0]));
}

ParameterizedTest(ob_u8_test_case_t *tc, output_buffer, ob_u8_values) {
  outbuf_t ob = {0};

  ob_u8(&ob, tc->value);
  ob_term(&ob);
  cr_assert_str_eq(ob.buf, tc->expected_output, "%s: Expected '%s', got '%s'", tc->description, tc->expected_output,
                   ob.buf);

  free(ob.buf);
}

// Parameterized test for ob_u32 with different values
typedef struct {
  uint32_t value;
  char expected_output[16];
  char description[64];
} ob_u32_test_case_t;

static ob_u32_test_case_t ob_u32_cases[] = {
    {0, "0", "Zero value"},
    {42, "42", "Small value"},
    {4294967295U, "4294967295", "Maximum uint32_t value"},
    {9, "9", "Boundary: single to double digit"},
    {10, "10", "Boundary: first double digit"},
    {99, "99", "Boundary: double to triple digit"},
    {100, "100", "Boundary: first triple digit"},
    {999, "999", "Boundary: triple to quadruple digit"},
    {1000, "1000", "Boundary: first quadruple digit"},
    {9999, "9999", "Boundary: quadruple to quintuple digit"},
    {10000, "10000", "Boundary: first quintuple digit"},
};

ParameterizedTestParameters(output_buffer, ob_u32_values) {
  return cr_make_param_array(ob_u32_test_case_t, ob_u32_cases, sizeof(ob_u32_cases) / sizeof(ob_u32_cases[0]));
}

ParameterizedTest(ob_u32_test_case_t *tc, output_buffer, ob_u32_values) {
  outbuf_t ob = {0};

  ob_u32(&ob, tc->value);
  ob_term(&ob);
  cr_assert_str_eq(ob.buf, tc->expected_output, "%s: Expected '%s', got '%s'", tc->description, tc->expected_output,
                   ob.buf);

  free(ob.buf);
}

/* ============================================================================
 * ANSI Escape Sequence Tests
 * ============================================================================ */

Test(output_buffer, emit_set_truecolor_fg_basic) {
  outbuf_t ob = {0};

  emit_set_truecolor_fg(&ob, 255, 128, 64);
  ob_term(&ob);

  cr_assert_gt(ob.len, 0);
  cr_assert(strstr(ob.buf, "38;2;255;128;64") != NULL);

  free(ob.buf);
}

Test(output_buffer, emit_set_truecolor_bg_basic) {
  outbuf_t ob = {0};

  emit_set_truecolor_bg(&ob, 0, 255, 128);
  ob_term(&ob);

  cr_assert_gt(ob.len, 0);
  cr_assert(strstr(ob.buf, "48;2;0;255;128") != NULL);

  free(ob.buf);
}

Test(output_buffer, emit_set_256_color_fg_basic) {
  outbuf_t ob = {0};

  emit_set_256_color_fg(&ob, 42);
  ob_term(&ob);

  cr_assert_gt(ob.len, 0);
  cr_assert(strstr(ob.buf, "38;5;42") != NULL);

  free(ob.buf);
}

Test(output_buffer, emit_set_256_color_bg_basic) {
  outbuf_t ob = {0};

  emit_set_256_color_bg(&ob, 200);
  ob_term(&ob);

  cr_assert_gt(ob.len, 0);
  cr_assert(strstr(ob.buf, "48;5;200") != NULL);

  free(ob.buf);
}

Test(output_buffer, emit_reset_basic) {
  outbuf_t ob = {0};

  emit_reset(&ob);
  ob_term(&ob);

  cr_assert_gt(ob.len, 0);
  cr_assert(strstr(ob.buf, "0m") != NULL);

  free(ob.buf);
}

Test(output_buffer, emit_set_fg_basic) {
  outbuf_t ob = {0};

  emit_set_fg(&ob, 255, 0, 0);
  ob_term(&ob);

  cr_assert_gt(ob.len, 0);

  free(ob.buf);
}

Test(output_buffer, emit_set_bg_basic) {
  outbuf_t ob = {0};

  emit_set_bg(&ob, 0, 255, 0);
  ob_term(&ob);

  cr_assert_gt(ob.len, 0);

  free(ob.buf);
}

/* ============================================================================
 * REP (Repetition) Tests
 * ============================================================================ */

Test(output_buffer, rep_is_profitable_basic) {
  // Test various run lengths
  cr_assert_eq(rep_is_profitable(0), false);
  cr_assert_eq(rep_is_profitable(1), false);
  cr_assert_eq(rep_is_profitable(2), false);
  cr_assert_eq(rep_is_profitable(3), false); // 3 total chars: manual=3, REP=5
  cr_assert_eq(rep_is_profitable(4), false); // 4 total chars: manual=4, REP=5
  cr_assert_eq(rep_is_profitable(5), false); // 5 total chars: manual=5, REP=5 (equal)
  cr_assert_eq(rep_is_profitable(6), true);  // 6 total chars: manual=6, REP=5
  cr_assert_eq(rep_is_profitable(10), true);
  cr_assert_eq(rep_is_profitable(100), true);
}

Test(output_buffer, emit_rep_basic) {
  outbuf_t ob = {0};

  emit_rep(&ob, 5);
  ob_term(&ob);

  cr_assert_gt(ob.len, 0);
  cr_assert(strstr(ob.buf, "5") != NULL);

  free(ob.buf);
}

Test(output_buffer, emit_rep_large) {
  outbuf_t ob = {0};

  emit_rep(&ob, 1000);
  ob_term(&ob);

  cr_assert_gt(ob.len, 0);
  cr_assert(strstr(ob.buf, "1000") != NULL);

  free(ob.buf);
}

/* ============================================================================
 * Digits Calculation Tests
 * ============================================================================ */

Test(output_buffer, digits_u32_basic) {
  cr_assert_eq(digits_u32(0), 1);
  cr_assert_eq(digits_u32(9), 1);
  cr_assert_eq(digits_u32(10), 2);
  cr_assert_eq(digits_u32(99), 2);
  cr_assert_eq(digits_u32(100), 3);
  cr_assert_eq(digits_u32(999), 3);
  cr_assert_eq(digits_u32(1000), 4);
  cr_assert_eq(digits_u32(9999), 4);
  cr_assert_eq(digits_u32(10000), 5);
  cr_assert_eq(digits_u32(100000), 6);
  cr_assert_eq(digits_u32(1000000), 7);
  cr_assert_eq(digits_u32(10000000), 8);
  cr_assert_eq(digits_u32(100000000), 9);
  cr_assert_eq(digits_u32(1000000000), 10);
  cr_assert_eq(digits_u32(4294967295U), 10);
}

/* ============================================================================
 * Complex Operations Tests
 * ============================================================================ */

Test(output_buffer, complex_ansi_sequence) {
  outbuf_t ob = {0};

  // Build a complex ANSI sequence
  emit_set_truecolor_fg(&ob, 255, 0, 0);
  ob_putc(&ob, 'H');
  emit_set_truecolor_bg(&ob, 0, 255, 0);
  ob_putc(&ob, 'i');
  emit_reset(&ob);
  ob_term(&ob);

  cr_assert_gt(ob.len, 0);
  cr_assert(strstr(ob.buf, "38;2;255;0;0") != NULL);
  cr_assert(strstr(ob.buf, "48;2;0;255;0") != NULL);
  cr_assert(strstr(ob.buf, "0m") != NULL);

  free(ob.buf);
}

Test(output_buffer, mixed_operations) {
  outbuf_t ob = {0};

  // Mix different operations
  ob_write(&ob, "Count: ", 7);
  ob_u32(&ob, 42);
  ob_putc(&ob, '\n');
  emit_set_256_color_fg(&ob, 200);
  ob_write(&ob, "Color text", 10);
  emit_reset(&ob);
  ob_term(&ob);

  cr_assert_gt(ob.len, 0);
  cr_assert(strstr(ob.buf, "Count: 42") != NULL);
  cr_assert(strstr(ob.buf, "Color text") != NULL);

  free(ob.buf);
}

Test(output_buffer, large_buffer_operations) {
  outbuf_t ob = {0};

  // Test with large amounts of data
  for (int i = 0; i < 1000; i++) {
    ob_u32(&ob, i);
    ob_putc(&ob, ' ');
  }
  ob_term(&ob);

  cr_assert_gt(ob.len, 1000);
  cr_assert(ob.cap >= ob.len);

  free(ob.buf);
}

/* ============================================================================
 * Edge Cases and Error Handling Tests
 * ============================================================================ */

Test(output_buffer, null_buffer_operations) {
  // These should not crash
  ob_reserve(NULL, 100);
  ob_putc(NULL, 'A');
  ob_write(NULL, "test", 4);
  ob_term(NULL);
  ob_u8(NULL, 42);
  ob_u32(NULL, 42);
  emit_set_truecolor_fg(NULL, 255, 0, 0);
  emit_set_truecolor_bg(NULL, 0, 255, 0);
  emit_set_256_color_fg(NULL, 42);
  emit_set_256_color_bg(NULL, 42);
  emit_reset(NULL);
  emit_rep(NULL, 5);
  emit_set_fg(NULL, 255, 0, 0);
  emit_set_bg(NULL, 0, 255, 0);
}

Test(output_buffer, zero_length_operations) {
  outbuf_t ob = {0};

  ob_write(&ob, "test", 0);
  cr_assert_eq(ob.len, 0);

  free(ob.buf);
}

Test(output_buffer, extreme_values) {
  outbuf_t ob = {0};

  // Test with extreme values
  ob_u8(&ob, 0);
  ob_u8(&ob, 255);
  ob_u32(&ob, 0);
  ob_u32(&ob, 4294967295U);
  ob_term(&ob);

  cr_assert_gt(ob.len, 0);

  free(ob.buf);
}
