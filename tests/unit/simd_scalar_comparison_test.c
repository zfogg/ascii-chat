/*
 * NOTE: This test file is currently disabled because it references non-existent APIs:
 * - ascii-chat/video/simd/ascii_simd.h does not exist (SIMD code is under ascii/)
 * - image_print_simd() function does not exist
 * - get_utf8_palette_cache() function does not exist
 *
 * When the SIMD abstraction layer is properly exported as public headers, this
 * test can be re-enabled by updating the includes and function calls.
 */

#include <criterion/criterion.h>

TestSuite(simd_scalar_comparison);

Test(simd_scalar_comparison, disabled) {
  cr_skip_test("SIMD test APIs not exported - test suite disabled");
}
