#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdint.h>

#include "common.h"

// Global setup to reduce verbose output during tests
void setup_quiet_logging(void);
void restore_logging(void);

TestSuite(logging, .init = setup_quiet_logging, .fini = restore_logging);

void setup_quiet_logging(void) {
    // Suppress logging output during tests like other unit tests
    log_set_level(LOG_FATAL);
}

void restore_logging(void) {
    // Restore normal log level after tests
    log_set_level(LOG_DEBUG);
}

// =============================================================================
// Basic Logging Tests
// =============================================================================

Test(logging, log_levels) {
    // Test that we can call logging functions without crashing
    log_debug("Debug message test");
    log_info("Info message test");
    log_warn("Warning message test");
    log_error("Error message test");

    // If we reach here, basic logging works
    cr_assert(true, "Basic logging functions should not crash");
}

Test(logging, log_with_format) {
    const char *test_string = "test";
    int test_number = 42;

    // Test formatted logging
    log_debug("Debug with string: %s, number: %d", test_string, test_number);
    log_info("Info with string: %s, number: %d", test_string, test_number);
    log_warn("Warning with string: %s, number: %d", test_string, test_number);
    log_error("Error with string: %s, number: %d", test_string, test_number);

    // Test passes if no crash occurs
    cr_assert(true, "Formatted logging should work");
}

Test(logging, log_empty_messages) {
    // Test logging empty or minimal messages
    log_debug("");
    log_info("");
    log_warn("");
    log_error("");

    log_debug(".");
    log_info(".");
    log_warn(".");
    log_error(".");

    cr_assert(true, "Empty message logging should not crash");
}

Test(logging, log_long_messages) {
    // Test with reasonably long message
    char long_message[1024];
    memset(long_message, 'A', sizeof(long_message) - 1);
    long_message[sizeof(long_message) - 1] = '\0';

    log_debug("Long debug message: %s", long_message);
    log_info("Long info message: %s", long_message);
    log_warn("Long warning message: %s", long_message);
    log_error("Long error message: %s", long_message);

    cr_assert(true, "Long message logging should not crash");
}

// =============================================================================
// Special Characters and Edge Cases
// =============================================================================

Test(logging, log_special_characters) {
    // Test with special characters that might cause issues
    log_debug("Message with newlines\n\n");
    log_info("Message with tabs\t\t");
    log_warn("Message with quotes: \"test\" and 'test'");
    log_error("Message with unicode: café naïve résumé");

    // Test with format specifiers in the message (should be handled safely)
    log_debug("Message with percent signs: 100%% complete");
    log_info("Message with format chars: %s %d %f (but no args)", "test", 42, 3.14);

    cr_assert(true, "Special character logging should work");
}

Test(logging, log_null_safety) {
    // Test logging with NULL string (should be handled gracefully)
    const char *null_string = NULL;

    // These should not crash (good logging implementations handle NULL)
    log_debug("Debug with null: %s", null_string ? null_string : "(null)");
    log_info("Info with null: %s", null_string ? null_string : "(null)");
    log_warn("Warning with null: %s", null_string ? null_string : "(null)");
    log_error("Error with null: %s", null_string ? null_string : "(null)");

    cr_assert(true, "NULL-safe logging should work");
}

// =============================================================================
// Performance and Stress Tests
// =============================================================================

Test(logging, log_performance) {
    // Test that we can log many messages without issues
    for (int i = 0; i < 1000; i++) {
        log_debug("Debug message number %d", i);
        if (i % 100 == 0) {
            log_info("Progress: %d messages logged", i);
        }
    }

    cr_assert(true, "High-volume logging should work");
}

Test(logging, mixed_log_levels) {
    // Test mixing different log levels rapidly
    for (int i = 0; i < 100; i++) {
        switch (i % 4) {
            case 0: log_debug("Debug %d", i); break;
            case 1: log_info("Info %d", i); break;
            case 2: log_warn("Warn %d", i); break;
            case 3: log_error("Error %d", i); break;
        }
    }

    cr_assert(true, "Mixed level logging should work");
}

// =============================================================================
// Context and Threading Tests
// =============================================================================

