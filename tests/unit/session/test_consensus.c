/**
 * @file session/test_consensus.c
 * @brief Tests for session consensus abstraction
 * @ingroup session
 *
 * NOTE: This test file is currently disabled because the session-level consensus
 * API it tests does not exist. The network-level consensus API exists under
 * lib/network/consensus/ but the session-level abstraction in lib/session/consensus.h
 * is not implemented. When the session consensus feature is implemented, this test
 * can be re-enabled.
 */

#include <criterion/criterion.h>

TestSuite(session_consensus);

Test(session_consensus, disabled) {
  cr_skip_test("Session consensus API not implemented - test suite disabled");
}
