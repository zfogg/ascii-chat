// Integration test that runs the actual client main() with --help
// This verifies the client binary works and shows help correctly

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

Test(client_main, test_client_help) {
#ifdef _WIN32
  // Windows version - use CreateProcess
  STARTUPINFO si;
  PROCESS_INFORMATION pi;

  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  ZeroMemory(&pi, sizeof(pi));

  // Run client with --help
  char cmdLine[] = "ascii-chat-client.exe --help";
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
    cr_skip_test("Client binary not available");
  }
#else
  // Unix version - use fork/exec
  pid_t pid = fork();
  if (pid == 0) {
    // Child: run client with --help
    char *argv[] = {"ascii-chat-client", "--help", NULL};

    // Run the regular client binary
    execv("./build/bin/ascii-chat-client", argv);
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