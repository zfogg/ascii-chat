/**
 * @file ascii_simd_integration_test.c
 * @brief SIMD ASCII rendering integration tests
 *
 * NOTE: Tests disabled - SIMD headers (ascii_simd.h, common.h) are not publicly exported.
 * These are implementation details under lib/video/ascii/.
 */

#include <criterion/criterion.h>

TestSuite(ascii_simd_integration);

Test(ascii_simd_integration, placeholder) {
  cr_skip("SIMD tests require internal SIMD headers to be exported");
}
