/**
 * Test program to verify shared library exports work correctly
 *
 * This program links against asciichat.dll (Windows) or libasciichat.so (Linux)
 * and calls basic API functions to ensure the library is properly exporting symbols.
 */

#include <stdio.h>
#include "logging.h"

int main(void) {
    printf("Testing ascii-chat shared library...\n");

    // Initialize logging
    printf("1. Calling log_init()...\n");
    log_init(NULL, LOG_INFO);

    // Log a message
    printf("2. Calling log_info()...\n");
    log_info("Hello from test program! Library works correctly.");

    // Cleanup
    printf("3. Calling log_destroy()...\n");
    log_destroy();

    printf("\nSuccess! Shared library is working correctly.\n");
    return 0;
}
