// Integration test that runs the actual client main() with --help
// This verifies the client binary works and shows help correctly

#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
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

#include <stdbool.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>

// Use shared binary path detection from tests/common.h
#define get_binary_path test_get_binary_path
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TEST_CASE("client_main/test_client_help", "[client_main]") {
#ifdef _WIN32
  // Windows version - use CreateProcess
  STARTUPINFO si;
  PROCESS_INFORMATION pi;

  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  ZeroMemory(&pi, sizeof(pi));

  // Run client with --help
  char cmdLine[512];
  safe_snprintf(cmdLine, sizeof(cmdLine), "%s --help", get_binary_path());
  if (CreateProcess(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
    // Wait for process to complete
    WaitForSingleObject(pi.hProcess, INFINITE);

    // Get exit code
    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    // Clean up
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    ASCIICHAT_REQUIRE_EQ(exitCode, 0, "Help should exit with 0");
  } else {
    ASCIICHAT_SKIP("Client binary not available");
  }
#else
  // Unix version - use fork/exec
  pid_t pid = fork();
  if (pid == 0) {
    // Child: run client with --help
    char *argv[] = {"ascii-chat", "--help", NULL};

    // Run the regular client binary
    execv(get_binary_path(), argv);
    exit(127); // If exec fails
  }

  int status;
  waitpid(pid, &status, 0);

  // Use parentheses to avoid macro expansion issues in C2X mode
  if (WIFEXITED(status)) {
    ASCIICHAT_REQUIRE_EQ(WEXITSTATUS(status), 0, "Help should exit with 0");
  } else {
    ASCIICHAT_FAIL("Client did not exit normally");
  }
#endif
}
