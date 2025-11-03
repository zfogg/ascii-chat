#ifndef LOGGING_H
#define LOGGING_H

/**
 * @file tests/logging.h
 * @ingroup testing
 * @brief Test logging control utilities
 *
 * This header provides utilities for controlling logging output during tests.
 * It enables tests to temporarily disable or redirect stdout/stderr for quiet
 * test execution, and provides convenient macros for test suite setup and teardown.
 *
 * CORE FEATURES:
 * ==============
 * - stdout/stderr redirection to /dev/null
 * - Log level control during tests
 * - Test suite setup/teardown macros
 * - Per-test logging control macros
 * - Debug logging mode for test development
 *
 * TEST LOGGING MACROS:
 * ===================
 * - TEST_LOGGING_SETUP_AND_TEARDOWN(): Simple quiet logging setup
 * - TEST_SUITE_WITH_QUIET_LOGGING(): Complete test suite with quiet logging
 * - TEST_SUITE_WITH_DEBUG_LOGGING(): Test suite with debug logging enabled
 * - TEST_LOGGING_TEMPORARILY_DISABLE(): Disable logging within a test
 *
 * USAGE:
 * =====
 * @code
 * // Simple test suite with quiet logging
 * TEST_SUITE_WITH_QUIET_LOGGING(my_suite);
 *
 * Test(my_suite, my_test) {
 *   // Test code runs quietly
 * }
 *
 * // Test suite with custom log levels
 * TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(
 *   my_suite, LOG_FATAL, LOG_DEBUG, true, true
 * );
 *
 * // Debug mode for test development
 * TEST_SUITE_WITH_DEBUG_LOGGING(debug_suite);
 * @endcode
 *
 * @note Logging redirection is automatically restored when tests complete.
 * @note All logging control functions are thread-safe for use in parallel tests.
 * @note This header should be included via tests/common.h for consistency.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stdbool.h>

/**
 * @brief Redirect stdout and/or stderr to /dev/null for quiet testing
 * @param disable_stdout If true, redirect stdout to /dev/null
 * @param disable_stderr If true, redirect stderr to /dev/null
 * @return 0 on success, -1 on failure
 *
 * Redirects stdout and/or stderr to /dev/null to suppress output during tests.
 * This is useful for tests that produce noisy output or test error handling
 * without cluttering the test output.
 *
 * @note The original file descriptors are saved and can be restored with
 *       test_logging_restore().
 * @note This function can be called multiple times safely.
 *
 * @ingroup testing
 */
int test_logging_disable(bool disable_stdout, bool disable_stderr);

/**
 * @brief Restore stdout and/or stderr to their original state
 * @return 0 on success, -1 on failure
 *
 * Restores stdout and/or stderr to their original file descriptors that were
 * saved when test_logging_disable() was called.
 *
 * @note This function should be called in test teardown to ensure proper
 *       cleanup, though the test macros handle this automatically.
 *
 * @ingroup testing
 */
int test_logging_restore(void);

/**
 * @brief Check if logging is currently disabled
 * @return true if logging is disabled, false otherwise
 *
 * Checks whether stdout and/or stderr have been redirected to /dev/null.
 * Useful for conditional logging control or debugging test setup.
 *
 * @ingroup testing
 */
bool test_logging_is_disabled(void);

// ============================================================================
// Convenience Macros for Test Suites
// ============================================================================

/**
 * @brief Macro to create setup and teardown functions for quiet testing
 *
 * This macro creates two functions:
 * - setup_quiet_test_logging() - disables both stdout and stderr
 * - restore_test_logging() - restores stdout and stderr
 *
 * Usage:
 * @code
 * TEST_LOGGING_SETUP_AND_TEARDOWN();
 * TestSuite(my_suite, .init = setup_quiet_test_logging, .fini = restore_test_logging);
 * @endcode
 */
#define TEST_LOGGING_SETUP_AND_TEARDOWN()                                                                              \
  void setup_quiet_test_logging(void) {                                                                                \
    test_logging_disable(true, true);                                                                                  \
  }                                                                                                                    \
  void restore_test_logging(void) {                                                                                    \
    test_logging_restore();                                                                                            \
  }

