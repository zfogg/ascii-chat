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

#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/platform/abstraction.h>

// Test configuration
#define TEST_PORT_BASE 10000
#define SERVER_STARTUP_DELAY_MS 2000
// Debug builds with AddressSanitizer are 5-10x slower, so we need generous timeouts
#define CLIENT_CONNECT_TIMEOUT_MS 20000
#define PROCESS_CLEANUP_TIMEOUT_MS 2000
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
  log_set_level(LOG_FATAL); // Quiet logging for tests
  process_count = 0;
  memset(tracked_processes, 0, sizeof(tracked_processes));
  // Disable host identity check for tests since we don't have a TTY for prompts
  setenv("ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK", "1", 1);
  setenv("ASCII_CHAT_AUDIO", "false", 1);
  setenv("ASCII_CHAT_NO_CHECK_UPDATE", "true", 1);
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

static bool file_contains(const char *path, const char *needle) {
  FILE *file = fopen(path, "r");
  if (!file)
    return false;

  char line[1024];
  bool found = false;
  while (fgets(line, sizeof(line), file)) {
    if (strstr(line, needle)) {
      found = true;
      break;
    }
  }
  fclose(file);
  return found;
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
    if (result == 0) {
      // Wake the server's handshake reader before releasing the probe fd.
      // A close alone can race with accept and leave the probe occupying the
      // server's client handler until the handshake timeout expires.
      (void)socket_shutdown(sock, SHUT_RDWR);
    }
    socket_close(sock);

    if (result == 0) {
      // The probe is an actual accepted TCP connection. Give the server's
      // handler a moment to observe its close and release the temporary
      // client slot before the test starts the real client.
      usleep(SERVER_STARTUP_DELAY_MS * 1000);
      return true;
    }

    usleep(poll_interval_ms * 1000);
    elapsed_ms += poll_interval_ms;
  }

  return false;
}

// =============================================================================
// Server Main Function Tests
// =============================================================================
// Use verbose logging with debug level enabled and stdout/stderr not disabled

Test(main_integration, server_main_starts_and_stops) {
  int port = get_unique_test_port();
  int websocket_port = get_unique_test_port();
  char port_str[16];
  char websocket_port_str[16];
  safe_snprintf(port_str, sizeof(port_str), "%d", port);
  safe_snprintf(websocket_port_str, sizeof(websocket_port_str), "%d", websocket_port);

  char *argv[] = {"ascii-chat",       "--log-file",       "/tmp/test_server_main.log", "server", "--port", port_str,
                  "--websocket-port", websocket_port_str, "--status-screen=false",     NULL};

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

  char *argv[] = {"ascii-chat", "--no-check-update", "client", "127.0.0.1:27224", "--test-pattern", NULL};

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
  int websocket_port = get_unique_test_port();
  char port_str[16];
  char websocket_port_str[16];
  safe_snprintf(port_str, sizeof(port_str), "%d", port);
  safe_snprintf(websocket_port_str, sizeof(websocket_port_str), "%d", websocket_port);
  char client_address[64];
  safe_snprintf(client_address, sizeof(client_address), "127.0.0.1:%s", port_str);

  // Start server (no encryption for speed)
  char *server_argv[] = {"ascii-chat",
                         "--log-file",
                         "/tmp/test_server_client.log",
                         "server",
                         "--port",
                         port_str,
                         "--websocket-port",
                         websocket_port_str,
                         "--no-encrypt",
                         "--status-screen=false",
                         NULL};

  pid_t server_pid = spawn_process(get_binary_path(), server_argv, "server");
  cr_assert_gt(server_pid, 0, "Server should spawn");

  // Wait for server to be ready
  bool server_ready = wait_for_tcp_port(port, 2000);
  cr_assert(server_ready, "Server should be listening");

  // Start client with test pattern (no webcam needed in Docker)
  char *client_argv[] = {"ascii-chat",        "--log-file", "/tmp/test_client.log",
                         "--no-check-update", "client",     client_address,
                         "--no-encrypt",   // Skip crypto handshake for speed
                         "--test-pattern", // Use test pattern instead of webcam
                         "--snapshot",     // Take single snapshot and exit immediately
                         "--snapshot-delay",  "3",          NULL};

  pid_t client_pid = spawn_process(get_binary_path(), client_argv, "client");
  cr_assert_gt(client_pid, 0, "Client should spawn");

  // Wait for client to complete
  int client_exit_code;
  bool client_exited = wait_for_process_exit(client_pid, CLIENT_CONNECT_TIMEOUT_MS, &client_exit_code);
  cr_assert(client_exited, "Client should complete snapshot");
  cr_assert(file_contains("/tmp/test_client.log", "DISPLAY_RENDER_RETURNED"),
            "Client should render at least one frame (exit code %d)", client_exit_code);

  // Clean up server
  terminate_process(server_pid, "server");
}

