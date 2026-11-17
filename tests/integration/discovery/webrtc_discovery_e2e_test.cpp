#include <catch2/catch_test_macros.hpp>
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

#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/common.h>
#include <catch2/catch_all.hpp>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <libplatform/compiler.h>
#include <libplatform/filesystem.h>
#include <libplatform/process.h>
#include <libplatform/system.h>
#include <libplatform/terminal.h>
#include <ascii-chat/common.h>

// Test configuration
#define TEST_PORT_BASE 20000
#define ACDS_STARTUP_TIMEOUT_MS 10000
#define HOST_STARTUP_TIMEOUT_MS 10000
#define WEBRTC_CONNECTION_TIMEOUT_MS 15000 // WebRTC can take time with STUN
#define PROCESS_CLEANUP_TIMEOUT_MS 2000
#define MAX_PROCESSES 10
#define LOG_POLL_INTERVAL_MS 100
#define MAX_LOG_LINE_LENGTH 2048

// Process management
typedef struct {
  platform_process_t *process;
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
  int pid_offset = (platform_get_pid() % 400) * 10;
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
  char temp_dir[256];
  REQUIRE(platform_get_temp_dir(temp_dir, sizeof(temp_dir)));
  snprintf(test_db_path, sizeof(test_db_path), "%s/acds_webrtc_e2e_%d.db", temp_dir, platform_get_pid());
  snprintf(test_db_wal, sizeof(test_db_wal), "%s-wal", test_db_path);
  snprintf(test_db_shm, sizeof(test_db_shm), "%s-shm", test_db_path);

  // Clean up any leftover database files
  platform_unlink(test_db_path);
  platform_unlink(test_db_wal);
  platform_unlink(test_db_shm);

  // Disable host identity check for tests
  platform_setenv("ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK", "1");
  platform_setenv("ASCII_CHAT_AUDIO", "false");
}

void teardown_webrtc_e2e_tests(void) {
  // Kill any remaining processes
  for (int i = 0; i < process_count; i++) {
    if (tracked_processes[i].running && tracked_processes[i].process) {
      platform_process_kill(tracked_processes[i].process);
      platform_process_wait(tracked_processes[i].process, PROCESS_CLEANUP_TIMEOUT_MS, NULL);
      platform_process_destroy(tracked_processes[i].process);
      tracked_processes[i].process = NULL;
    }
  }

  // Clean up database files (logs are intentionally preserved)
  platform_unlink(test_db_path);
  platform_unlink(test_db_wal);
  platform_unlink(test_db_shm);

  log_set_level(LOG_DEBUG); // Restore logging
}
// =============================================================================
// Process Management Utilities
// =============================================================================

static platform_process_t *spawn_process_with_log(const char *binary_path, const char *const argv[], const char *name, const char *log_path) {
  int log_fd = platform_open("webrtc-e2e-log", log_path, PLATFORM_O_WRONLY | PLATFORM_O_CREAT | PLATFORM_O_TRUNC | PLATFORM_O_BINARY, 0600);
  if (log_fd < 0)
    return NULL;
  platform_process_t *process = NULL;
  int result = platform_process_spawn(&process, binary_path, argv, -1, log_fd, log_fd);
  platform_close(log_fd);
  if (result == ASCIICHAT_OK && process && process_count < MAX_PROCESSES) {
    tracked_processes[process_count].process = process;
    tracked_processes[process_count].name = name;
    SAFE_STRNCPY(tracked_processes[process_count].log_path, log_path, sizeof(tracked_processes[process_count].log_path));
    tracked_processes[process_count].running = true;
    process_count++;
  }

  return process;
}

static bool wait_for_process_exit(platform_process_t *process, int timeout_ms, int *exit_code) {
  return platform_process_wait(process, timeout_ms, exit_code) == ASCIICHAT_OK;
}