/**
 * @brief Macro to create setup and teardown functions for quiet testing with custom log level control
 *
 * This macro creates two functions that control the log level:
 * - setup_quiet_test_logging() - sets log level to setup_level and disables stdout/stderr
 * - restore_test_logging() - restores log level to restore_level and restores stdout/stderr
 *
 * Usage:
 * @code
 * TEST_LOGGING_SETUP_AND_TEARDOWN_WITH_LOG_LEVELS(LOG_FATAL, LOG_DEBUG);
 * TestSuite(my_suite, .init = setup_quiet_test_logging, .fini = restore_test_logging);
 * @endcode
 */
#define TEST_LOGGING_SETUP_AND_TEARDOWN_WITH_LOG_LEVELS(setup_level, restore_level, disable_stdout, disable_stderr)    \
  void setup_quiet_test_logging(void) {                                                                                \
    log_set_level(setup_level);                                                                                        \
    test_logging_disable(disable_stdout, disable_stderr);                                                              \
  }                                                                                                                    \
  void restore_test_logging(void) {                                                                                    \
    log_set_level(restore_level);                                                                                      \
    test_logging_restore();                                                                                            \
  }

/**
 * @brief Macro to create setup and teardown functions for quiet testing with log level control (default levels)
 *
 * This macro creates two functions that also control the log level:
 * - setup_quiet_test_logging() - sets log level to FATAL and disables stdout/stderr
 * - restore_test_logging() - restores log level to DEBUG and restores stdout/stderr
 *
 * Usage:
 * @code
 * TEST_LOGGING_SETUP_AND_TEARDOWN_WITH_LOG_LEVEL();
 * TestSuite(my_suite, .init = setup_quiet_test_logging, .fini = restore_test_logging);
 * @endcode
 */
#define TEST_LOGGING_SETUP_AND_TEARDOWN_WITH_LOG_LEVEL()                                                               \
  TEST_LOGGING_SETUP_AND_TEARDOWN_WITH_LOG_LEVELS(LOG_FATAL, LOG_DEBUG, true, true)

/**
 * @brief Macro to create a complete test suite with quiet logging and custom log levels
 *
 * This macro creates unique setup/teardown functions with custom log levels and declares the test suite
 * in one go. It supports additional TestSuite options and avoids redefinition errors.
 *
 * Usage:
 * @code
 * TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(my_suite, LOG_FATAL, LOG_DEBUG, true, true);
 * TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(my_suite, LOG_FATAL, LOG_DEBUG, false, false, .timeout = 10);
 * @endcode
 */