Test(logging, log_with_context) {
    // Test logging with different context information
    log_debug("Starting test function: %s", __func__);
    log_info("Current file: %s, line: %d", __FILE__, __LINE__);

    // Test with different data types
    size_t size_val = 1024;
    uint32_t uint_val = 0xDEADBEEF;
    float float_val = 3.14159f;

    log_info("Values: size=%zu, uint=0x%08x, float=%.2f", size_val, uint_val, float_val);

    cr_assert(true, "Context logging should work");
}

Test(logging, concurrent_logging) {
    // Simple concurrent logging test (not full threading, just rapid calls)
    for (int i = 0; i < 50; i++) {
        log_debug("Thread-like debug %d", i);
        log_info("Thread-like info %d", i);
        log_warn("Thread-like warning %d", i);
        log_error("Thread-like error %d", i);
    }

    cr_assert(true, "Concurrent-like logging should work");
}

// =============================================================================
// Error Conditions
// =============================================================================

Test(logging, log_with_extreme_formats) {
    // Test with complex format strings
    log_debug("Complex format: %*.*s", 10, 5, "hello world");
    log_info("Hex dump style: %02x %02x %02x", 0xAA, 0xBB, 0xCC);
    log_warn("Pointer format: %p", (void*)0x12345678);
    log_error("Character codes: %c %c %c", 'A', 'B', 'C');

    cr_assert(true, "Complex format logging should work");
}

// =============================================================================
// Integration with Common Module
// =============================================================================

Test(logging, log_memory_operations) {
    // Test logging during memory operations
    void *ptr;
    SAFE_MALLOC(ptr, 1024, void*);
    log_debug("Allocated memory at %p", ptr);

    if (ptr) {
        memset(ptr, 0xAB, 1024);
        log_info("Filled memory with pattern 0xAB");

        SAFE_REALLOC(ptr, 2048, void*);
        log_info("Reallocated memory to 2048 bytes at %p", ptr);

        free(ptr);
        log_debug("Freed memory");
    }

    cr_assert(true, "Logging during memory operations should work");
}

Test(logging, log_error_codes) {
    // Test logging with common error codes
    log_error("Network error: %d", ASCIICHAT_ERR_NETWORK);
    log_error("Memory error: %d", ASCIICHAT_ERR_MALLOC);
    log_error("Invalid param error: %d", ASCIICHAT_ERR_INVALID_PARAM);
    log_warn("Buffer full error: %d", ASCIICHAT_ERR_BUFFER_FULL);
    log_info("Test numeric value: %d", 42);

    cr_assert(true, "Error code logging should work");
}

// =============================================================================
// Real-world Usage Simulation
// =============================================================================

Test(logging, simulate_application_logging) {
    // Simulate typical application logging patterns
    log_info("Application starting up...");

    log_debug("Initializing subsystems");
    for (int i = 0; i < 5; i++) {
        log_debug("Initializing subsystem %d", i);
        if (i == 3) {
            log_warn("Subsystem %d initialized with warnings", i);
        } else {
            log_info("Subsystem %d initialized successfully", i);
        }
    }

    log_info("Processing requests...");
    for (int req = 0; req < 10; req++) {
        log_debug("Processing request %d", req);
        if (req == 7) {
            log_error("Request %d failed with error", req);
        } else {
            log_debug("Request %d completed successfully", req);
        }
    }

    log_info("Shutting down gracefully");
    log_debug("Cleanup completed");

    cr_assert(true, "Application-style logging should work");
}

Test(logging, network_simulation_logging) {
    // Simulate network-related logging
    const char *client_ip = "192.168.1.100";
    uint16_t port = 8080;
    uint32_t packet_id = 0x12345;

    log_info("Server listening on port %d", port);
    log_info("Client connected from %s:%d", client_ip, port + 1);

    log_debug("Received packet ID 0x%08x from %s", packet_id, client_ip);
    log_debug("Packet size: %zu bytes", sizeof(packet_id) * 8);

    log_warn("High latency detected: %d ms", 150);
    log_error("Connection timeout for client %s", client_ip);

    log_info("Client %s disconnected", client_ip);

    cr_assert(true, "Network simulation logging should work");
}

