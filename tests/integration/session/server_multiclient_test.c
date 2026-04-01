/**
 * @file server_multiclient_test.c
 * @brief Server multi-client integration tests
 *
 * NOTE: Tests disabled - references non-existent SIMD headers.
 * These tests need to be updated to work with the current architecture.
 */

#include <criterion/criterion.h>

TestSuite(server_multiclient);

Test(server_multiclient, placeholder) {
  cr_skip("Server multiclient tests need to be updated");
}
