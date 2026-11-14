#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include <ascii-chat/video/ascii/output_buffer.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>

// Use the enhanced macro to create complete test suite with default log levels
/* ============================================================================
 * Basic Buffer Operations Tests
 * ============================================================================ */

TEST_CASE("output_buffer/ob_reserve_basic", "[output_buffer]") {
  outbuf_t ob = {0};

  ob_reserve(&ob, 100);
  ASCIICHAT_REQUIRE_NOT_NULL(ob.buf);
  ASCIICHAT_REQUIRE(ob.cap >= 100);
  ASCIICHAT_REQUIRE_EQ(ob.len, 0);

  SAFE_FREE(ob.buf);
}

TEST_CASE("output_buffer/ob_reserve_zero", "[output_buffer]") {
  outbuf_t ob = {0};

  ob_reserve(&ob, 0);
  ASCIICHAT_REQUIRE_NOT_NULL(ob.buf);
  ASCIICHAT_REQUIRE(ob.cap >= 4096); // Default capacity
  ASCIICHAT_REQUIRE_EQ(ob.len, 0);

  SAFE_FREE(ob.buf);
}

TEST_CASE("output_buffer/ob_reserve_expansion", "[output_buffer]") {
  outbuf_t ob = {0};

  // Reserve small amount first
  ob_reserve(&ob, 100);
  size_t initial_cap = ob.cap;

  // Reserve much larger amount
  ob_reserve(&ob, 10000);
  ASCIICHAT_REQUIRE_GT(ob.cap, initial_cap);
  ASCIICHAT_REQUIRE(ob.cap >= 10000);

  SAFE_FREE(ob.buf);
}

TEST_CASE("output_buffer/ob_putc_basic", "[output_buffer]") {
  outbuf_t ob = {0};

  ob_putc(&ob, 'A');
  ASCIICHAT_REQUIRE_EQ(ob.len, 1);
  ASCIICHAT_REQUIRE_EQ(ob.buf[0], 'A');

  ob_putc(&ob, 'B');
  ASCIICHAT_REQUIRE_EQ(ob.len, 2);
  ASCIICHAT_REQUIRE_EQ(ob.buf[1], 'B');

  SAFE_FREE(ob.buf);
}

TEST_CASE("output_buffer/ob_putc_multiple", "[output_buffer]") {
  outbuf_t ob = {0};

  for (int i = 0; i < 100; i++) {
    ob_putc(&ob, 'A' + (i % 26));
  }

  ASCIICHAT_REQUIRE_EQ(ob.len, 100);
  ASCIICHAT_REQUIRE(ob.cap >= 100);

  SAFE_FREE(ob.buf);
}

TEST_CASE("output_buffer/ob_write_basic", "[output_buffer]") {
  outbuf_t ob = {0};
  const char *data = "Hello World";

  ob_write(&ob, data, strlen(data));
  ASCIICHAT_REQUIRE_EQ(ob.len, strlen(data));
  ASCIICHAT_REQUIRE_EQ(memcmp(ob.buf, data, strlen(data)), 0);

  SAFE_FREE(ob.buf);
}

TEST_CASE("output_buffer/ob_write_empty", "[output_buffer]") {
  outbuf_t ob = {0};

  ob_write(&ob, "", 0);
  ASCIICHAT_REQUIRE_EQ(ob.len, 0);

  SAFE_FREE(ob.buf);
}

TEST_CASE("output_buffer/ob_write_large", "[output_buffer]") {
  outbuf_t ob = {0};
  char data[1000];

  for (int i = 0; i < 1000; i++) {
    data[i] = 'A' + (i % 26);
  }

  ob_write(&ob, data, 1000);
  ASCIICHAT_REQUIRE_EQ(ob.len, 1000);
  ASCIICHAT_REQUIRE_EQ(memcmp(ob.buf, data, 1000), 0);

  SAFE_FREE(ob.buf);
}

