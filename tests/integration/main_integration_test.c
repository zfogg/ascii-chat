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
#define SERVER_STARTUP_DELAY_MS 500
#define CLIENT_CONNECT_TIMEOUT_MS 5000
#define PROCESS_CLEANUP_TIMEOUT_MS 3000
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
static int next_test_port = TEST_PORT_BASE;

// Logging control
static log_level_t original_log_level;

void setup_main_tests(void) {
  original_log_level = log_get_level();
  log_set_level(LOG_FATAL);
  process_count = 0;
  memset(tracked_processes, 0, sizeof(tracked_processes));
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
}

TestSuite(main_integration, .init = setup_main_tests, .fini = teardown_main_tests);

// =============================================================================
// Process Management Utilities
// =============================================================================

static pid_t spawn_process(const char *path, char *const argv[], const char *name) {
  pid_t pid = fork();
  if (pid == 0) {
    // Child: redirect output to log file
    char log_path[256];
    snprintf(log_path, sizeof(log_path), "/tmp/ascii_chat_test_%s_%d.log", name, getpid());

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
  int port = next_test_port++;
  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%d", port);

  char *argv[] = {"ascii-chat-server", "--port", port_str, "--log-file", "/tmp/test_server_main.log", NULL};

  pid_t server_pid = spawn_process("./build/bin/ascii-chat-server", argv, "server");
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
  char *argv[] = {"ascii-chat-server", "--help", NULL};

  pid_t server_pid = spawn_process("./build/bin/ascii-chat-server", argv, "server_help");
  cr_assert_gt(server_pid, 0, "Server should spawn for help");

  int exit_code;
  bool exited = wait_for_process_exit(server_pid, 1000, &exit_code);
  cr_assert(exited, "Server should exit after showing help");
  cr_assert_eq(exit_code, 0, "Server should exit with code 0 for --help");
}

Test(main_integration, server_main_invalid_port) {
  char *argv[] = {"ascii-chat-server", "--port", "99999", // Invalid port
                  NULL};

  pid_t server_pid = spawn_process("./build/bin/ascii-chat-server", argv, "server_bad_port");
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
  char *argv[] = {"ascii-chat-client", "--help", NULL};

  pid_t client_pid = spawn_process("./build/bin/ascii-chat-client", argv, "client_help");
  cr_assert_gt(client_pid, 0, "Client should spawn for help");

  int exit_code;
  bool exited = wait_for_process_exit(client_pid, 1000, &exit_code);
  cr_assert(exited, "Client should exit after showing help");
  cr_assert_eq(exit_code, 0, "Client should exit with code 0 for --help");
}

Test(main_integration, client_main_no_server) {
  int port = next_test_port++;
  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%d", port);

  char *argv[] = {"ascii-chat-client",
                  "--port",
                  port_str,
                  "--address",
                  "127.0.0.1",
                  "--snapshot", // Exit after one frame
                  NULL};

  pid_t client_pid = spawn_process("./build/bin/ascii-chat-client", argv, "client_no_server");
  cr_assert_gt(client_pid, 0, "Client should spawn");

  int exit_code;
  bool exited = wait_for_process_exit(client_pid, 3000, &exit_code);
  cr_assert(exited, "Client should exit when no server available");
  cr_assert_neq(exit_code, 0, "Client should exit with error when no server");
}

// =============================================================================
// Combined Server-Client Tests
// =============================================================================

Test(main_integration, server_client_basic_connection) {
  int port = next_test_port++;
  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%d", port);

  // Start server
  char *server_argv[] = {"ascii-chat-server", "--port", port_str, "--log-file", "/tmp/test_server_client.log", NULL};

  pid_t server_pid = spawn_process("./build/bin/ascii-chat-server", server_argv, "server");
  cr_assert_gt(server_pid, 0, "Server should spawn");

  // Wait for server to be ready
  bool server_ready = wait_for_tcp_port(port, 2000);
  cr_assert(server_ready, "Server should be listening");

  // Start client
  char *client_argv[] = {"ascii-chat-client",
                         "--port",
                         port_str,
                         "--address",
                         "127.0.0.1",
                         "--snapshot",
                         "--snapshot-delay",
                         "1", // Run for 1 second
                         "--log-file",
                         "/tmp/test_client.log",
                         NULL};

  pid_t client_pid = spawn_process("./build/bin/ascii-chat-client", client_argv, "client");
  cr_assert_gt(client_pid, 0, "Client should spawn");

  // Wait for client to complete
  int client_exit_code;
  bool client_exited = wait_for_process_exit(client_pid, 5000, &client_exit_code);
  cr_assert(client_exited, "Client should complete snapshot");
  cr_assert_eq(client_exit_code, 0, "Client should exit successfully");

  // Clean up server
  terminate_process(server_pid, "server");
}

Test(main_integration, server_multiple_clients_sequential) {
  int port = next_test_port++;
  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%d", port);

  // Start server
  char *server_argv[] = {"ascii-chat-server", "--port", port_str, "--log-file", "/tmp/test_multi_seq.log", NULL};

  pid_t server_pid = spawn_process("./build/bin/ascii-chat-server", server_argv, "server");
  cr_assert_gt(server_pid, 0, "Server should spawn");

  bool server_ready = wait_for_tcp_port(port, 2000);
  cr_assert(server_ready, "Server should be listening");

  // Connect multiple clients sequentially
  for (int i = 0; i < 3; i++) {
    char client_name[32];
    snprintf(client_name, sizeof(client_name), "client_%d", i);

    char *client_argv[] = {
        "ascii-chat-client",        "--port", port_str, "--address", "127.0.0.1", "--snapshot", "--log-file",
        "/tmp/test_client_seq.log", NULL};

    pid_t client_pid = spawn_process("./build/bin/ascii-chat-client", client_argv, client_name);
    cr_assert_gt(client_pid, 0, "Client %d should spawn", i);

    int exit_code;
    bool exited = wait_for_process_exit(client_pid, 3000, &exit_code);
    cr_assert(exited, "Client %d should complete", i);
    cr_assert_eq(exit_code, 0, "Client %d should exit successfully", i);
  }

  terminate_process(server_pid, "server");
}

Test(main_integration, server_multiple_clients_concurrent) {
  int port = next_test_port++;
  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%d", port);

  // Start server
  char *server_argv[] = {"ascii-chat-server", "--port", port_str, "--log-file", "/tmp/test_multi_concurrent.log", NULL};

  pid_t server_pid = spawn_process("./build/bin/ascii-chat-server", server_argv, "server");
  cr_assert_gt(server_pid, 0, "Server should spawn");

  bool server_ready = wait_for_tcp_port(port, 2000);
  cr_assert(server_ready, "Server should be listening");

  // Start multiple clients concurrently
  pid_t client_pids[3];
  for (int i = 0; i < 3; i++) {
    char client_name[32];
    snprintf(client_name, sizeof(client_name), "client_%d", i);

    char delay_str[16];
    snprintf(delay_str, sizeof(delay_str), "%d", 2 + i); // Different durations

    char *client_argv[] = {"ascii-chat-client",
                           "--port",
                           port_str,
                           "--address",
                           "127.0.0.1",
                           "--snapshot",
                           "--snapshot-delay",
                           delay_str,
                           "--log-file",
                           "/tmp/test_client_concurrent.log",
                           NULL};

    client_pids[i] = spawn_process("./build/bin/ascii-chat-client", client_argv, client_name);
    cr_assert_gt(client_pids[i], 0, "Client %d should spawn", i);
    usleep(100000); // 100ms between client starts
  }

  // Wait for all clients to complete
  for (int i = 0; i < 3; i++) {
    int exit_code;
    bool exited = wait_for_process_exit(client_pids[i], 10000, &exit_code);
    cr_assert(exited, "Client %d should complete", i);
    cr_assert_eq(exit_code, 0, "Client %d should exit successfully", i);
  }

  terminate_process(server_pid, "server");
}

Test(main_integration, server_client_with_options) {
  int port = next_test_port++;
  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%d", port);

  // Start server with options
  char *server_argv[] = {"ascii-chat-server",
                         "--port",
                         port_str,
                         "--color",
                         "--audio",
                         "--log-file",
                         "/tmp/test_server_options.log",
                         NULL};

  pid_t server_pid = spawn_process("./build/bin/ascii-chat-server", server_argv, "server");
  cr_assert_gt(server_pid, 0, "Server should spawn with options");

  bool server_ready = wait_for_tcp_port(port, 2000);
  cr_assert(server_ready, "Server should be listening");

  // Start client with matching options
  char *client_argv[] = {"ascii-chat-client",
                         "--port",
                         port_str,
                         "--address",
                         "127.0.0.1",
                         "--color",
                         "--audio",
                         "--width",
                         "80",
                         "--height",
                         "24",
                         "--snapshot",
                         "--snapshot-delay",
                         "2",
                         "--log-file",
                         "/tmp/test_client_options.log",
                         NULL};

  pid_t client_pid = spawn_process("./build/bin/ascii-chat-client", client_argv, "client");
  cr_assert_gt(client_pid, 0, "Client should spawn with options");

  int client_exit_code;
  bool client_exited = wait_for_process_exit(client_pid, 5000, &client_exit_code);
  cr_assert(client_exited, "Client should complete");
  cr_assert_eq(client_exit_code, 0, "Client should exit successfully with options");

  terminate_process(server_pid, "server");
}

Test(main_integration, server_survives_client_crash) {
  int port = next_test_port++;
  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%d", port);

  // Start server
  char *server_argv[] = {"ascii-chat-server", "--port", port_str, "--log-file", "/tmp/test_server_survives.log", NULL};

  pid_t server_pid = spawn_process("./build/bin/ascii-chat-server", server_argv, "server");
  cr_assert_gt(server_pid, 0, "Server should spawn");

  bool server_ready = wait_for_tcp_port(port, 2000);
  cr_assert(server_ready, "Server should be listening");

  // Start client
  char *client_argv[] = {"ascii-chat-client",          "--port", port_str, "--address", "127.0.0.1", "--log-file",
                         "/tmp/test_client_crash.log", NULL};

  pid_t client_pid = spawn_process("./build/bin/ascii-chat-client", client_argv, "client");
  cr_assert_gt(client_pid, 0, "Client should spawn");

  usleep(500000); // Let client connect

  // Kill client abruptly
  kill(client_pid, SIGKILL);
  waitpid(client_pid, NULL, 0);

  // Server should still be running
  int status;
  pid_t result = waitpid(server_pid, &status, WNOHANG);
  cr_assert_eq(result, 0, "Server should survive client crash");

  // Try connecting another client to verify server is still functional
  char *client2_argv[] = {"ascii-chat-client",
                          "--port",
                          port_str,
                          "--address",
                          "127.0.0.1",
                          "--snapshot",
                          "--log-file",
                          "/tmp/test_client_after_crash.log",
                          NULL};

  pid_t client2_pid = spawn_process("./build/bin/ascii-chat-client", client2_argv, "client2");
  cr_assert_gt(client2_pid, 0, "Second client should spawn");

  int exit_code;
  bool exited = wait_for_process_exit(client2_pid, 3000, &exit_code);
  cr_assert(exited, "Second client should complete");
  cr_assert_eq(exit_code, 0, "Second client should connect successfully");

  terminate_process(server_pid, "server");
}