static void terminate_process(platform_process_t *process) {
  if (!process)
    return;

  platform_process_kill(process);
  int exit_code = -1;
  wait_for_process_exit(process, PROCESS_CLEANUP_TIMEOUT_MS, &exit_code);

  // Mark as not running
  for (int i = 0; i < process_count; i++) {
    if (tracked_processes[i].process == process) {
      tracked_processes[i].running = false;
      tracked_processes[i].exit_code = exit_code;
      platform_process_destroy(process);
      tracked_processes[i].process = NULL;
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
static bool wait_for_log_pattern(const char *log_path, const char *pattern, int timeout_ms, char *output, size_t output_size) {
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

    platform_sleep_ms(LOG_POLL_INTERVAL_MS);
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
      while (j < size - 1 && *start && *start != ' ' && *start != '"' && *start != '\'' && *start != '\n' && *start != '\r') {
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

TEST_CASE("webrtc_discovery_e2e/full_connection_flow", "[webrtc_discovery_e2e]") {
  const char *binary_path = test_get_binary_path();
  int acds_port = get_unique_test_port();
  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%d", acds_port);

  // Generate log file paths
  char acds_log[256], host_log[256], guest_log[256];
  char temp_dir[256];
  REQUIRE(platform_get_temp_dir(temp_dir, sizeof(temp_dir)));
  snprintf(acds_log, sizeof(acds_log), "%s/webrtc_e2e_acds_%d.log", temp_dir, platform_get_pid());
  snprintf(host_log, sizeof(host_log), "%s/webrtc_e2e_host_%d.log", temp_dir, platform_get_pid());
  snprintf(guest_log, sizeof(guest_log), "%s/webrtc_e2e_guest_%d.log", temp_dir, platform_get_pid());

  // ============================================================
  // Step 1: Start discovery-service
  // ============================================================

  const char *acds_argv[] = {binary_path,
                             "--no-check-update",
                             "--log-level",
                             "debug",
                             "discovery-service",
                             "--port",
                             port_str,
                             "--database",
                             test_db_path,
                             "--status-screen=false", // Disable UI for clean log parsing
                             NULL};

  platform_process_t *acds_process = spawn_process_with_log(binary_path, acds_argv, "acds", acds_log);
  REQUIRE(acds_process != NULL);

  // Wait for discovery service to be ready
  bool acds_ready = wait_for_log_pattern(acds_log, "Listening on", ACDS_STARTUP_TIMEOUT_MS, NULL, 0);
  REQUIRE(acds_ready);

  // ============================================================
  // Step 2: Start host to create session
  // ============================================================

  const char *host_argv[] = {binary_path, "--no-check-update", "--log-level", "debug",
                             "server",        // Run as SERVER with --discovery flag
                             "0.0.0.0", "::", // Bind addresses
                             "--port", "27224",
                             "--discovery",           // Register with discovery service
                             "--discovery-expose-ip", // Allow IP exposure for testing
                             "--discovery-service", "localhost", "--discovery-service-port", port_str,
                             "--status-screen=false", // Disable UI for clean log parsing
                             // Server doesn't need video/audio source - clients provide media
                             // Server runs continuously until terminated
                             "--no-encrypt", // Simplify for testing
                             NULL};

  platform_process_t *host_process = spawn_process_with_log(binary_path, host_argv, "host", host_log);
  REQUIRE(host_process != NULL);

  // Wait for session string in host logs (server mode uses "Session String:")
  char session_line[MAX_LOG_LINE_LENGTH] = {0};
  bool session_found = wait_for_log_pattern(host_log, "Session String:", HOST_STARTUP_TIMEOUT_MS, session_line, sizeof(session_line));
  REQUIRE(session_found);

  // Extract session string
  char session_string[64] = {0};
  bool extracted = extract_session_string(session_line, session_string, sizeof(session_string));
  REQUIRE(extracted);
  REQUIRE((strlen(session_string)) > (10));

  fprintf(stderr, "Test: Extracted session string: %s\n", session_string);

  // ============================================================
  // Step 3: Start guest with session string and --prefer-webrtc
  // ============================================================

  const char *guest_argv[] = {binary_path,
                              "--no-check-update",
                              "--log-level",
                              "debug",
                              session_string, // Discovery mode with session string
                              "--discovery-service",
                              "localhost",
                              "--discovery-service-port",
                              port_str,
                              "--prefer-webrtc", // Force WebRTC instead of direct TCP
                              "--port",
                              "27234",          // Successor preparation needs a separate local listener.
                              "--test-pattern", // Use test pattern instead of webcam
                              "--snapshot",
                              "--snapshot-delay",
                              "10", // Keep alive for 10s to allow WebRTC connection
                              "--volume",
                              "0",
                              "--no-encrypt",
                              NULL};

  platform_process_t *guest_process = spawn_process_with_log(binary_path, guest_argv, "guest", guest_log);
  REQUIRE(guest_process != NULL);

  // ============================================================
  // Step 4: Verify WebRTC connection establishment
  // ============================================================

  // Look for WebRTC DataChannel establishment in logs
  const char *webrtc_patterns[] = {"WebRTC DataChannel established", "WebRTC DataChannel successfully established", NULL};

  bool webrtc_connected = false;
  for (int i = 0; webrtc_patterns[i] != NULL && !webrtc_connected; i++) {
    webrtc_connected = wait_for_log_pattern(guest_log, webrtc_patterns[i], WEBRTC_CONNECTION_TIMEOUT_MS, NULL, 0);
  }

  REQUIRE(webrtc_connected);

  // Also verify host sees the connection
  bool host_connected = wait_for_log_pattern(host_log, "WebRTC", WEBRTC_CONNECTION_TIMEOUT_MS, NULL, 0);
  REQUIRE(host_connected);

  // ============================================================
  // Step 5: Wait for guest to complete (server stays running)
  // ============================================================

  int guest_exit_code = -1;
  // Wait for guest to complete snapshot and exit
  bool guest_exited = wait_for_process_exit(guest_process, 15000, &guest_exit_code);

  REQUIRE(guest_exited);
  REQUIRE((guest_exit_code) == (0));

  // ============================================================
  // Step 6: Clean shutdown (logs preserved for debugging)
  // ============================================================

  terminate_process(acds_process);
  terminate_process(host_process);
  terminate_process(guest_process);

  // Verify log files exist and are not empty
  platform_stat_t st;
  REQUIRE(platform_stat(acds_log, &st) == ASCIICHAT_OK);
  REQUIRE(st.size > 0);

  REQUIRE(platform_stat(host_log, &st) == ASCIICHAT_OK);
  REQUIRE(st.size > 0);

  REQUIRE(platform_stat(guest_log, &st) == ASCIICHAT_OK);
  REQUIRE(st.size > 0);

  fprintf(stderr, "Test complete. Logs preserved:\n");
  fprintf(stderr, "  ACDS:  %s\n", acds_log);
  fprintf(stderr, "  Host:  %s\n", host_log);
  fprintf(stderr, "  Guest: %s\n", guest_log);
}