TEST_CASE("output_buffer/ob_term_basic", "[output_buffer]") {
  outbuf_t ob = {0};

  ob_putc(&ob, 'H');
  ob_putc(&ob, 'i');
  ob_term(&ob);

  ASCIICHAT_REQUIRE_EQ(ob.len, 2);
  ASCIICHAT_REQUIRE_EQ(ob.buf[2], '\0');
  ASCIICHAT_REQUIRE_STR_EQ(ob.buf, "Hi");

  SAFE_FREE(ob.buf);
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



TEST_CASE("output_buffer/ob_u8_values", "[output_buffer]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(ob_u8_cases)); auto *tc = &generated_case;
  outbuf_t ob = {0};

  ob_u8(&ob, tc->value);
  ob_term(&ob);
  ASCIICHAT_REQUIRE_STR_EQ(ob.buf, tc->expected_output, "%s: Expected '%s', got '%s'", tc->description, tc->expected_output,
                   ob.buf);

  SAFE_FREE(ob.buf);
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



TEST_CASE("output_buffer/ob_u32_values", "[output_buffer]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(ob_u32_cases)); auto *tc = &generated_case;
  outbuf_t ob = {0};

  ob_u32(&ob, tc->value);
  ob_term(&ob);
  ASCIICHAT_REQUIRE_STR_EQ(ob.buf, tc->expected_output, "%s: Expected '%s', got '%s'", tc->description, tc->expected_output,
                   ob.buf);

  SAFE_FREE(ob.buf);
}

/* ============================================================================
 * ANSI Escape Sequence Tests
 * ============================================================================ */

TEST_CASE("output_buffer/emit_set_truecolor_fg_basic", "[output_buffer]") {
  outbuf_t ob = {0};

  emit_set_truecolor_fg(&ob, 255, 128, 64);
  ob_term(&ob);

  ASCIICHAT_REQUIRE_GT(ob.len, 0);
  ASCIICHAT_REQUIRE(strstr(ob.buf, "38;2;255;128;64") != NULL);

  SAFE_FREE(ob.buf);
}

TEST_CASE("output_buffer/emit_set_truecolor_bg_basic", "[output_buffer]") {
  outbuf_t ob = {0};

  emit_set_truecolor_bg(&ob, 0, 255, 128);
  ob_term(&ob);

  ASCIICHAT_REQUIRE_GT(ob.len, 0);
  ASCIICHAT_REQUIRE(strstr(ob.buf, "48;2;0;255;128") != NULL);

  SAFE_FREE(ob.buf);
}

TEST_CASE("output_buffer/emit_set_256_color_fg_basic", "[output_buffer]") {
  outbuf_t ob = {0};

  emit_set_256_color_fg(&ob, 42);
  ob_term(&ob);

  ASCIICHAT_REQUIRE_GT(ob.len, 0);
  ASCIICHAT_REQUIRE(strstr(ob.buf, "38;5;42") != NULL);

  SAFE_FREE(ob.buf);
}

TEST_CASE("output_buffer/emit_set_256_color_bg_basic", "[output_buffer]") {
  outbuf_t ob = {0};

  emit_set_256_color_bg(&ob, 200);
  ob_term(&ob);

  ASCIICHAT_REQUIRE_GT(ob.len, 0);
  ASCIICHAT_REQUIRE(strstr(ob.buf, "48;5;200") != NULL);

  SAFE_FREE(ob.buf);
}

TEST_CASE("output_buffer/emit_reset_basic", "[output_buffer]") {
  outbuf_t ob = {0};

  emit_reset(&ob);
  ob_term(&ob);

  ASCIICHAT_REQUIRE_GT(ob.len, 0);
  ASCIICHAT_REQUIRE(strstr(ob.buf, "0m") != NULL);

  SAFE_FREE(ob.buf);
}

TEST_CASE("output_buffer/emit_set_fg_basic", "[output_buffer]") {
  outbuf_t ob = {0};

  emit_set_fg(&ob, 255, 0, 0);
  ob_term(&ob);

  ASCIICHAT_REQUIRE_GT(ob.len, 0);

  SAFE_FREE(ob.buf);
}

TEST_CASE("output_buffer/emit_set_bg_basic", "[output_buffer]") {
  outbuf_t ob = {0};

  emit_set_bg(&ob, 0, 255, 0);
  ob_term(&ob);

  ASCIICHAT_REQUIRE_GT(ob.len, 0);

  SAFE_FREE(ob.buf);
}

/* ============================================================================
 * REP (Repetition) Tests
 * ============================================================================ */

TEST_CASE("output_buffer/rep_is_profitable_basic", "[output_buffer]") {
  // Test various run lengths
  ASCIICHAT_REQUIRE_EQ(rep_is_profitable(0), false);
  ASCIICHAT_REQUIRE_EQ(rep_is_profitable(1), false);
  ASCIICHAT_REQUIRE_EQ(rep_is_profitable(2), false);
  ASCIICHAT_REQUIRE_EQ(rep_is_profitable(3), false); // 3 total chars: manual=3, REP=5
  ASCIICHAT_REQUIRE_EQ(rep_is_profitable(4), false); // 4 total chars: manual=4, REP=5
  ASCIICHAT_REQUIRE_EQ(rep_is_profitable(5), false); // 5 total chars: manual=5, REP=5 (equal)
  ASCIICHAT_REQUIRE_EQ(rep_is_profitable(6), true);  // 6 total chars: manual=6, REP=5
  ASCIICHAT_REQUIRE_EQ(rep_is_profitable(10), true);
  ASCIICHAT_REQUIRE_EQ(rep_is_profitable(100), true);
}

TEST_CASE("output_buffer/emit_rep_basic", "[output_buffer]") {
  outbuf_t ob = {0};

  emit_rep(&ob, 5);
  ob_term(&ob);

  ASCIICHAT_REQUIRE_GT(ob.len, 0);
  ASCIICHAT_REQUIRE(strstr(ob.buf, "5") != NULL);

  SAFE_FREE(ob.buf);
}

TEST_CASE("output_buffer/emit_rep_large", "[output_buffer]") {
  outbuf_t ob = {0};

  emit_rep(&ob, 1000);
  ob_term(&ob);

  ASCIICHAT_REQUIRE_GT(ob.len, 0);
  ASCIICHAT_REQUIRE(strstr(ob.buf, "1000") != NULL);

  SAFE_FREE(ob.buf);
}

/* ============================================================================
 * Digits Calculation Tests
 * ============================================================================ */

TEST_CASE("output_buffer/digits_u32_basic", "[output_buffer]") {
  ASCIICHAT_REQUIRE_EQ(digits_u32(0), 1);
  ASCIICHAT_REQUIRE_EQ(digits_u32(9), 1);
  ASCIICHAT_REQUIRE_EQ(digits_u32(10), 2);
  ASCIICHAT_REQUIRE_EQ(digits_u32(99), 2);
  ASCIICHAT_REQUIRE_EQ(digits_u32(100), 3);
  ASCIICHAT_REQUIRE_EQ(digits_u32(999), 3);
  ASCIICHAT_REQUIRE_EQ(digits_u32(1000), 4);
  ASCIICHAT_REQUIRE_EQ(digits_u32(9999), 4);
  ASCIICHAT_REQUIRE_EQ(digits_u32(10000), 5);
  ASCIICHAT_REQUIRE_EQ(digits_u32(100000), 6);
  ASCIICHAT_REQUIRE_EQ(digits_u32(1000000), 7);
  ASCIICHAT_REQUIRE_EQ(digits_u32(10000000), 8);
  ASCIICHAT_REQUIRE_EQ(digits_u32(100000000), 9);
  ASCIICHAT_REQUIRE_EQ(digits_u32(1000000000), 10);
  ASCIICHAT_REQUIRE_EQ(digits_u32(4294967295U), 10);
}

/* ============================================================================
 * Complex Operations Tests
 * ============================================================================ */

TEST_CASE("output_buffer/complex_ansi_sequence", "[output_buffer]") {
  outbuf_t ob = {0};

  // Build a complex ANSI sequence
  emit_set_truecolor_fg(&ob, 255, 0, 0);
  ob_putc(&ob, 'H');
  emit_set_truecolor_bg(&ob, 0, 255, 0);
  ob_putc(&ob, 'i');
  emit_reset(&ob);
  ob_term(&ob);

  ASCIICHAT_REQUIRE_GT(ob.len, 0);
  ASCIICHAT_REQUIRE(strstr(ob.buf, "38;2;255;0;0") != NULL);
  ASCIICHAT_REQUIRE(strstr(ob.buf, "48;2;0;255;0") != NULL);
  ASCIICHAT_REQUIRE(strstr(ob.buf, "0m") != NULL);

  SAFE_FREE(ob.buf);
}

TEST_CASE("output_buffer/mixed_operations", "[output_buffer]") {
  outbuf_t ob = {0};

  // Mix different operations
  ob_write(&ob, "Count: ", 7);
  ob_u32(&ob, 42);
  ob_putc(&ob, '\n');
  emit_set_256_color_fg(&ob, 200);
  ob_write(&ob, "Color text", 10);
  emit_reset(&ob);
  ob_term(&ob);

  ASCIICHAT_REQUIRE_GT(ob.len, 0);
  ASCIICHAT_REQUIRE(strstr(ob.buf, "Count: 42") != NULL);
  ASCIICHAT_REQUIRE(strstr(ob.buf, "Color text") != NULL);

  SAFE_FREE(ob.buf);
}

TEST_CASE("output_buffer/large_buffer_operations", "[output_buffer]") {
  outbuf_t ob = {0};

  // Test with large amounts of data
  for (int i = 0; i < 1000; i++) {
    ob_u32(&ob, i);
    ob_putc(&ob, ' ');
  }
  ob_term(&ob);

  ASCIICHAT_REQUIRE_GT(ob.len, 1000);
  ASCIICHAT_REQUIRE(ob.cap >= ob.len);

  SAFE_FREE(ob.buf);
}

/* ============================================================================
 * Edge Cases and Error Handling Tests
 * ============================================================================ */

TEST_CASE("output_buffer/null_buffer_operations", "[output_buffer]") {
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

TEST_CASE("output_buffer/zero_length_operations", "[output_buffer]") {
  outbuf_t ob = {0};

  ob_write(&ob, "test", 0);
  ASCIICHAT_REQUIRE_EQ(ob.len, 0);

  SAFE_FREE(ob.buf);
}

TEST_CASE("output_buffer/extreme_values", "[output_buffer]") {
  outbuf_t ob = {0};

  // Test with extreme values
  ob_u8(&ob, 0);
  ob_u8(&ob, 255);
  ob_u32(&ob, 0);
  ob_u32(&ob, 4294967295U);
  ob_term(&ob);

  ASCIICHAT_REQUIRE_GT(ob.len, 0);

  SAFE_FREE(ob.buf);
}