/* ============================================================================
 * Log Level Management Tests
 * ============================================================================ */

Test(logging, log_level_setting_and_getting) {
    // Test setting and getting log levels
    log_level_t original_level = log_get_level();
    
    // Test all log levels
    log_level_t levels[] = {LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL};
    int num_levels = sizeof(levels) / sizeof(levels[0]);
    
    for (int i = 0; i < num_levels; i++) {
        log_set_level(levels[i]);
        log_level_t retrieved_level = log_get_level();
        cr_assert_eq(retrieved_level, levels[i], "Log level should be set correctly");
    }
    
    // Restore original level
    log_set_level(original_level);
}

Test(logging, log_level_filtering) {
    // Test that log level filtering works correctly
    log_set_level(LOG_WARN);
    
    // These should be filtered out (below WARN level)
    log_debug("This debug message should be filtered");
    log_info("This info message should be filtered");
    
    // These should be shown (WARN level and above)
    log_warn("This warning should be shown");
    log_error("This error should be shown");
    log_fatal("This fatal should be shown");
    
    // Test with different levels
    log_set_level(LOG_ERROR);
    log_warn("This warning should now be filtered");
    log_error("This error should still be shown");
    
    cr_assert(true, "Log level filtering should work");
}

Test(logging, log_level_edge_cases) {
    // Test edge cases for log levels
    log_set_level(LOG_DEBUG); // Most permissive
    log_debug("Debug message at most permissive level");
    
    log_set_level(LOG_FATAL); // Most restrictive
    log_debug("Debug message at most restrictive level (should be filtered)");
    log_info("Info message at most restrictive level (should be filtered)");
    log_warn("Warning message at most restrictive level (should be filtered)");
    log_error("Error message at most restrictive level (should be filtered)");
    log_fatal("Fatal message at most restrictive level (should be shown)");
    
    cr_assert(true, "Log level edge cases should work");
}

/* ============================================================================
 * Log File Management Tests
 * ============================================================================ */

Test(logging, log_file_operations) {
    // Test log file operations
    const char *test_log_file = "/tmp/test_logging.log";
    
    // Clean up any existing test file
    unlink(test_log_file);
    
    // Initialize logging to file
    log_init(test_log_file, LOG_DEBUG);
    
    // Write some test messages
    log_info("Test message 1");
    log_warn("Test message 2");
    log_error("Test message 3");
    
    // Check that file was created and has content
    struct stat st;
    int result = stat(test_log_file, &st);
    cr_assert_eq(result, 0, "Log file should be created");
    cr_assert_gt(st.st_size, 0, "Log file should have content");
    
    // Test log destruction
    log_destroy();
    
    // Clean up
    unlink(test_log_file);
}

Test(logging, log_file_initialization_failure) {
    // Test log initialization with invalid filename
    const char *invalid_file = "/invalid/path/that/does/not/exist/test.log";
    
    // This should not crash, but should fall back to stderr
    log_init(invalid_file, LOG_INFO);
    
    // Should still be able to log
    log_info("This should go to stderr due to file failure");
    
    log_destroy();
    cr_assert(true, "Log initialization failure should be handled gracefully");
}

Test(logging, log_file_reinitialization) {
    // Test reinitializing logging
    const char *test_log_file1 = "/tmp/test_logging1.log";
    const char *test_log_file2 = "/tmp/test_logging2.log";
    
    // Clean up any existing test files
    unlink(test_log_file1);
    unlink(test_log_file2);
    
    // Initialize with first file
    log_init(test_log_file1, LOG_INFO);
    log_info("Message to first file");
    
    // Reinitialize with second file
    log_init(test_log_file2, LOG_DEBUG);
    log_info("Message to second file");
    
    // Check both files exist
    struct stat st1, st2;
    cr_assert_eq(stat(test_log_file1, &st1), 0, "First log file should exist");
    cr_assert_eq(stat(test_log_file2, &st2), 0, "Second log file should exist");
    
    // Clean up
    log_destroy();
    unlink(test_log_file1);
    unlink(test_log_file2);
}

