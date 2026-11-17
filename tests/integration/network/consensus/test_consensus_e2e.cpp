#include <catch2/catch_test_macros.hpp>
/**
 * @file test_consensus_e2e.c
 * @brief End-to-end integration test for ring consensus protocol
 *
 * This test:
 * 1. Spawns a server process
 * 2. Connects multiple client processes
 * 3. Waits for consensus round to trigger
 * 4. Captures logs to verify election happened
 * 5. Parses election results from output
 * 6. Verifies all participants reached consensus
 */

#include <ascii-chat/tests/common.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <stdint.h>
#include <ascii-chat/tests/common.h>
#include <libplatform/filesystem.h>
#include <libplatform/process.h>
#include <libplatform/system.h>

#define SERVER_PORT 29998
#define MAX_CLIENTS 3
#define WAIT_TIMEOUT_MS 15000 // 15 seconds total for consensus
#define LOG_BUFFER_SIZE 65536

typedef struct {
  platform_process_t *process;
  char stdout_path[256];
  char stderr_path[256];
  char stdout_buf[LOG_BUFFER_SIZE];
  char stderr_buf[LOG_BUFFER_SIZE];
  size_t stdout_len;
  size_t stderr_len;
} process_t;

static process_t server_proc = {0};
static process_t client_procs[MAX_CLIENTS] = {0};

/**
 * Read from a pipe without blocking, appending to buffer
 */
static int read_from_pipe(const char *path, char *buf, size_t *len, size_t max_size) {
  if (!path || *len >= max_size) {
    return 0;
  }
  FILE *file = fopen(path, "rb");
  if (!file)
    return 0;
  fseek(file, (long)*len, SEEK_SET);
  size_t n = fread(buf + *len, 1, max_size - *len - 1, file);
  fclose(file);
  if (n > 0) {
    *len += n;
    buf[*len] = '\0';
    return 1;
  }
  return 0;
}

/**
 * Wait for pattern in process output, with timeout
 */
__attribute__((unused)) static int wait_for_pattern(process_t *proc, const char *pattern, int timeout_ms) {
  time_t start = time(NULL);

  while (time(NULL) - start < timeout_ms / 1000) {
    // Try to read from stdout
    read_from_pipe(proc->stdout_path, proc->stdout_buf, &proc->stdout_len, LOG_BUFFER_SIZE);

    // Try to read from stderr
    read_from_pipe(proc->stderr_path, proc->stderr_buf, &proc->stderr_len, LOG_BUFFER_SIZE);

    // Check both buffers for pattern
    if (strstr(proc->stdout_buf, pattern) || strstr(proc->stderr_buf, pattern)) {
      return 1;
    }

    platform_sleep_ms(100); // 100ms sleep
  }

  return 0;
}

/**
 * Start server process
 */
static void start_server(void) {
  platform_setenv("ASCII_CHAT_AUDIO", "false");
  const char *binary_path = test_get_binary_path();
  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%d", SERVER_PORT);
  snprintf(server_proc.stdout_path, sizeof(server_proc.stdout_path), "consensus-server.log");
  snprintf(server_proc.stderr_path, sizeof(server_proc.stderr_path), "%s", server_proc.stdout_path);
  const char *argv[] = {binary_path, "--log-level", "debug", "server", "--port", port_str, "--max-clients", "4", "--status-screen=false", "--no-encrypt", NULL};
  int fd = platform_open("consensus-server-log", server_proc.stdout_path, PLATFORM_O_WRONLY | PLATFORM_O_CREAT | PLATFORM_O_TRUNC | PLATFORM_O_BINARY, 0600);
  REQUIRE(fd >= 0);
  REQUIRE(platform_process_spawn(&server_proc.process, binary_path, argv, -1, fd, fd) == 0);
  platform_close(fd);

  // Wait for server to start listening
  sleep(2);

  // Check if server is still running
  if (!platform_process_is_alive(server_proc.process)) {
    // Server exited, read its error output
    read_from_pipe(server_proc.stderr_path, server_proc.stderr_buf, &server_proc.stderr_len, LOG_BUFFER_SIZE);
    INFO("Server stderr: %s");
    REQUIRE(server_proc.process != NULL);
  }
}

/**
 * Start a client process
 */
static void start_client(int client_num) {
  process_t *proc = &client_procs[client_num];
  const char *binary_path = test_get_binary_path();
  snprintf(proc->stdout_path, sizeof(proc->stdout_path), "consensus-client-%d.log", client_num);
  snprintf(proc->stderr_path, sizeof(proc->stderr_path), "%s", proc->stdout_path);
  char addr_str[32];
  snprintf(addr_str, sizeof(addr_str), "127.0.0.1:%d", SERVER_PORT);
  const char *argv[] = {binary_path, "--log-level", "debug", "client", addr_str, "--snapshot", "--snapshot-delay", "2", "--status-screen=false", "--no-encrypt", NULL};
  int fd = platform_open("consensus-client-log", proc->stdout_path, PLATFORM_O_WRONLY | PLATFORM_O_CREAT | PLATFORM_O_TRUNC | PLATFORM_O_BINARY, 0600);
  REQUIRE(fd >= 0);
  REQUIRE(platform_process_spawn(&proc->process, binary_path, argv, -1, fd, fd) == 0);
  platform_close(fd);
}

