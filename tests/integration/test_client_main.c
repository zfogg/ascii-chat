// Integration test that runs the actual client main() with mocked webcam
// This demonstrates how to test the complete client with mock

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdlib.h>

// Platform-specific includes
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

// For testing main(), we need to compile a special version of client
// Build it with: gcc -DUSE_WEBCAM_MOCK -o test_client src/client.c tests/mocks/webcam_mock.c ...

Test(client_main_mock, test_client_help_with_mock) {
#ifdef _WIN32
  // Windows version - use CreateProcess
  STARTUPINFO si;
  PROCESS_INFORMATION pi;

  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  ZeroMemory(&pi, sizeof(pi));

  // Set environment variable
  SetEnvironmentVariable("WEBCAM_MOCK", "1");

  // Try to run the mock client
  char cmdLine[] = "ascii-chat-client-mock.exe --help";
  if (CreateProcess(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
    // Wait for process to complete
    WaitForSingleObject(pi.hProcess, INFINITE);

    // Get exit code
    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    // Clean up
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    cr_assert_eq(exitCode, 0, "Help should exit with 0");
  } else {
    // Process creation failed - skip test
    cr_skip_test("Mock client not available");
  }
#else
  // Unix version - use fork/exec
  pid_t pid = fork();
  if (pid == 0) {
    // Child: run client with mock enabled
    char *argv[] = {"client", "--help", NULL};

    // Set environment to use mock
    setenv("WEBCAM_MOCK", "1", 1);

    // This would need a specially compiled client with mock
    execv("./build/bin/ascii-chat-client-mock", argv);
    exit(127); // If exec fails
  }

  int status;
  waitpid(pid, &status, 0);

  // Use parentheses to avoid macro expansion issues in C2X mode
  if (WIFEXITED(status)) {
    cr_assert_eq(WEXITSTATUS(status), 0, "Help should exit with 0");
  } else {
    cr_fail("Client did not exit normally");
  }
#endif
}

// Example of how to build client with mock for testing:
// Create a CMake target that compiles client with mock

/*
CMakeLists.txt addition:

# Build a special client with webcam mocking for testing
add_executable(ascii-chat-client-mock
    src/client.c
    tests/mocks/webcam_mock.c
    # ... other sources
)
target_compile_definitions(ascii-chat-client-mock PRIVATE USE_WEBCAM_MOCK)
target_link_libraries(ascii-chat-client-mock PRIVATE ${CLIENT_LIBS})
*/