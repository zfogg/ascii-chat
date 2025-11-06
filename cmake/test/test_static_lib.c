#include <stdio.h>
#include <stdlib.h>
#include "logging.h"

int main(void) {
    printf("Testing static library (libasciichat.a)...\n");

    // Initialize logging system
    printf("Calling log_init()...\n");
    log_init(NULL, LOG_INFO);
    printf("log_init() completed successfully!\n");

    // Test log_info macro
    printf("Calling log_info()...\n");
    log_info("Static library test: log_init() and log_info() work correctly!");

    printf("Calling log_debug()...\n");
    log_debug("This is a debug message from the static library test");

    printf("\n=== Test completed successfully! ===\n");
    printf("The static library (libasciichat.a) compiled and linked correctly.\n");
    printf("Successfully called log_init() and log_msg() functions.\n");

    return 0;
}
