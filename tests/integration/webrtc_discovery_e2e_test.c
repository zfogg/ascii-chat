/**
 * @file webrtc_discovery_e2e_test.c
 * @brief End-to-end integration test for WebRTC connection via discovery service
 *
 * Tests the full WebRTC discovery flow:
 * 1. Start discovery-service (acds) with fresh database
 * 2. Start ascii-chat host to create session
 * 3. Parse session string from host logs
 * 4. Start ascii-chat guest with session string and --prefer-webrtc
 * 5. Verify WebRTC STUN connection establishment
 * 6. Clean up processes while preserving logs
 *
 * This test requires:
 * - Live STUN servers (public internet access)
 * - NAT traversal capability (most networks)
 * - No firewall blocking UDP
 */

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
#include <time.h>

#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/common.h>

// Test configuration
#define TEST_PORT_BASE 20000
#define ACDS_STARTUP_TIMEOUT_MS 2000
#define HOST_STARTUP_TIMEOUT_MS 3000
#define WEBRTC_CONNECTION_TIMEOUT_MS 15000 // WebRTC can take time with STUN
#define PROCESS_CLEANUP_TIMEOUT_MS 2000
#define MAX_PROCESSES 10
#define LOG_POLL_INTERVAL_MS 100
#define MAX_LOG_LINE_LENGTH 2048

// Process management
typedef struct {
  pid_t pid;
  const char *name;
  char log_path[256];
  int exit_code;
  bool running;
} process_info_t;

static process_info_t tracked_processes[MAX_PROCESSES];
static int process_count = 0;

// Test database paths
static char test_db_path[256];
static char test_db_wal[256];
static char test_db_shm[256];

// Port allocation to avoid collisions
// Ensures port stays within valid range (1-65535)
static int get_unique_test_port(void) {
  static int port_offset = 0;
  // Use smaller range to avoid exceeding 65535
  // Range: 20000 + (0 to 4000) = 20000-24000
  int pid_offset = (getpid() % 400) * 10;
  return TEST_PORT_BASE + pid_offset + (port_offset++ % 10);
}

// =============================================================================
// Setup and Teardown
// =============================================================================

void setup_webrtc_e2e_tests(void) {
  log_set_level(LOG_FATAL); // Quiet test framework logging
  process_count = 0;
  memset(tracked_processes, 0, sizeof(tracked_processes));

  // Generate unique database paths
  snprintf(test_db_path, sizeof(test_db_path), "/tmp/acds_webrtc_e2e_%d.db", getpid());
  snprintf(test_db_wal, sizeof(test_db_wal), "%s-wal", test_db_path);
  snprintf(test_db_shm, sizeof(test_db_shm), "%s-shm", test_db_path);

  // Clean up any leftover database files
  unlink(test_db_path);
  unlink(test_db_wal);
  unlink(test_db_shm);

  // Disable host identity check for tests
  setenv("ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK", "1", 1);
}

void teardown_webrtc_e2e_tests(void) {
  // Kill any remaining processes
  for (int i = 0; i < process_count; i++) {
    if (tracked_processes[i].running && tracked_processes[i].pid > 0) {
      kill(tracked_processes[i].pid, SIGTERM);
      usleep(100000); // 100ms grace period
      kill(tracked_processes[i].pid, SIGKILL);
      waitpid(tracked_processes[i].pid, NULL, 0);
    }
  }

  // Clean up database files (logs are intentionally preserved)
  unlink(test_db_path);
  unlink(test_db_wal);
  unlink(test_db_shm);

  log_set_level(LOG_DEBUG); // Restore logging
}

TestSuite(webrtc_discovery_e2e, .init = setup_webrtc_e2e_tests, .fini = teardown_webrtc_e2e_tests, .timeout = 45.0);

// =============================================================================
// Process Management Utilities
// =============================================================================