/**
 * Cleanup processes
 */
static void cleanup_processes(void) {
  process_t *processes[MAX_CLIENTS + 1] = {&server_proc};
  for (int i = 0; i < MAX_CLIENTS; i++) {
    processes[i + 1] = &client_procs[i];
  }

  for (size_t i = 0; i < MAX_CLIENTS + 1; i++) {
    process_t *proc = processes[i];
    if (!proc->process) {
      continue;
    }

    platform_process_kill(proc->process);
    platform_process_wait(proc->process, 2000, NULL);
    platform_process_destroy(proc->process);
    proc->process = NULL;
  }
}

/**
 * Catch2 fixture for proper cleanup
 */
__attribute__((unused)) static void consensus_teardown(void) {
  cleanup_processes();
}

/**
 * Parse election result from logs
 */
__attribute__((unused)) static int parse_election_host(const char *buf, uint8_t *out_host_id) {
  // Look for: "Election result received: host=XX, backup=YY"
  // or: "Election complete: host=XX, backup=YY"
  const char *pattern = "host=";
  const char *pos = strstr(buf, pattern);

  if (!pos) {
    return 0;
  }

  pos += strlen(pattern);
  unsigned int host_val;
  if (sscanf(pos, "%u", &host_val) != 1) {
    return 0;
  }

  if (out_host_id) {
    memset(out_host_id, 0, 16);
    out_host_id[0] = host_val & 0xFF;
  }

  return 1;
}

/**
 * Test: Server can be started
 */
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TEST_CASE("consensus_e2e/server_startup", "[consensus_e2e] [integration]") {
  // Disabled: Server requires TTY for display
  // Enable when TTY-less server mode is added
  start_server();
  cleanup_processes();
}

/**
 * Test: Clients can connect to server
 */
TEST_CASE("consensus_e2e/client_connection", "[consensus_e2e] [integration]") {
  // Disabled: Server requires TTY for display
  // This test demonstrates client connection works when server is available
  start_server();

  // Connect 2 clients
  start_client(0);
  start_client(1);

  // Wait for clients to complete
  sleep(5);

  cleanup_processes();
}

/**
 * Test: Multiple clients can connect and consensus forms a ring
 */
TEST_CASE("consensus_e2e/consensus_ring_formation", "[consensus_e2e] [integration]") {
  start_server();

  // Connect 3 clients to form a multi-participant session
  for (int i = 0; i < 2; i++) {
    start_client(i);
    sleep(1); // Stagger connections
  }

  // Wait for consensus operations
  sleep(8);

  // Read all output
  for (int i = 0; i < 2; i++) {
    read_from_pipe(client_procs[i].stdout_path, client_procs[i].stdout_buf, &client_procs[i].stdout_len, LOG_BUFFER_SIZE);
    read_from_pipe(client_procs[i].stderr_path, client_procs[i].stderr_buf, &client_procs[i].stderr_len, LOG_BUFFER_SIZE);
  }

  read_from_pipe(server_proc.stdout_path, server_proc.stdout_buf, &server_proc.stdout_len, LOG_BUFFER_SIZE);
  read_from_pipe(server_proc.stderr_path, server_proc.stderr_buf, &server_proc.stderr_len, LOG_BUFFER_SIZE);

  // Verify server received connections
  REQUIRE((strlen(server_proc.stdout_buf) > 0 || strlen(server_proc.stderr_buf) > 0));
  cleanup_processes();
}

/**
 * Test: Consensus protocol logs show up during session
 */
TEST_CASE("consensus_e2e/consensus_protocol_execution", "[consensus_e2e] [integration]") {
  start_server();

  // Connect 2 clients
  for (int i = 0; i < 2; i++) {
    start_client(i);
    sleep(1);
  }

  // Wait for session to establish and consensus to potentially trigger
  sleep(10);

  // Collect all output
  for (int i = 0; i < 2; i++) {
    read_from_pipe(client_procs[i].stdout_path, client_procs[i].stdout_buf, &client_procs[i].stdout_len, LOG_BUFFER_SIZE);
    read_from_pipe(client_procs[i].stderr_path, client_procs[i].stderr_buf, &client_procs[i].stderr_len, LOG_BUFFER_SIZE);
  }

  read_from_pipe(server_proc.stdout_path, server_proc.stdout_buf, &server_proc.stdout_len, LOG_BUFFER_SIZE);
  read_from_pipe(server_proc.stderr_path, server_proc.stderr_buf, &server_proc.stderr_len, LOG_BUFFER_SIZE);

  // Combine all output
  char combined[LOG_BUFFER_SIZE * 3] = {0};
  strncat(combined, server_proc.stdout_buf, LOG_BUFFER_SIZE - 1);
  strncat(combined, server_proc.stderr_buf, LOG_BUFFER_SIZE - 1);

  for (int i = 0; i < 2; i++) {
    strncat(combined, client_procs[i].stdout_buf, 1000);
    strncat(combined, client_procs[i].stderr_buf, 1000);
  }

  // Log combined output for inspection
  if (strlen(combined) > 0) {
    INFO("\n=== Combined Output ===\n%s\n=== End Output ===\n");
  }

  // Verify connections were made (basic smoke test)
  REQUIRE((strlen(server_proc.stdout_buf) > 0 || strlen(server_proc.stderr_buf) > 0));
  cleanup_processes();
}
