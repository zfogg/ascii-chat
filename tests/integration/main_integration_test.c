#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "tests/common.h"
#include "platform/abstraction.h"

// Test configuration
#define TEST_PORT_BASE 10000
#define SERVER_STARTUP_DELAY_MS 100
#define CLIENT_CONNECT_TIMEOUT_MS 2000
#define PROCESS_CLEANUP_TIMEOUT_MS 1000
#define MAX_PROCESSES 10

// Process management
typedef struct {
  pid_t pid;
  const char *name;
  int exit_code;
  bool running;
} process_info_t;

static process_info_t tracked_processes[MAX_PROCESSES];
static int process_count = 0;

// Port allocation using PID to avoid collisions when Criterion runs tests in parallel.
// Each forked test process gets a unique port range based on its PID.
// Range: TEST_PORT_BASE to TEST_PORT_BASE + 50000 (ports 10000-60000)
static int get_unique_test_port(void) {
  static int port_offset = 0;
  // Use PID to create unique port base per process (each process gets 10 ports)
  int pid_offset = (getpid() % 5000) * 10;
  return TEST_PORT_BASE + pid_offset + (port_offset++ % 10);
}

// Logging control
static log_level_t original_log_level;

void setup_main_tests(void) {
  original_log_level = log_get_level();
  log_set_level(LOG_FATAL);
  process_count = 0;
  memset(tracked_processes, 0, sizeof(tracked_processes));
  // Disable host identity check for tests since we don't have a TTY for prompts
  setenv("ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK", "1", 1);
}

void teardown_main_tests(void) {
  // Kill any remaining processes
  for (int i = 0; i < process_count; i++) {
    if (tracked_processes[i].running && tracked_processes[i].pid > 0) {
      kill(tracked_processes[i].pid, SIGTERM);
      usleep(100000); // 100ms grace period
      kill(tracked_processes[i].pid, SIGKILL);
      waitpid(tracked_processes[i].pid, NULL, 0);
    }
  }
  log_set_level(original_log_level);
  // Clean up test environment
}

TestSuite(main_integration, .init = setup_main_tests, .fini = teardown_main_tests);

// =============================================================================
// Process Management Utilities
// =============================================================================

// Use shared binary path detection from tests/common.h
#define get_binary_path test_get_binary_path

static pid_t spawn_process(const char *path, char *const argv[], const char *name) {
  pid_t pid = fork();
  if (pid == 0) {
    // Child: redirect output to log file
    char log_path[256];
    safe_snprintf(log_path, sizeof(log_path), "/tmp/ascii_chat_test_%s_%d.log", name, getpid());

    FILE *log_file = fopen(log_path, "w");
    if (log_file) {
      dup2(fileno(log_file), STDOUT_FILENO);
      dup2(fileno(log_file), STDERR_FILENO);
      fclose(log_file);
    }

    execv(path, argv);
    fprintf(stderr, "Failed to exec %s: %s\n", path, strerror(errno));
    exit(127);
  }

  if (pid > 0 && process_count < MAX_PROCESSES) {
    tracked_processes[process_count].pid = pid;
    tracked_processes[process_count].name = name;
    tracked_processes[process_count].running = true;
    process_count++;
  }

  return pid;
}

static bool wait_for_process_exit(pid_t pid, int timeout_ms, int *exit_code) {
  int elapsed_ms = 0;
  const int poll_interval_ms = 10;

  while (elapsed_ms < timeout_ms) {
    int status;
    pid_t result = waitpid(pid, &status, WNOHANG);

    if (result == pid) {
      if (WIFEXITED(status)) {
        if (exit_code)
          *exit_code = WEXITSTATUS(status);
        return true;
      }
      if (WIFSIGNALED(status)) {
        if (exit_code)
          *exit_code = 128 + WTERMSIG(status);
        return true;
      }
    } else if (result < 0) {
      return false; // Error
    }

    usleep(poll_interval_ms * 1000);
    elapsed_ms += poll_interval_ms;
  }

  return false; // Timeout
}

static void terminate_process(pid_t pid, const char *name) {
  UNUSED(name);
  if (pid <= 0)
    return;

  // Try graceful termination first
  kill(pid, SIGTERM);

  int exit_code = -1;
  if (!wait_for_process_exit(pid, PROCESS_CLEANUP_TIMEOUT_MS, &exit_code)) {
    // Force kill if graceful shutdown failed
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    exit_code = -1; // Force killed
  }

  // Mark as not running
  for (int i = 0; i < process_count; i++) {
    if (tracked_processes[i].pid == pid) {
      tracked_processes[i].running = false;
      tracked_processes[i].exit_code = exit_code;
      break;
    }
  }
}

