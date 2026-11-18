#include <catch2/catch_test_macros.hpp>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/common.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/log/log.h>
#include <libplatform/compiler.h>
#include <libplatform/process.h>
#include <libplatform/system.h>
#include <libplatform/terminal.h>

// Test configuration
#define TEST_PORT_BASE 10000
#define SERVER_STARTUP_DELAY_MS 2000
// Debug builds with AddressSanitizer are 5-10x slower, so we need generous timeouts
#define CLIENT_CONNECT_TIMEOUT_MS 20000
#define PROCESS_CLEANUP_TIMEOUT_MS 2000
#define MAX_PROCESSES 10

// Process management
typedef struct {
  platform_process_t *process;
  const char *name;
  int exit_code;
  bool running;
} process_info_t;

static process_info_t tracked_processes[MAX_PROCESSES];
static int process_count = 0;

// Port allocation using PID to avoid collisions when Catch2 runs tests in parallel.
// Each forked test process gets a unique port range based on its PID.
// Range: TEST_PORT_BASE to TEST_PORT_BASE + 50000 (ports 10000-60000)
static int get_unique_test_port(void) {
  static int port_offset = 0;
  // Use PID to create unique port base per process (each process gets 10 ports)
  static int process_offset = 0;
  int pid_offset = (process_offset++ % 5000) * 10;
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
  platform_setenv("ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK", "1");
  platform_setenv("ASCII_CHAT_AUDIO", "false");
  platform_setenv("ASCII_CHAT_NO_CHECK_UPDATE", "true");
}

void teardown_main_tests(void) {
  // Kill any remaining processes
  for (int i = 0; i < process_count; i++) {
    if (tracked_processes[i].running && tracked_processes[i].process) {
      platform_process_kill(tracked_processes[i].process);
      platform_sleep_ms(100);
      platform_process_wait(tracked_processes[i].process, -1, NULL);
      platform_process_destroy(tracked_processes[i].process);
      tracked_processes[i].process = NULL;
    }
  }
  log_set_level(original_log_level);
  // Clean up test environment
}
// =============================================================================
// Process Management Utilities
// =============================================================================

// Use shared binary path detection from tests/common.h
#define get_binary_path test_get_binary_path

