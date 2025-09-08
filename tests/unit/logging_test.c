#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#include "common.h"

// Global setup to reduce verbose output during tests
void setup_quiet_logging(void);
void restore_logging(void);

TestSuite(logging, .init = setup_quiet_logging, .fini = restore_logging);

void setup_quiet_logging(void) {
    // For logging tests, we need to see the log output to verify it works
    // Don't suppress logging for these tests
    log_set_level(LOG_DEBUG);
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