static bool wait_for_tcp_port(int port, int timeout_ms) {
  int elapsed_ms = 0;
  const int poll_interval_ms = 50;

  while (elapsed_ms < timeout_ms) {
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET_VALUE)
      return false;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int result = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    socket_close(sock);

    if (result == 0) {
      return true; // Port is open
    }

    usleep(poll_interval_ms * 1000);
    elapsed_ms += poll_interval_ms;
  }

  return false;
}

// =============================================================================
// Server Main Function Tests
// =============================================================================

Test(main_integration, server_main_starts_and_stops) {
  int port = get_unique_test_port();
  char port_str[16];
  safe_snprintf(port_str, sizeof(port_str), "%d", port);

  char *argv[] = {"ascii-chat", "server", "--port", port_str, "--log-file", "/tmp/test_server_main.log", NULL};

  pid_t server_pid = spawn_process(get_binary_path(), argv, "server");
  cr_assert_gt(server_pid, 0, "Server should spawn successfully");

  // Wait for server to start listening
  bool started = wait_for_tcp_port(port, 2000);
  cr_assert(started, "Server should start listening on port %d", port);

  // Verify server is still running
  int status;
  pid_t result = waitpid(server_pid, &status, WNOHANG);
  cr_assert_eq(result, 0, "Server should still be running");

  // Graceful shutdown
  terminate_process(server_pid, "server");
}

Test(main_integration, server_main_help_flag) {
  char *argv[] = {"ascii-chat", "server", "--help", NULL};

  pid_t server_pid = spawn_process(get_binary_path(), argv, "server_help");
  cr_assert_gt(server_pid, 0, "Server should spawn for help");

  int exit_code;
  bool exited = wait_for_process_exit(server_pid, 1000, &exit_code);
  cr_assert(exited, "Server should exit after showing help");
  cr_assert_eq(exit_code, 0, "Server should exit with code 0 for --help");
}

Test(main_integration, server_main_invalid_port) {
  char *argv[] = {"ascii-chat", "server", "--port", "99999", // Invalid port
                  NULL};

  pid_t server_pid = spawn_process(get_binary_path(), argv, "server_bad_port");
  cr_assert_gt(server_pid, 0, "Server should spawn");

  int exit_code;
  bool exited = wait_for_process_exit(server_pid, 2000, &exit_code);
  cr_assert(exited, "Server should exit on invalid port");
  cr_assert_neq(exit_code, 0, "Server should exit with non-zero code for invalid port");
}

// =============================================================================
// Client Main Function Tests
// =============================================================================

Test(main_integration, client_main_help_flag) {
  char *argv[] = {"ascii-chat", "client", "--help", NULL};

  pid_t client_pid = spawn_process(get_binary_path(), argv, "client_help");
  cr_assert_gt(client_pid, 0, "Client should spawn for help");

  int exit_code;
  bool exited = wait_for_process_exit(client_pid, 1000, &exit_code);
  cr_assert(exited, "Client should exit after showing help");
  cr_assert_eq(exit_code, 0, "Client should exit with code 0 for --help");
}

Test(main_integration, client_main_no_server) {
  // Client is designed to retry connecting forever - verify it stays alive
  int port = get_unique_test_port();
  char port_str[16];
  safe_snprintf(port_str, sizeof(port_str), "%d", port);

  char *argv[] = {"ascii-chat", "client", "--port", port_str, "--address", "127.0.0.1", "--test-pattern", NULL};

  pid_t client_pid = spawn_process(get_binary_path(), argv, "client_no_server");
  cr_assert_gt(client_pid, 0, "Client should spawn");

  // Wait 200ms - client should still be running (retrying connection)
  usleep(200000);

  // Verify client is still alive (waitpid with WNOHANG returns 0 if still running)
  int status;
  pid_t result = waitpid(client_pid, &status, WNOHANG);
  cr_assert_eq(result, 0, "Client should still be running while retrying connection");

  // Clean up - terminate the client
  terminate_process(client_pid, "client_no_server");
}

// =============================================================================
// Combined Server-Client Tests
// =============================================================================

