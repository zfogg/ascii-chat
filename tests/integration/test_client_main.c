// Integration test that runs the actual client main() with mocked webcam
// This demonstrates how to test the complete client with mock

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// For testing main(), we need to compile a special version of client
// Build it with: gcc -DUSE_WEBCAM_MOCK -o test_client src/client.c tests/mocks/webcam_mock.c ...

Test(client_main_mock, test_client_help_with_mock) {
    // Even with mock, --help should work
    pid_t pid = fork();
    if (pid == 0) {
        // Child: run client with mock enabled
        char *argv[] = {"client", "--help", NULL};

        // Set environment to use mock
        setenv("WEBCAM_MOCK", "1", 1);

        // This would need a specially compiled client with mock
        execv("./build/bin/ascii-chat-client-mock", argv);
        exit(127);  // If exec fails
    }

    int status;
    waitpid(pid, &status, 0);
    cr_assert(WIFEXITED(status), "Client should exit normally");
    cr_assert_eq(WEXITSTATUS(status), 0, "Help should exit with 0");
}

// Example of how to build client with mock for testing:
// Create a CMake target that compiles client with mock

/*
CMakeLists.txt addition:

# Test version of client with mocked webcam
if(BUILD_TESTS AND CRITERION_FOUND)
    add_executable(ascii-chat-client-mock
        src/client.c
        tests/mocks/webcam_mock.c
        ${LIB_SRCS}  # All library sources except real webcam
    )

    target_compile_definitions(ascii-chat-client-mock PRIVATE
        USE_WEBCAM_MOCK=1
        WEBCAM_MOCK_ENABLED=1
    )

    target_include_directories(ascii-chat-client-mock PRIVATE
        ${CMAKE_SOURCE_DIR}/lib
        ${CMAKE_SOURCE_DIR}/tests/mocks
    )

    target_link_libraries(ascii-chat-client-mock
        ${COMMON_LIBS}
    )
endif()
*/