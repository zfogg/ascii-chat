/**
 * @file simd_scalar_comparison_test.c
 * @brief SIMD vs scalar performance comparison tests
 *
 * NOTE: Tests disabled - SIMD headers (ascii_simd.h, common.h) are not publicly exported.
 * These are implementation details under lib/video/ascii/.
 */

#include <criterion/criterion.h>

TestSuite(simd_scalar_comparison);

Test(simd_scalar_comparison, placeholder) {
  cr_skip("SIMD tests require internal SIMD headers to be exported");
}