Test(main_integration, server_client_basic_connection) {
  int port = get_unique_test_port();
  char port_str[16];
  safe_snprintf(port_str, sizeof(port_str), "%d", port);

  // Start server (no encryption for speed)
  char *server_argv[] = {"ascii-chat", "server", "--port", port_str, "--no-encrypt", "--log-file",
                         "/tmp/test_server_client.log", NULL};

  pid_t server_pid = spawn_process(get_binary_path(), server_argv, "server");
  cr_assert_gt(server_pid, 0, "Server should spawn");

  // Wait for server to be ready
  bool server_ready = wait_for_tcp_port(port, 2000);
  cr_assert(server_ready, "Server should be listening");

  // Start client with test pattern (no webcam needed in Docker)
  char *client_argv[] = {"ascii-chat",
                         "client",
                         "--port",
                         port_str,
                         "--address",
                         "127.0.0.1",
                         "--no-encrypt",   // Skip crypto handshake for speed
                         "--test-pattern", // Use test pattern instead of webcam
                         "--snapshot",     // Take single snapshot and exit immediately
                         "--snapshot-delay",
                         "0",
                         "--log-file",
                         "/tmp/test_client.log",
                         NULL};

  pid_t client_pid = spawn_process(get_binary_path(), client_argv, "client");
  cr_assert_gt(client_pid, 0, "Client should spawn");

  // Wait for client to complete
  int client_exit_code;
  bool client_exited = wait_for_process_exit(client_pid, 2000, &client_exit_code);
  cr_assert(client_exited, "Client should complete snapshot");
  cr_assert_eq(client_exit_code, 0, "Client should exit successfully");

  // Clean up server
  terminate_process(server_pid, "server");
}

Test(main_integration, server_multiple_clients_sequential) {
  int port = get_unique_test_port();
  char port_str[16];
  safe_snprintf(port_str, sizeof(port_str), "%d", port);

  // Start server (no encryption for speed)
  char *server_argv[] = {"ascii-chat", "server", "--port", port_str, "--no-encrypt", "--log-file",
                         "/tmp/test_multi_seq.log", NULL};

  pid_t server_pid = spawn_process(get_binary_path(), server_argv, "server");
  cr_assert_gt(server_pid, 0, "Server should spawn");

  bool server_ready = wait_for_tcp_port(port, 2000);
  cr_assert(server_ready, "Server should be listening");

  // Connect multiple clients sequentially with test pattern (no webcam needed)
  for (int i = 0; i < 2; i++) {
    char client_name[32];
    safe_snprintf(client_name, sizeof(client_name), "client_%d", i);

    char *client_argv[] = {"ascii-chat",
                           "client",
                           "--port",
                           port_str,
                           "--address",
                           "127.0.0.1",
                           "--no-encrypt",
                           "--test-pattern",
                           "--snapshot",
                           "--snapshot-delay",
                           "0",
                           "--log-file",
                           "/tmp/test_client_seq.log",
                           NULL};

    pid_t client_pid = spawn_process(get_binary_path(), client_argv, client_name);
    cr_assert_gt(client_pid, 0, "Client %d should spawn", i);

    int exit_code;
    bool exited = wait_for_process_exit(client_pid, 2000, &exit_code);
    cr_assert(exited, "Client %d should complete", i);
    cr_assert_eq(exit_code, 0, "Client %d should exit successfully", i);
  }

  terminate_process(server_pid, "server");
}

Test(main_integration, server_multiple_clients_concurrent) {
  int port = get_unique_test_port();
  char port_str[16];
  safe_snprintf(port_str, sizeof(port_str), "%d", port);

  // Start server (no encryption for speed)
  char *server_argv[] = {"ascii-chat", "server", "--port", port_str, "--no-encrypt", "--log-file",
                         "/tmp/test_multi_concurrent.log", NULL};

  pid_t server_pid = spawn_process(get_binary_path(), server_argv, "server");
  cr_assert_gt(server_pid, 0, "Server should spawn");

  bool server_ready = wait_for_tcp_port(port, 2000);
  cr_assert(server_ready, "Server should be listening");

  // Start multiple clients concurrently with test pattern (no webcam needed)
  pid_t client_pids[2];
  for (int i = 0; i < 2; i++) {
    char client_name[32];
    safe_snprintf(client_name, sizeof(client_name), "client_%d", i);

    char *client_argv[] = {"ascii-chat",
                           "client",
                           "--port",
                           port_str,
                           "--address",
                           "127.0.0.1",
                           "--no-encrypt",   // Skip crypto handshake for speed
                           "--test-pattern", // Use test pattern instead of webcam
                           "--snapshot",     // Take single snapshot and exit
                           "--snapshot-delay",
                           "0",
                           "--log-file",
                           "/tmp/test_client_concurrent.log",
                           NULL};

    client_pids[i] = spawn_process(get_binary_path(), client_argv, client_name);
    cr_assert_gt(client_pids[i], 0, "Client %d should spawn", i);
    usleep(50000); // 50ms between client starts
  }

  // Wait for all clients to complete
  for (int i = 0; i < 2; i++) {
    int exit_code;
    bool exited = wait_for_process_exit(client_pids[i], 2000, &exit_code);
    cr_assert(exited, "Client %d should complete", i);
    cr_assert_eq(exit_code, 0, "Client %d should exit successfully", i);
  }

  terminate_process(server_pid, "server");
}