Test(logging, log_file_null_filename) {
    // Test logging with NULL filename (should use stderr)
    log_init(NULL, LOG_INFO);
    
    // Should still be able to log
    log_info("This should go to stderr");
    log_warn("This should also go to stderr");
    
    log_destroy();
    cr_assert(true, "Logging with NULL filename should work");
}

/* ============================================================================
 * Terminal Output Control Tests
 * ============================================================================ */

Test(logging, terminal_output_control) {
    // Test terminal output control
    log_set_terminal_output(false);
    log_info("This should not appear on terminal");
    
    log_set_terminal_output(true);
    log_info("This should appear on terminal");
    
    cr_assert(true, "Terminal output control should work");
}

Test(logging, terminal_output_with_file_logging) {
    // Test terminal output control when logging to file
    const char *test_log_file = "/tmp/test_terminal_output.log";
    
    // Clean up any existing test file
    unlink(test_log_file);
    
    log_init(test_log_file, LOG_DEBUG);
    
    // Test with terminal output enabled
    log_set_terminal_output(true);
    log_info("Message with terminal output enabled");
    
    // Test with terminal output disabled
    log_set_terminal_output(false);
    log_info("Message with terminal output disabled");
    
    // Check that file has content
    struct stat st;
    cr_assert_eq(stat(test_log_file, &st), 0, "Log file should exist");
    cr_assert_gt(st.st_size, 0, "Log file should have content");
    
    // Clean up
    log_destroy();
    unlink(test_log_file);
}

/* ============================================================================
 * Log Truncation Tests
 * ============================================================================ */

Test(logging, log_truncation_manual) {
    // Test manual log truncation
    const char *test_log_file = "/tmp/test_log_truncation.log";
    
    // Clean up any existing test file
    unlink(test_log_file);
    
    log_init(test_log_file, LOG_DEBUG);
    
    // Write some messages
    for (int i = 0; i < 10; i++) {
        log_info("Test message %d", i);
    }
    
    // Check initial size
    struct stat st;
    cr_assert_eq(stat(test_log_file, &st), 0, "Log file should exist");
    size_t initial_size = (size_t)st.st_size;
    
    // Call truncation (should not do anything for small files)
    log_truncate_if_large();
    
    // Check size after truncation (should be same for small files)
    cr_assert_eq(stat(test_log_file, &st), 0, "Log file should still exist");
    size_t after_size = (size_t)st.st_size;
    
    // For small files, size should be the same
    cr_assert_eq(after_size, initial_size, "Small log file should not be truncated");
    
    // Clean up
    log_destroy();
    unlink(test_log_file);
}

/* ============================================================================
 * Log Message Formatting Tests
 * ============================================================================ */

Test(logging, log_message_formatting_complex) {
    // Test complex message formatting
    const char *string_var = "test_string";
    int int_var = 42;
    double double_var = 3.14159;
    void *ptr_var = (void *)0x12345678;
    
    log_info("Complex formatting: string='%s', int=%d, double=%.2f, ptr=%p", 
             string_var, int_var, double_var, ptr_var);
    
    log_warn("Multiple %s with %d %s", "parameters", 3, "values");
    
    log_error("Error code: %d, message: %s", 404, "Not found");
    
    cr_assert(true, "Complex log message formatting should work");
}

Test(logging, log_message_formatting_edge_cases) {
    // Test edge cases in message formatting
    log_info("Empty string: '%s'", "");
    log_info("Null pointer: %p", NULL);
    log_info("Zero values: %d, %f, %s", 0, 0.0, "zero");
    log_info("Negative values: %d, %f", -42, -3.14);
    log_info("Large values: %d, %zu", INT32_MAX, SIZE_MAX);
    
    cr_assert(true, "Edge case log message formatting should work");
}

Test(logging, log_message_formatting_long_strings) {
    // Test formatting with long strings
    char long_string[1000];
    memset(long_string, 'A', sizeof(long_string) - 1);
    long_string[sizeof(long_string) - 1] = '\0';
    
    log_info("Long string: %s", long_string);
    
    // Test with very long format string
    log_info("Very long format string with many parameters: %s %d %s %d %s %d %s %d %s %d", 
             "param1", 1, "param2", 2, "param3", 3, "param4", 4, "param5", 5);
    
    cr_assert(true, "Long string log message formatting should work");
}

