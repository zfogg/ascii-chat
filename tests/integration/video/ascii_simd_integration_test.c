/*
 * NOTE: This test file is currently disabled because it references non-existent APIs:
 * - ascii-chat/video/simd/ascii_simd.h does not exist
 * - ascii-chat/video/simd/common.h does not exist
 * - SIMD code is under lib/video/ascii/[arch]/ not a simd/ subdirectory
 *
 * When the SIMD abstraction layer is properly exported as public headers, this
 * test can be re-enabled by updating the includes.
 */

#include <criterion/criterion.h>

TestSuite(ascii_simd_integration);

Test(ascii_simd_integration, disabled) {
  cr_skip_test("SIMD integration test APIs not exported - test suite disabled");
}