static pid_t spawn_process_with_log(const char *binary_path, char *const argv[], const char *name,
                                    const char *log_path) {
  pid_t pid = fork();
  if (pid == 0) {
    // Child: redirect output to log file
    FILE *log_file = fopen(log_path, "w");
    if (log_file) {
      dup2(fileno(log_file), STDOUT_FILENO);
      dup2(fileno(log_file), STDERR_FILENO);
      fclose(log_file);
    }

    execv(binary_path, argv);
    fprintf(stderr, "execv(%s) failed: %s\n", binary_path, strerror(errno));
    exit(127);
  }

  if (pid > 0 && process_count < MAX_PROCESSES) {
    tracked_processes[process_count].pid = pid;
    tracked_processes[process_count].name = name;
    SAFE_STRNCPY(tracked_processes[process_count].log_path, log_path,
                 sizeof(tracked_processes[process_count].log_path));
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

static void terminate_process(pid_t pid) {
  if (pid <= 0)
    return;

  // Try graceful termination first
  kill(pid, SIGTERM);

  int exit_code = -1;
  if (!wait_for_process_exit(pid, PROCESS_CLEANUP_TIMEOUT_MS, &exit_code)) {
    // Force kill if graceful shutdown failed
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
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

// =============================================================================
// Log Parsing Utilities
// =============================================================================

/**
 * @brief Search for a pattern in a log file
 * @param log_path Path to log file
 * @param pattern String pattern to search for
 * @param timeout_ms Maximum time to wait for pattern
 * @param output Buffer to store matching line (optional, can be NULL)
 * @param output_size Size of output buffer
 * @return true if pattern found, false if timeout or error
 */
static bool wait_for_log_pattern(const char *log_path, const char *pattern, int timeout_ms, char *output,
                                 size_t output_size) {
  int elapsed_ms = 0;
  FILE *log_file = NULL;
  char line[MAX_LOG_LINE_LENGTH];
  long last_pos = 0;

  while (elapsed_ms < timeout_ms) {
    log_file = fopen(log_path, "r");
    if (log_file) {
      // Resume from last read position
      fseek(log_file, last_pos, SEEK_SET);

      while (fgets(line, sizeof(line), log_file)) {
        if (strstr(line, pattern)) {
          if (output && output_size > 0) {
            SAFE_STRNCPY(output, line, output_size);
          }
          last_pos = ftell(log_file);
          fclose(log_file);
          return true;
        }
        last_pos = ftell(log_file);
      }

      fclose(log_file);
    }

    usleep(LOG_POLL_INTERVAL_MS * 1000);
    elapsed_ms += LOG_POLL_INTERVAL_MS;
  }

  return false;
}

/**
 * @brief Extract session string from log line
 * @param log_line Log line containing session string
 * @param session_string Output buffer for session string
 * @param size Size of output buffer
 * @return true if session string extracted, false otherwise
 *
 * Expected format: "Session string: blue-mountain-tiger"
 * or variations with quotes or formatting
 */
static bool extract_session_string(const char *log_line, char *session_string, size_t size) {
  // Look for common patterns:
  // - "Session string: blue-mountain-tiger"
  // - "Session: blue-mountain-tiger"
  // - "Join with: blue-mountain-tiger"

  const char *patterns[] = {"Session String: ",                           // Server mode format
                            "Session ready! Share this with your peer: ", // Discovery mode format
                            "Session string: ",
                            "Session: ",
                            "Join with: ",
                            "session string: ",
                            "session: ",
                            NULL};

  for (int i = 0; patterns[i] != NULL; i++) {
    const char *start = strstr(log_line, patterns[i]);
    if (start) {
      start += strlen(patterns[i]);

      // Skip whitespace and quotes
      while (*start == ' ' || *start == '"' || *start == '\'')
        start++;

      // Copy until whitespace, quote, or end of line
      size_t j = 0;
      while (j < size - 1 && *start && *start != ' ' && *start != '"' && *start != '\'' && *start != '\n' &&
             *start != '\r') {
        session_string[j++] = *start++;
      }
      session_string[j] = '\0';

      // Verify format: word-word-word
      if (j > 10 && strchr(session_string, '-') != NULL) {
        return true;
      }
    }
  }

  return false;
}

// =============================================================================
// Test Cases
// =============================================================================

/**
 * @brief Full end-to-end WebRTC connection test via discovery service
 *
 * Tests:
 * 1. Discovery service startup
 * 2. Host creates session and gets session string
 * 3. Guest joins via session string with --prefer-webrtc
 * 4. WebRTC connection establishes via STUN
 * 5. Both parties can exchange data
 * 6. Clean shutdown preserves logs
 */
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(webrtc_discovery_e2e, LOG_DEBUG, LOG_DEBUG, false, false);

Test(webrtc_discovery_e2e, full_connection_flow) {
  const char *binary_path = test_get_binary_path();
  int acds_port = get_unique_test_port();
  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%d", acds_port);

  // Generate log file paths
  char acds_log[256], host_log[256], guest_log[256];
  snprintf(acds_log, sizeof(acds_log), "/tmp/webrtc_e2e_acds_%d.log", getpid());
  snprintf(host_log, sizeof(host_log), "/tmp/webrtc_e2e_host_%d.log", getpid());
  snprintf(guest_log, sizeof(guest_log), "/tmp/webrtc_e2e_guest_%d.log", getpid());

  // ============================================================
  // Step 1: Start discovery-service
  // ============================================================

  char *acds_argv[] = {(char *)binary_path,
                       "discovery-service",
                       "--port",
                       port_str,
                       "--database",
                       test_db_path,
                       "--log-level",
                       "debug",
                       "--no-status-screen", // Disable UI for clean log parsing
                       NULL};

  pid_t acds_pid = spawn_process_with_log(binary_path, acds_argv, "acds", acds_log);
  cr_assert_gt(acds_pid, 0, "Failed to spawn discovery-service");

  // Wait for discovery service to be ready
  bool acds_ready =
      wait_for_log_pattern(acds_log, "Discovery server accepting connections", ACDS_STARTUP_TIMEOUT_MS, NULL, 0);
  cr_assert(acds_ready, "Discovery service failed to start (log: %s)", acds_log);

  // ============================================================
  // Step 2: Start host to create session
  // ============================================================

  char *host_argv[] = {(char *)binary_path, "--log-level", "debug",
                       "server",        // Run as SERVER with --discovery flag
                       "0.0.0.0", "::", // Bind addresses
                       "--port", "27224",
                       "--discovery",           // Register with discovery service
                       "--discovery-expose-ip", // Allow IP exposure for testing
                       "--discovery-service", "localhost", "--discovery-port", port_str,
                       "--no-status-screen", // Disable UI for clean log parsing
                       // Server doesn't need video/audio source - clients provide media
                       // Server runs continuously until terminated
                       "--no-encrypt", // Simplify for testing
                       NULL};

  pid_t host_pid = spawn_process_with_log(binary_path, host_argv, "host", host_log);
  cr_assert_gt(host_pid, 0, "Failed to spawn host");

  // Wait for session string in host logs (server mode uses "Session String:")
  char session_line[MAX_LOG_LINE_LENGTH] = {0};
  bool session_found =
      wait_for_log_pattern(host_log, "Session String:", HOST_STARTUP_TIMEOUT_MS, session_line, sizeof(session_line));
  cr_assert(session_found, "Host failed to create session (log: %s)", host_log);

  // Extract session string
  char session_string[64] = {0};
  bool extracted = extract_session_string(session_line, session_string, sizeof(session_string));
  cr_assert(extracted, "Failed to extract session string from: %s", session_line);
  cr_assert_gt(strlen(session_string), 10, "Session string too short: %s", session_string);

  fprintf(stderr, "Test: Extracted session string: %s\n", session_string);

  // ============================================================
  // Step 3: Start guest with session string and --prefer-webrtc
  // ============================================================

  char *guest_argv[] = {(char *)binary_path,
                        session_string, // Discovery mode with session string
                        "--discovery-service",
                        "localhost",
                        "--discovery-port",
                        port_str,
                        "--prefer-webrtc", // Force WebRTC instead of direct TCP
                        "--log-level",
                        "debug",
                        "--test-pattern", // Use test pattern instead of webcam
                        "--snapshot",
                        "--snapshot-delay",
                        "10", // Keep alive for 10s to allow WebRTC connection
                        "--volume",
                        "0",
                        "--no-encrypt",
                        NULL};

  pid_t guest_pid = spawn_process_with_log(binary_path, guest_argv, "guest", guest_log);
  cr_assert_gt(guest_pid, 0, "Failed to spawn guest");

  // ============================================================
  // Step 4: Verify WebRTC connection establishment
  // ============================================================

  // Look for WebRTC DataChannel establishment in logs
  const char *webrtc_patterns[] = {"WebRTC DataChannel established", "WebRTC DataChannel successfully established",
                                   NULL};

  bool webrtc_connected = false;
  for (int i = 0; webrtc_patterns[i] != NULL && !webrtc_connected; i++) {
    webrtc_connected = wait_for_log_pattern(guest_log, webrtc_patterns[i], WEBRTC_CONNECTION_TIMEOUT_MS, NULL, 0);
  }

  cr_assert(webrtc_connected, "WebRTC connection failed to establish (logs: host=%s, guest=%s)", host_log, guest_log);

  // Also verify host sees the connection
  bool host_connected = wait_for_log_pattern(host_log, "WebRTC", WEBRTC_CONNECTION_TIMEOUT_MS, NULL, 0);
  cr_assert(host_connected, "Host did not detect WebRTC connection (log: %s)", host_log);

  // ============================================================
  // Step 5: Wait for guest to complete (server stays running)
  // ============================================================

  int guest_exit_code = -1;
  // Wait for guest to complete snapshot and exit
  bool guest_exited = wait_for_process_exit(guest_pid, 15000, &guest_exit_code);

  cr_assert(guest_exited, "Guest did not exit cleanly");
  cr_assert_eq(guest_exit_code, 0, "Guest exited with error: %d", guest_exit_code);

  // ============================================================
  // Step 6: Clean shutdown (logs preserved for debugging)
  // ============================================================

  terminate_process(acds_pid);

  // Verify log files exist and are not empty
  struct stat st;
  cr_assert_eq(stat(acds_log, &st), 0, "ACDS log missing: %s", acds_log);
  cr_assert_gt(st.st_size, 0, "ACDS log is empty");

  cr_assert_eq(stat(host_log, &st), 0, "Host log missing: %s", host_log);
  cr_assert_gt(st.st_size, 0, "Host log is empty");

  cr_assert_eq(stat(guest_log, &st), 0, "Guest log missing: %s", guest_log);
  cr_assert_gt(st.st_size, 0, "Guest log is empty");

  fprintf(stderr, "Test complete. Logs preserved:\n");
  fprintf(stderr, "  ACDS:  %s\n", acds_log);
  fprintf(stderr, "  Host:  %s\n", host_log);
  fprintf(stderr, "  Guest: %s\n", guest_log);
}