Test(main_integration, server_client_with_options) {
  int port = get_unique_test_port();
  char port_str[16];
  safe_snprintf(port_str, sizeof(port_str), "%d", port);

  // Start server with standard options (no encryption for speed)
  char *server_argv[] = {"ascii-chat", "server", "--port", port_str, "--no-encrypt", "--log-file",
                         "/tmp/test_server_options.log", NULL};

  pid_t server_pid = spawn_process(get_binary_path(), server_argv, "server");
  cr_assert_gt(server_pid, 0, "Server should spawn with options");

  bool server_ready = wait_for_tcp_port(port, 2000);
  cr_assert(server_ready, "Server should be listening");

  // Start client with options (test pattern for no webcam)
  // Note: --color-mode is the correct option, not --color
  char *client_argv[] = {"ascii-chat",
                         "client",
                         "--port",
                         port_str,
                         "--address",
                         "127.0.0.1",
                         "--no-encrypt",   // Skip crypto handshake for speed
                         "--test-pattern", // Use test pattern instead of webcam
                         "--color-mode",
                         "auto",
                         "--width",
                         "80",
                         "--height",
                         "24",
                         "--snapshot", // Take single snapshot and exit
                         "--snapshot-delay",
                         "0",
                         "--log-file",
                         "/tmp/test_client_options.log",
                         NULL};

  pid_t client_pid = spawn_process(get_binary_path(), client_argv, "client");
  cr_assert_gt(client_pid, 0, "Client should spawn with options");

  int client_exit_code;
  bool client_exited = wait_for_process_exit(client_pid, 2000, &client_exit_code);
  cr_assert(client_exited, "Client should complete");
  cr_assert_eq(client_exit_code, 0, "Client should exit successfully with options");

  terminate_process(server_pid, "server");
}

Test(main_integration, server_survives_client_crash) {
  int port = get_unique_test_port();
  char port_str[16];
  safe_snprintf(port_str, sizeof(port_str), "%d", port);

  // Start server (no encryption for speed)
  char *server_argv[] = {"ascii-chat", "server", "--port", port_str, "--no-encrypt", "--log-file",
                         "/tmp/test_server_survives.log", NULL};

  pid_t server_pid = spawn_process(get_binary_path(), server_argv, "server");
  cr_assert_gt(server_pid, 0, "Server should spawn");

  bool server_ready = wait_for_tcp_port(port, 2000);
  cr_assert(server_ready, "Server should be listening");

  // Start client with test pattern (no webcam needed)
  char *client_argv[] = {"ascii-chat",
                         "client",
                         "--port",
                         port_str,
                         "--address",
                         "127.0.0.1",
                         "--no-encrypt",   // Skip crypto handshake for speed
                         "--test-pattern",
                         "--log-file",
                         "/tmp/test_client_crash.log",
                         NULL};

  pid_t client_pid = spawn_process(get_binary_path(), client_argv, "client");
  cr_assert_gt(client_pid, 0, "Client should spawn");

  usleep(100000); // 100ms - Let client connect (fast with --no-encrypt)

  // Kill client abruptly
  kill(client_pid, SIGKILL);
  waitpid(client_pid, NULL, 0);

  // Server should still be running
  int status;
  pid_t result = waitpid(server_pid, &status, WNOHANG);
  cr_assert_eq(result, 0, "Server should survive client crash");

  // Try connecting another client to verify server is still functional
  char *client2_argv[] = {"ascii-chat",
                          "client",
                          "--port",
                          port_str,
                          "--address",
                          "127.0.0.1",
                          "--no-encrypt",   // Skip crypto handshake for speed
                          "--test-pattern",
                          "--snapshot",
                          "--snapshot-delay",
                          "0",
                          "--log-file",
                          "/tmp/test_client_after_crash.log",
                          NULL};

  pid_t client2_pid = spawn_process(get_binary_path(), client2_argv, "client2");
  cr_assert_gt(client2_pid, 0, "Second client should spawn");

  int exit_code;
  bool exited = wait_for_process_exit(client2_pid, 2000, &exit_code);
  cr_assert(exited, "Second client should complete");
  cr_assert_eq(exit_code, 0, "Second client should connect successfully");

  terminate_process(server_pid, "server");
}