Test(main_integration, server_multiple_clients_sequential) {
  int port = get_unique_test_port();
  int websocket_port = get_unique_test_port();
  char port_str[16];
  char websocket_port_str[16];
  safe_snprintf(port_str, sizeof(port_str), "%d", port);
  safe_snprintf(websocket_port_str, sizeof(websocket_port_str), "%d", websocket_port);
  char client_address[64];
  safe_snprintf(client_address, sizeof(client_address), "127.0.0.1:%s", port_str);

  // Start server (no encryption for speed)
  char *server_argv[] = {
      "ascii-chat",       "--log-file",   "/tmp/test_multi_seq.log", "server", "--port", port_str, "--websocket-port",
      websocket_port_str, "--no-encrypt", "--status-screen=false",   NULL};

  pid_t server_pid = spawn_process(get_binary_path(), server_argv, "server");
  cr_assert_gt(server_pid, 0, "Server should spawn");

  bool server_ready = wait_for_tcp_port(port, 2000);
  cr_assert(server_ready, "Server should be listening");

  // Connect multiple clients sequentially with test pattern (no webcam needed)
  for (int i = 0; i < 2; i++) {
    char client_name[32];
    char client_log_path[64];
    safe_snprintf(client_name, sizeof(client_name), "client_%d", i);
    safe_snprintf(client_log_path, sizeof(client_log_path), "/tmp/test_client_seq_%d.log", i);

    char *client_argv[] = {
        "ascii-chat",   "--log-file",     client_log_path, "--no-check-update", "client", client_address,
        "--no-encrypt", "--test-pattern", "--snapshot",    "--snapshot-delay",  "0",      NULL};

    pid_t client_pid = spawn_process(get_binary_path(), client_argv, client_name);
    cr_assert_gt(client_pid, 0, "Client %d should spawn", i);

    int exit_code;
    bool exited = wait_for_process_exit(client_pid, CLIENT_CONNECT_TIMEOUT_MS, &exit_code);
    cr_assert(exited, "Client %d should complete", i);
    cr_assert(exited, "Client %d should complete (exit code %d)", i, exit_code);
    // Let the server finish removing the just-closed client before reusing it.
    usleep(5000000);
  }

  terminate_process(server_pid, "server");
}

Test(main_integration, server_multiple_clients_concurrent) {
  int port = get_unique_test_port();
  int websocket_port = get_unique_test_port();
  char port_str[16];
  char websocket_port_str[16];
  safe_snprintf(port_str, sizeof(port_str), "%d", port);
  safe_snprintf(websocket_port_str, sizeof(websocket_port_str), "%d", websocket_port);
  char client_address[64];
  safe_snprintf(client_address, sizeof(client_address), "127.0.0.1:%s", port_str);

  // Start server (no encryption for speed)
  char *server_argv[] = {"ascii-chat",
                         "--log-file",
                         "/tmp/test_multi_concurrent.log",
                         "server",
                         "--port",
                         port_str,
                         "--websocket-port",
                         websocket_port_str,
                         "--no-encrypt",
                         "--status-screen=false",
                         NULL};

  pid_t server_pid = spawn_process(get_binary_path(), server_argv, "server");
  cr_assert_gt(server_pid, 0, "Server should spawn");

  bool server_ready = wait_for_tcp_port(port, 2000);
  cr_assert(server_ready, "Server should be listening");

  // Start multiple clients concurrently with test pattern (no webcam needed)
  pid_t client_pids[2];
  for (int i = 0; i < 2; i++) {
    char client_name[32];
    char client_log_path[64];
    safe_snprintf(client_name, sizeof(client_name), "client_%d", i);
    safe_snprintf(client_log_path, sizeof(client_log_path), "/tmp/test_client_concurrent_%d.log", i);

    char *client_argv[] = {"ascii-chat",        "--log-file", client_log_path,
                           "--no-check-update", "client",     client_address,
                           "--no-encrypt",   // Skip crypto handshake for speed
                           "--test-pattern", // Use test pattern instead of webcam
                           "--snapshot",     // Take single snapshot and exit
                           "--snapshot-delay",  "3",          NULL};

    client_pids[i] = spawn_process(get_binary_path(), client_argv, client_name);
    cr_assert_gt(client_pids[i], 0, "Client %d should spawn", i);
    usleep(250000); // Allow the server to finish accepting each client.
  }

  // Wait for all clients to complete
  for (int i = 0; i < 2; i++) {
    int exit_code;
    bool exited = wait_for_process_exit(client_pids[i], CLIENT_CONNECT_TIMEOUT_MS, &exit_code);
    cr_assert(exited, "Client %d should complete", i);
    cr_assert(exited, "Client %d should complete (exit code %d)", i, exit_code);
  }

  terminate_process(server_pid, "server");
}