#define TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(suite_name, setup_level, restore_level, disable_stdout,           \
                                                     disable_stderr, ...)                                              \
  void setup_quiet_test_logging_##suite_name(void) {                                                                   \
    log_set_level(setup_level);                                                                                        \
    test_logging_disable(disable_stdout, disable_stderr);                                                              \
  }                                                                                                                    \
  void restore_test_logging_##suite_name(void) {                                                                       \
    log_set_level(restore_level);                                                                                      \
    test_logging_restore();                                                                                            \
  }                                                                                                                    \
  TestSuite(suite_name, .init = setup_quiet_test_logging_##suite_name, .fini = restore_test_logging_##suite_name,      \
            ##__VA_ARGS__);

/**
 * @brief Macro to create a complete test suite with quiet logging (default log levels)
 *
 * This macro creates unique setup/teardown functions and declares the test suite
 * in one go. It supports additional TestSuite options and avoids redefinition errors
 * when multiple test suites are in the same file.
 *
 * Usage:
 * @code
 * TEST_SUITE_WITH_QUIET_LOGGING(my_suite);
 * TEST_SUITE_WITH_QUIET_LOGGING(my_suite, .timeout = 10);
 * @endcode
 */
#define TEST_SUITE_WITH_QUIET_LOGGING(suite_name, ...)                                                                 \
  void setup_quiet_test_logging_##suite_name(void) {                                                                   \
    test_logging_disable(true, true);                                                                                  \
  }                                                                                                                    \
  void restore_test_logging_##suite_name(void) {                                                                       \
    test_logging_restore();                                                                                            \
  }                                                                                                                    \
  TestSuite(suite_name, .init = setup_quiet_test_logging_##suite_name, .fini = restore_test_logging_##suite_name,      \
            ##__VA_ARGS__);

/**
 * @brief Macro to create a complete test suite with quiet logging and log level control (default levels)
 *
 * This macro creates unique setup/teardown functions with log level control and declares the test suite
 * in one go. It supports additional TestSuite options and avoids redefinition errors.
 *
 * Usage:
 * @code
 * TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVEL(my_suite);
 * TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVEL(my_suite, .timeout = 10);
 * @endcode
 */
#define TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVEL(suite_name, ...)                                                   \
  void setup_quiet_test_logging_##suite_name(void) {                                                                   \
    log_set_level(LOG_FATAL);                                                                                          \
    test_logging_disable(true, true);                                                                                  \
  }                                                                                                                    \
  void restore_test_logging_##suite_name(void) {                                                                       \
    log_set_level(LOG_DEBUG);                                                                                          \
    test_logging_restore();                                                                                            \
  }                                                                                                                    \
  TestSuite(suite_name, .init = setup_quiet_test_logging_##suite_name, .fini = restore_test_logging_##suite_name,      \
            ##__VA_ARGS__);

/**
 * @brief Macro to temporarily disable logging for a specific test
 *
 * This macro can be used within a test to temporarily disable logging,
 * with automatic restoration when the test ends.
 *
 * Usage:
 * @code
 * Test(my_suite, my_test) {
 *   TEST_LOGGING_TEMPORARILY_DISABLE();
 *   // ... test code that should be quiet ...
 * }
 * @endcode
 */
#define TEST_LOGGING_TEMPORARILY_DISABLE()                                                                             \
  bool _logging_was_disabled = test_logging_is_disabled();                                                             \
  if (!_logging_was_disabled) {                                                                                        \
    test_logging_disable(true, true);                                                                                  \
  }                                                                                                                    \
  /* Note: Restoration happens automatically when test ends */

/**
 * @brief Macro to temporarily disable only stdout for a specific test
 *
 * This macro can be used within a test to temporarily disable only stdout,
 * keeping stderr available for error messages.
 *
 * Usage:
 * @code
 * Test(my_suite, my_test) {
 *   TEST_LOGGING_TEMPORARILY_DISABLE_STDOUT();
 *   // ... test code that should be quiet on stdout but can use stderr ...
 * }
 * @endcode
 */
#define TEST_LOGGING_TEMPORARILY_DISABLE_STDOUT()                                                                      \
  bool _logging_was_disabled = test_logging_is_disabled();                                                             \
  if (!_logging_was_disabled) {                                                                                        \
    test_logging_disable(true, false);                                                                                 \
  }

/**
 * @brief Macro to temporarily disable only stderr for a specific test
 *
 * This macro can be used within a test to temporarily disable only stderr,
 * keeping stdout available for normal output.
 *
 * Usage:
 * @code
 * Test(my_suite, my_test) {
 *   TEST_LOGGING_TEMPORARILY_DISABLE_STDERR();
 *   // ... test code that can use stdout but should be quiet on stderr ...
 * }
 * @endcode
 */
#define TEST_LOGGING_TEMPORARILY_DISABLE_STDERR()                                                                      \
  bool _logging_was_disabled = test_logging_is_disabled();                                                             \
  if (!_logging_was_disabled) {                                                                                        \
    test_logging_disable(false, true);                                                                                 \
  }

/**
 * @brief Macro to create a complete test suite with debug logging and stdout/stderr enabled
 *
 * This macro creates unique setup/teardown functions with debug logging enabled
 * and stdout/stderr available for debugging output.
 *
 * Usage:
 * @code
 * TEST_SUITE_WITH_DEBUG_LOGGING(my_suite);
 * TEST_SUITE_WITH_DEBUG_LOGGING(my_suite, .timeout = 10);
 * @endcode
 */
#define TEST_SUITE_WITH_DEBUG_LOGGING(suite_name, ...)                                                                 \
  TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(suite_name, LOG_DEBUG, LOG_DEBUG, false, false, ##__VA_ARGS__)

#endif // LOGGING_H