<<<<<<< HEAD:tests/integration/session/main_integration_test.c
static pid_t spawn_process(const char *path, char *const argv[], const char *name) {
  pid_t pid = fork();
  if (pid == 0) {
    // Child: redirect output to log file
    char log_path[256];
    safe_snprintf(log_path, sizeof(log_path), "/tmp/ascii_chat_test_%s_%d.log", name, getpid());

    FILE *log_file = fopen(log_path, "w");
    if (log_file) {
      (void)platform_dup2(fileno(log_file), STDOUT_FILENO);
      (void)platform_dup2(fileno(log_file), STDERR_FILENO);
      fclose(log_file);
    }

    execv(path, argv);
    fprintf(stderr, "Failed to exec %s: %s\n", path, strerror(errno));
    exit(127);
  }

  if (pid > 0 && process_count < MAX_PROCESSES) {
    tracked_processes[process_count].pid = pid;
=======
static platform_process_t *spawn_process(const char *path, char *const argv[], const char *name) {
  platform_process_t *process = NULL;
  if (platform_process_spawn(&process, path, (const char *const *)argv, -1, -1, -1) != ASCIICHAT_OK)
    return NULL;
  if (process_count < MAX_PROCESSES) {
    tracked_processes[process_count].process = process;
>>>>>>> origin/master:tests/integration/session/main_integration_test.cpp
    tracked_processes[process_count].name = name;
    tracked_processes[process_count].running = true;
    process_count++;
  }

  return process;
}

static bool wait_for_process_exit(platform_process_t *process, int timeout_ms, int *exit_code) {
  return process && platform_process_wait(process, timeout_ms, exit_code) == ASCIICHAT_OK;
}

static void terminate_process(platform_process_t *process, const char *name) {
  UNUSED(name);
  if (!process)
    return;

  int exit_code = -1;
  if (!wait_for_process_exit(process, PROCESS_CLEANUP_TIMEOUT_MS, &exit_code)) {
    platform_process_kill(process);
    platform_process_wait(process, -1, &exit_code);
  }

  // Mark as not running
  for (int i = 0; i < process_count; i++) {
    if (tracked_processes[i].process == process) {
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
      platform_sleep_ms(SERVER_STARTUP_DELAY_MS);
      return true;
    }

    platform_sleep_ms(poll_interval_ms);
    elapsed_ms += poll_interval_ms;
  }

  return false;
}

// =============================================================================
// Server Main Function Tests
// =============================================================================
// Use verbose logging with debug level enabled and stdout/stderr not disabled

TEST_CASE("main_integration/server_main_starts_and_stops", "[main_integration] [integration]") {
  int port = get_unique_test_port();
  int websocket_port = get_unique_test_port();
  char port_str[16];
  char websocket_port_str[16];
  safe_snprintf(port_str, sizeof(port_str), "%d", port);
  safe_snprintf(websocket_port_str, sizeof(websocket_port_str), "%d", websocket_port);

  char *argv[] = {"ascii-chat", "--log-file", "/tmp/test_server_main.log", "server", "--port", port_str, "--websocket-port", websocket_port_str, "--status-screen=false", NULL};

  platform_process_t *server_pid = spawn_process(get_binary_path(), argv, "server");
  REQUIRE(server_pid != NULL);

  // Wait for server to start listening
  bool started = wait_for_tcp_port(port, 2000);
  REQUIRE(started);

  // Verify server is still running
  REQUIRE(platform_process_is_alive(server_pid));

  // Graceful shutdown
  terminate_process(server_pid, "server");
}

TEST_CASE("main_integration/server_main_help_flag", "[main_integration] [integration]") {
  char *argv[] = {"ascii-chat", "server", "--help", NULL};

  platform_process_t *server_pid = spawn_process(get_binary_path(), argv, "server_help");
  REQUIRE(server_pid != NULL);

  int exit_code;
  bool exited = wait_for_process_exit(server_pid, 1000, &exit_code);
  REQUIRE(exited);
  REQUIRE((exit_code) == (0));
}

TEST_CASE("main_integration/server_main_invalid_port", "[main_integration] [integration]") {
  char *argv[] = {"ascii-chat", "server", "--port", "99999", // Invalid port
                  NULL};

  platform_process_t *server_pid = spawn_process(get_binary_path(), argv, "server_bad_port");
  REQUIRE(server_pid != NULL);

  int exit_code;
  bool exited = wait_for_process_exit(server_pid, 2000, &exit_code);
  REQUIRE(exited);
  REQUIRE((exit_code) != (0));
}

// =============================================================================
// Client Main Function Tests
// =============================================================================

TEST_CASE("main_integration/client_main_help_flag", "[main_integration] [integration]") {
  char *argv[] = {"ascii-chat", "client", "--help", NULL};

  platform_process_t *client_pid = spawn_process(get_binary_path(), argv, "client_help");
  REQUIRE(client_pid != NULL);

  int exit_code;
  bool exited = wait_for_process_exit(client_pid, 1000, &exit_code);
  REQUIRE(exited);
  REQUIRE((exit_code) == (0));
}

TEST_CASE("main_integration/client_main_no_server", "[main_integration] [integration]") {
  // Client is designed to retry connecting forever - verify it stays alive
  int port = get_unique_test_port();
  char port_str[16];
  safe_snprintf(port_str, sizeof(port_str), "%d", port);

  char *argv[] = {"ascii-chat", "--no-check-update", "client", "127.0.0.1:27224", "--test-pattern", NULL};

  platform_process_t *client_pid = spawn_process(get_binary_path(), argv, "client_no_server");
  REQUIRE(client_pid != NULL);

  // Wait 200ms - client should still be running (retrying connection)
  platform_sleep_ms(200);

  // Verify client is still alive (waitpid with WNOHANG returns 0 if still running)
  REQUIRE(platform_process_is_alive(client_pid));

  // Clean up - terminate the client
  terminate_process(client_pid, "client_no_server");
}

// =============================================================================
// Combined Server-Client Tests
// =============================================================================

TEST_CASE("main_integration/server_client_basic_connection", "[main_integration] [integration]") {
  int port = get_unique_test_port();
  int websocket_port = get_unique_test_port();
  char port_str[16];
  char websocket_port_str[16];
  safe_snprintf(port_str, sizeof(port_str), "%d", port);
  safe_snprintf(websocket_port_str, sizeof(websocket_port_str), "%d", websocket_port);
  char client_address[64];
  safe_snprintf(client_address, sizeof(client_address), "127.0.0.1:%s", port_str);

  // Start server (no encryption for speed)
  char *server_argv[] = {"ascii-chat",       "--log-file",   "/tmp/test_server_client.log", "server", "--port", port_str, "--websocket-port",
                         websocket_port_str, "--no-encrypt", "--status-screen=false",       NULL};

  platform_process_t *server_pid = spawn_process(get_binary_path(), server_argv, "server");
  REQUIRE(server_pid != NULL);

  // Wait for server to be ready
  bool server_ready = wait_for_tcp_port(port, 2000);
  REQUIRE(server_ready);

  // Start client with test pattern (no webcam needed in Docker)
  char *client_argv[] = {"ascii-chat",
                         "--log-file",
                         "/tmp/test_client.log",
                         "--log-level",
                         "debug",
                         "--no-check-update",
                         "client",
                         client_address,
                         "--no-encrypt",   // Skip crypto handshake for speed
                         "--test-pattern", // Use test pattern instead of webcam
                         "--snapshot",     // Take single snapshot and exit immediately
                         "--snapshot-delay",
                         "3",
                         NULL};

  platform_process_t *client_pid = spawn_process(get_binary_path(), client_argv, "client");
  REQUIRE(client_pid != NULL);

  // Wait for client to complete
  int client_exit_code;
  bool client_exited = wait_for_process_exit(client_pid, CLIENT_CONNECT_TIMEOUT_MS, &client_exit_code);
  REQUIRE(client_exited);

  // Clean up server
  terminate_process(server_pid, "server");
}

TEST_CASE("main_integration/server_multiple_clients_sequential", "[main_integration] [integration]") {
  int port = get_unique_test_port();
  int websocket_port = get_unique_test_port();
  char port_str[16];
  char websocket_port_str[16];
  safe_snprintf(port_str, sizeof(port_str), "%d", port);
  safe_snprintf(websocket_port_str, sizeof(websocket_port_str), "%d", websocket_port);
  char client_address[64];
  safe_snprintf(client_address, sizeof(client_address), "127.0.0.1:%s", port_str);

  // Start server (no encryption for speed)
  char *server_argv[] = {"ascii-chat", "--log-file", "/tmp/test_multi_seq.log", "server", "--port", port_str, "--websocket-port", websocket_port_str, "--no-encrypt", "--status-screen=false", NULL};

  platform_process_t *server_pid = spawn_process(get_binary_path(), server_argv, "server");
  REQUIRE(server_pid != NULL);

  bool server_ready = wait_for_tcp_port(port, 2000);
  REQUIRE(server_ready);

  // Connect multiple clients sequentially with test pattern (no webcam needed)
  for (int i = 0; i < 2; i++) {
    char client_name[32];
    char client_log_path[64];
    safe_snprintf(client_name, sizeof(client_name), "client_%d", i);
    safe_snprintf(client_log_path, sizeof(client_log_path), "/tmp/test_client_seq_%d.log", i);

    char *client_argv[] = {"ascii-chat", "--log-file", client_log_path, "--no-check-update", "client", client_address, "--no-encrypt", "--test-pattern", "--snapshot", "--snapshot-delay", "0", NULL};

    platform_process_t *client_pid = spawn_process(get_binary_path(), client_argv, client_name);
    REQUIRE(client_pid != NULL);

    int exit_code;
    bool exited = wait_for_process_exit(client_pid, CLIENT_CONNECT_TIMEOUT_MS, &exit_code);
    REQUIRE(exited);
    REQUIRE(exited);
    // Let the server finish removing the just-closed client before reusing it.
    platform_sleep_ms(5000);
  }

  terminate_process(server_pid, "server");
}

TEST_CASE("main_integration/server_multiple_clients_concurrent", "[main_integration] [integration]") {
  int port = get_unique_test_port();
  int websocket_port = get_unique_test_port();
  char port_str[16];
  char websocket_port_str[16];
  safe_snprintf(port_str, sizeof(port_str), "%d", port);
  safe_snprintf(websocket_port_str, sizeof(websocket_port_str), "%d", websocket_port);
  char client_address[64];
  safe_snprintf(client_address, sizeof(client_address), "127.0.0.1:%s", port_str);

  // Start server (no encryption for speed)
  char *server_argv[] = {"ascii-chat",       "--log-file",   "/tmp/test_multi_concurrent.log", "server", "--port", port_str, "--websocket-port",
                         websocket_port_str, "--no-encrypt", "--status-screen=false",          NULL};

  platform_process_t *server_pid = spawn_process(get_binary_path(), server_argv, "server");
  REQUIRE(server_pid != NULL);

  bool server_ready = wait_for_tcp_port(port, 2000);
  REQUIRE(server_ready);

  // Start multiple clients concurrently with test pattern (no webcam needed)
  platform_process_t *client_pids[2];
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
    REQUIRE(client_pids[i] != NULL);
    platform_sleep_ms(250); // Allow the server to finish accepting each client.
  }

  // Wait for all clients to complete
  for (int i = 0; i < 2; i++) {
    int exit_code;
    bool exited = wait_for_process_exit(client_pids[i], CLIENT_CONNECT_TIMEOUT_MS, &exit_code);
    REQUIRE(exited);
    REQUIRE(exited);
  }

  terminate_process(server_pid, "server");
}

TEST_CASE("main_integration/server_client_with_options", "[main_integration] [integration]") {
  int port = get_unique_test_port();
  int websocket_port = get_unique_test_port();
  char port_str[16];
  char websocket_port_str[16];
  safe_snprintf(port_str, sizeof(port_str), "%d", port);
  safe_snprintf(websocket_port_str, sizeof(websocket_port_str), "%d", websocket_port);
  char client_address[64];
  safe_snprintf(client_address, sizeof(client_address), "127.0.0.1:%s", port_str);

  // Start server with standard options (no encryption for speed)
  char *server_argv[] = {"ascii-chat",       "--log-file",   "/tmp/test_server_options.log", "server", "--port", port_str, "--websocket-port",
                         websocket_port_str, "--no-encrypt", "--status-screen=false",        NULL};

  platform_process_t *server_pid = spawn_process(get_binary_path(), server_argv, "server");
  REQUIRE(server_pid != NULL);

  bool server_ready = wait_for_tcp_port(port, 2000);
  REQUIRE(server_ready);

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

  platform_process_t *client_pid = spawn_process(get_binary_path(), client_argv, "client");
  REQUIRE(client_pid != NULL);

  int client_exit_code;
  bool client_exited = wait_for_process_exit(client_pid, CLIENT_CONNECT_TIMEOUT_MS, &client_exit_code);
  REQUIRE(client_exited);
  REQUIRE(client_exited);

  terminate_process(server_pid, "server");
}

TEST_CASE("main_integration/server_survives_client_crash", "[main_integration] [integration]") {
  int port = get_unique_test_port();
  int websocket_port = get_unique_test_port();
  char port_str[16];
  char websocket_port_str[16];
  safe_snprintf(port_str, sizeof(port_str), "%d", port);
  safe_snprintf(websocket_port_str, sizeof(websocket_port_str), "%d", websocket_port);
  char client_address[64];
  safe_snprintf(client_address, sizeof(client_address), "127.0.0.1:%s", port_str);

  // Start server (no encryption for speed)
  char *server_argv[] = {"ascii-chat",       "--log-file",   "/tmp/test_server_survives.log", "server", "--port", port_str, "--websocket-port",
                         websocket_port_str, "--no-encrypt", "--status-screen=false",         NULL};

  platform_process_t *server_pid = spawn_process(get_binary_path(), server_argv, "server");
  REQUIRE(server_pid != NULL);

  bool server_ready = wait_for_tcp_port(port, 2000);
  REQUIRE(server_ready);

  // Start client with test pattern (no webcam needed)
  char *client_argv[] = {"ascii-chat",     "--log-file", "/tmp/test_client_crash.log", "client", client_address,
                         "--no-encrypt", // Skip crypto handshake for speed
                         "--test-pattern", NULL};

  platform_process_t *client_pid = spawn_process(get_binary_path(), client_argv, "client");
  REQUIRE(client_pid != NULL);

  platform_sleep_ms(100); // 100ms - Let client connect (fast with --no-encrypt)

  // Kill client abruptly
  platform_process_kill(client_pid);
  platform_process_wait(client_pid, -1, NULL);
  // Let the server observe the abrupt disconnect and reclaim the client slot.
  platform_sleep_ms(5000);

  // Server should still be running
  REQUIRE(platform_process_is_alive(server_pid));

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

  platform_process_t *client2_pid = spawn_process(get_binary_path(), client2_argv, "client2");
  REQUIRE(client2_pid != NULL);

  int exit_code;
  bool exited = wait_for_process_exit(client2_pid, CLIENT_CONNECT_TIMEOUT_MS, &exit_code);
  REQUIRE(exited);
  REQUIRE(exited);

  terminate_process(server_pid, "server");
}