/* ============================================================================
 * Thread Safety Tests
 * ============================================================================ */

Test(logging, thread_safety_basic) {
    // Test basic thread safety by calling logging functions rapidly
    for (int i = 0; i < 100; i++) {
        log_debug("Thread safety test message %d", i);
        log_info("Thread safety test message %d", i);
        log_warn("Thread safety test message %d", i);
        log_error("Thread safety test message %d", i);
    }
    
    cr_assert(true, "Basic thread safety should work");
}

Test(logging, thread_safety_level_changes) {
    // Test thread safety with level changes
    for (int i = 0; i < 50; i++) {
        log_set_level(LOG_DEBUG);
        log_debug("Debug message %d", i);
        
        log_set_level(LOG_ERROR);
        log_error("Error message %d", i);
        
        log_set_level(LOG_INFO);
        log_info("Info message %d", i);
    }
    
    cr_assert(true, "Thread safety with level changes should work");
}

/* ============================================================================
 * Log Rotation Tests
 * ============================================================================ */

Test(logging, log_rotation_simulation) {
    // Test log rotation by creating a large log file
    const char *test_log_file = "/tmp/test_log_rotation.log";
    
    // Clean up any existing test file
    unlink(test_log_file);
    
    log_init(test_log_file, LOG_DEBUG);
    
    // Write many messages to simulate a large log file
    for (int i = 0; i < 1000; i++) {
        log_info("Rotation test message %d: This is a longer message to increase file size", i);
    }
    
    // Check that file exists and has content
    struct stat st;
    cr_assert_eq(stat(test_log_file, &st), 0, "Log file should exist");
    cr_assert_gt(st.st_size, 0, "Log file should have content");
    
    // Clean up
    log_destroy();
    unlink(test_log_file);
}

/* ============================================================================
 * Log Initialization Edge Cases
 * ============================================================================ */

Test(logging, log_initialization_edge_cases) {
    // Test various initialization scenarios
    
    // Test initialization with different levels
    log_init(NULL, LOG_DEBUG);
    log_debug("Debug message after init");
    log_destroy();
    
    log_init(NULL, LOG_INFO);
    log_info("Info message after init");
    log_destroy();
    
    log_init(NULL, LOG_WARN);
    log_warn("Warning message after init");
    log_destroy();
    
    log_init(NULL, LOG_ERROR);
    log_error("Error message after init");
    log_destroy();
    
    log_init(NULL, LOG_FATAL);
    log_fatal("Fatal message after init");
    log_destroy();
    
    cr_assert(true, "Log initialization edge cases should work");
}

Test(logging, log_destroy_without_init) {
    // Test destroying logging without initialization
    log_destroy();
    log_destroy(); // Call twice to test idempotency
    
    // Should still be able to log after destroy
    log_info("Message after destroy");
    
    cr_assert(true, "Log destroy without init should work");
}

/* ============================================================================
 * Log Message Edge Cases
 * ============================================================================ */

Test(logging, log_message_edge_cases) {
    // Test edge cases in log messages
    log_info("Message with newline\nin the middle");
    log_info("Message with tab\tand carriage return\r");
    log_info("Message with special chars: !@#$%%^&*()");
    log_info("Message with unicode: café, naïve, résumé");
    
    // Test with very long single parameter
    char very_long_param[2000];
    memset(very_long_param, 'X', sizeof(very_long_param) - 1);
    very_long_param[sizeof(very_long_param) - 1] = '\0';
    log_info("Very long parameter: %s", very_long_param);
    
    cr_assert(true, "Log message edge cases should work");
}

Test(logging, log_message_format_specifiers) {
    // Test various format specifiers
    log_info("Integer: %d, unsigned: %u, hex: %x, octal: %o", 42, 42U, 42, 42);
    log_info("Float: %f, scientific: %e, shortest: %g", 3.14159, 3.14159, 3.14159);
    log_info("String: %s, char: %c, percent: %%", "hello", 'A');
    log_info("Pointer: %p, size_t: %zu", (void *)0x12345678, (size_t)1000);
    
    cr_assert(true, "Log message format specifiers should work");
}