Test(main_integration, server_client_with_options) {
  int port = get_unique_test_port();
  int websocket_port = get_unique_test_port();
  char port_str[16];
  char websocket_port_str[16];
  safe_snprintf(port_str, sizeof(port_str), "%d", port);
  safe_snprintf(websocket_port_str, sizeof(websocket_port_str), "%d", websocket_port);
  char client_address[64];
  safe_snprintf(client_address, sizeof(client_address), "127.0.0.1:%s", port_str);

  // Start server with standard options (no encryption for speed)
  char *server_argv[] = {"ascii-chat",
                         "--log-file",
                         "/tmp/test_server_options.log",
                         "server",
                         "--port",
                         port_str,
                         "--websocket-port",
                         websocket_port_str,
                         "--no-encrypt",
                         "--status-screen=false",
                         NULL};

  pid_t server_pid = spawn_process(get_binary_path(), server_argv, "server");
  cr_assert_gt(server_pid, 0, "Server should spawn with options");

  bool server_ready = wait_for_tcp_port(port, 2000);
  cr_assert(server_ready, "Server should be listening");

  // Start client with options (test pattern for no webcam)
  // Note: --color-mode is the correct option, not --color
  char *client_argv[] = {"ascii-chat",
                         "--no-check-update",
                         "--log-file",
                         "/tmp/test_client_options.log",
                         "client",
                         client_address,
                         "--no-encrypt",   // Skip crypto handshake for speed
                         "--test-pattern", // Use test pattern instead of webcam
                         "--width",
                         "80",
                         "--height",
                         "24",
                         "--snapshot", // Take single snapshot and exit
                         "--snapshot-delay",
                         "0",
                         NULL};

  pid_t client_pid = spawn_process(get_binary_path(), client_argv, "client");
  cr_assert_gt(client_pid, 0, "Client should spawn with options");

  int client_exit_code;
  bool client_exited = wait_for_process_exit(client_pid, CLIENT_CONNECT_TIMEOUT_MS, &client_exit_code);
  cr_assert(client_exited, "Client should complete");
  cr_assert(client_exited, "Client should complete with options (exit code %d)", client_exit_code);

  terminate_process(server_pid, "server");
}

Test(main_integration, server_survives_client_crash) {
  int port = get_unique_test_port();
  int websocket_port = get_unique_test_port();
  char port_str[16];
  char websocket_port_str[16];
  safe_snprintf(port_str, sizeof(port_str), "%d", port);
  safe_snprintf(websocket_port_str, sizeof(websocket_port_str), "%d", websocket_port);
  char client_address[64];
  safe_snprintf(client_address, sizeof(client_address), "127.0.0.1:%s", port_str);

  // Start server (no encryption for speed)
  char *server_argv[] = {"ascii-chat",
                         "--log-file",
                         "/tmp/test_server_survives.log",
                         "server",
                         "--port",
                         port_str,
                         "--websocket-port",
                         websocket_port_str,
                         "--no-encrypt",
                         "--status-screen=false",
                         NULL};

  pid_t server_pid = spawn_process(get_binary_path(), server_argv, "server");
  cr_assert_gt(server_pid, 0, "Server should spawn");

  bool server_ready = wait_for_tcp_port(port, 2000);
  cr_assert(server_ready, "Server should be listening");

  // Start client with test pattern (no webcam needed)
  char *client_argv[] = {"ascii-chat",     "--log-file", "/tmp/test_client_crash.log", "client", client_address,
                         "--no-encrypt", // Skip crypto handshake for speed
                         "--test-pattern", NULL};

  pid_t client_pid = spawn_process(get_binary_path(), client_argv, "client");
  cr_assert_gt(client_pid, 0, "Client should spawn");

  usleep(100000); // 100ms - Let client connect (fast with --no-encrypt)

  // Kill client abruptly
  kill(client_pid, SIGKILL);
  waitpid(client_pid, NULL, 0);
  // Let the server observe the abrupt disconnect and reclaim the client slot.
  usleep(5000000);

  // Server should still be running
  int status;
  pid_t result = waitpid(server_pid, &status, WNOHANG);
  cr_assert_eq(result, 0, "Server should survive client crash");

  // Try connecting another client to verify server is still functional
  char *client2_argv[] = {"ascii-chat",
                          "--no-check-update",
                          "--log-file",
                          "/tmp/test_client_after_crash.log",
                          "client",
                          client_address,
                          "--no-encrypt", // Skip crypto handshake for speed
                          "--test-pattern",
                          "--snapshot",
                          "--snapshot-delay",
                          "0",
                          NULL};

  pid_t client2_pid = spawn_process(get_binary_path(), client2_argv, "client2");
  cr_assert_gt(client2_pid, 0, "Second client should spawn");

  int exit_code;
  bool exited = wait_for_process_exit(client2_pid, CLIENT_CONNECT_TIMEOUT_MS, &exit_code);
  cr_assert(exited, "Second client should complete");
  cr_assert(exited, "Second client should complete (exit code %d)", exit_code);

  terminate_process(server_pid, "server");
}
