/**
 * @file webrtc_frame_capture_test.c
 * @brief Integration test for WebRTC frame capture via discovery service
 *
 * This test performs a full end-to-end WebRTC connection through ACDS:
 * 1. Spawns ACDS discovery service on port 27225
 * 2. Spawns server with --discovery and --discovery-expose-ip
 * 3. Extracts session string from server output
 * 4. Connects client with --prefer-webrtc --snapshot --snapshot-delay 0
 * 5. Validates that ASCII art frame was captured in stdout
 *
 * This validates the complete WebRTC connection stack including:
 * - ACDS session creation and registration
 * - WebRTC signaling via ACDS
 * - ICE candidate exchange
 * - DataChannel establishment
 * - Frame transmission over WebRTC
 */

#include <criterion/criterion.h>
#include <criterion/redirect.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/platform/abstraction.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// Test fixture
TestSuite(webrtc_discovery, .timeout = 20.0);

// Process tracking
static pid_t g_acds_pid = -1;
static pid_t g_server_pid = -1;

// Test output paths
#define ACDS_LOG_PATH "/tmp/acds_test.log"
#define ACDS_DB_PATH "/tmp/acds_test.db"
#define SERVER_LOG_PATH "/tmp/server_test.log"
#define CLIENT_OUTPUT_PATH "/tmp/client_snapshot.txt"

/**
 * @brief Kill a process and wait for it to exit
 */
static void kill_and_wait(pid_t pid, const char *name) {
  if (pid > 0) {
    log_debug("Killing %s (PID %d)", name, pid);
    kill(pid, SIGTERM);
    sleep(1);
    int status;
    waitpid(pid, &status, WNOHANG);
    kill(pid, SIGKILL); // Force kill if still alive
    waitpid(pid, &status, 0);
  }
}

/**
 * @brief Cleanup function to kill spawned processes
 */
static void cleanup_processes(void) {
  kill_and_wait(g_server_pid, "server");
  kill_and_wait(g_acds_pid, "acds");
  g_acds_pid = -1;
  g_server_pid = -1;

  // Clean up database and SQLite WAL files only
  unlink(ACDS_DB_PATH);
  unlink(ACDS_DB_PATH "-shm"); // SQLite shared memory file
  unlink(ACDS_DB_PATH "-wal"); // SQLite write-ahead log
  // Preserve all log files and client output for inspection:
  // - ACDS_LOG_PATH (/tmp/acds_test.log)
  // - SERVER_LOG_PATH (/tmp/server_test.log)
  // - CLIENT_OUTPUT_PATH (/tmp/client_snapshot.txt)
  // - /tmp/client_test.log (stderr)
}

/**
 * @brief Extract session string from server log file
 *
 * Searches for "Session String: <session>" pattern in the log
 * Returns 1 on success, 0 on failure
 */
static int extract_session_string(const char *log_path, char *session_out, size_t session_len) {
  FILE *f = fopen(log_path, "r");
  if (!f) {
    return 0;
  }

  char line[512];
  int found = 0;
  while (fgets(line, sizeof(line), f)) {
    // Look for "Session String: " or "📋 Session String: "
    char *marker = strstr(line, "Session String: ");
    if (marker) {
      marker += strlen("Session String: ");
      // Extract session string (adjective-noun-noun format)
      char *end = strchr(marker, '\n');
      if (end) {
        *end = '\0';
      }
      // Trim any trailing whitespace or ANSI codes
      while (*marker == ' ' || *marker == '\t' || *marker == '\033') {
        if (*marker == '\033') {
          // Skip ANSI escape sequence
          while (*marker && *marker != 'm')
            marker++;
          if (*marker)
            marker++;
        } else {
          marker++;
        }
      }
      SAFE_STRNCPY(session_out, marker, session_len);
      found = 1;
      break;
    }
  }

  fclose(f);
  return found;
}

/**
 * @brief Wait for a file to contain a specific pattern
 *
 * Returns 1 if pattern found, 0 on timeout
 */
static int wait_for_pattern(const char *file_path, const char *pattern, int max_attempts) {
  for (int i = 0; i < max_attempts; i++) {
    FILE *f = fopen(file_path, "r");
    if (f) {
      char line[512];
      while (fgets(line, sizeof(line), f)) {
        if (strstr(line, pattern)) {
          fclose(f);
          return 1;
        }
      }
      fclose(f);
    }
    usleep(100000); // 100ms
  }
  return 0;
}

/**
 * @brief Check if output contains ASCII art characters
 *
 * Validates that the output contains characters from the standard ASCII palette
 * with high density, indicating actual rendered video frames:
 * - Standard palette: " ...',;:clodxkO0KXNWM"
 * - Multiple consecutive lines (at least 20)
 * - High character density (>80% palette characters)
 */
static int validate_ascii_frame(const char *output) {
  if (!output || strlen(output) < 500) {
    log_error("Output too short: %zu bytes", output ? strlen(output) : 0);
    return 0;
  }

  // Standard ASCII palette characters (from lib/video/palette.c)
  const char *palette = " ...',;:clodxkO0KXNWM";

  // Count lines and palette character occurrences
  int line_count = 0;
  int total_chars = 0;
  int palette_chars = 0;
  int consecutive_art_lines = 0;
  int max_consecutive_art_lines = 0;
  int chars_in_line = 0;
  int palette_chars_in_line = 0;

  const char *p = output;
  while (*p) {
    // Skip log lines (start with '[')
    if (*p == '[' && (p == output || *(p - 1) == '\n')) {
      // Skip entire log line
      while (*p && *p != '\n') {
        p++;
      }
      if (*p == '\n') {
        p++;
      }
      continue;
    }

    if (*p == '\n') {
      line_count++;
      // Line with >80% palette characters is considered an ASCII art line
      if (chars_in_line > 50 && palette_chars_in_line * 100 / chars_in_line > 80) {
        consecutive_art_lines++;
        if (consecutive_art_lines > max_consecutive_art_lines) {
          max_consecutive_art_lines = consecutive_art_lines;
        }
      } else {
        consecutive_art_lines = 0;
      }
      chars_in_line = 0;
      palette_chars_in_line = 0;
    } else {
      total_chars++;
      chars_in_line++;
      if (strchr(palette, *p)) {
        palette_chars++;
        palette_chars_in_line++;
      }
    }
    p++;
  }

  int density_percent = total_chars > 0 ? (palette_chars * 100 / total_chars) : 0;

  log_debug("ASCII frame validation: %d lines, %d palette chars / %d total (%d%%), max consecutive art lines: %d",
            line_count, palette_chars, total_chars, density_percent, max_consecutive_art_lines);

  // Require:
  // - At least 20 consecutive lines of ASCII art (indicates full frame)
  // - High palette character density (>60% overall)
  // - Significant palette character count (>500 chars from palette)
  if (max_consecutive_art_lines < 20) {
    log_error("Not enough consecutive ASCII art lines: %d (need 20)", max_consecutive_art_lines);
    return 0;
  }
  if (density_percent < 60) {
    log_error("Palette character density too low: %d%% (need 60%%)", density_percent);
    return 0;
  }
  if (palette_chars < 500) {
    log_error("Not enough palette characters: %d (need 500)", palette_chars);
    return 0;
  }

  return 1;
}

/**
 * @brief Test setup - clean up any leftover files
 */
static void setup_test(void) {
  // Clean up any leftover files from previous runs
  unlink(ACDS_LOG_PATH);
  unlink(ACDS_DB_PATH);
  unlink(ACDS_DB_PATH "-shm"); // SQLite shared memory file
  unlink(ACDS_DB_PATH "-wal"); // SQLite write-ahead log
  unlink(SERVER_LOG_PATH);
  unlink(CLIENT_OUTPUT_PATH);
}

/**
 * @brief Test full WebRTC connection via discovery service with frame capture
 */
Test(webrtc_discovery, frame_capture_via_webrtc, .init = setup_test, .fini = cleanup_processes) {
  // Determine binary path based on working directory
  const char *binary_path = access("./bin/ascii-chat", X_OK) == 0 ? "./bin/ascii-chat" : "./build/bin/ascii-chat";

  // Verify binary exists
  cr_assert(access(binary_path, X_OK) == 0, "ascii-chat binary must exist and be executable at %s", binary_path);

  // ========================================================================
  // Step 1: Start ACDS discovery service on port 27225
  // ========================================================================
  log_info("Starting ACDS discovery service...");
  g_acds_pid = fork();
  cr_assert_neq(g_acds_pid, -1, "Fork for ACDS should succeed");

  if (g_acds_pid == 0) {
    // Child: Start ACDS with fresh database (with 10 second timeout)
    freopen(ACDS_LOG_PATH, "w", stderr);
    freopen(ACDS_LOG_PATH, "w", stdout);
    execl("/opt/homebrew/bin/timeout", "timeout", "10", binary_path, "discovery-service", "127.0.0.1", "::", "--port",
          "27225", "--database", ACDS_DB_PATH, NULL);
    exit(1); // Should not reach here
  }

  // Wait for ACDS to be ready (check for "Listening on" in log) with 10 second timeout
  sleep(1);
  int acds_ready = wait_for_pattern(ACDS_LOG_PATH, "Listening on", 100); // 100 * 100ms = 10 seconds
  if (!acds_ready) {
    log_error("ACDS failed to start within 10 seconds, killing process");
    kill_and_wait(g_acds_pid, "acds");
    g_acds_pid = -1;
  }
  cr_assert(acds_ready, "ACDS should start and listen on port 27225 within 10 seconds");

  // ========================================================================
  // Step 2: Start server with discovery registration
  // ========================================================================
  log_info("Starting server with discovery...");
  g_server_pid = fork();
  cr_assert_neq(g_server_pid, -1, "Fork for server should succeed");

  if (g_server_pid == 0) {
    // Child: Start server (with 10 second timeout)
    freopen(SERVER_LOG_PATH, "w", stderr);
    freopen(SERVER_LOG_PATH, "w", stdout);
    execl("/opt/homebrew/bin/timeout", "timeout", "10", binary_path, "--log-level", "debug", "server", "0.0.0.0",
          "::", "--port", "27224", "--discovery", "--discovery-expose-ip", "--discovery-service", "127.0.0.1",
          "--discovery-port", "27225", NULL);
    exit(1); // Should not reach here
  }

  // Wait for server to register and get session string with 10 second timeout
  sleep(2);
  int server_ready = wait_for_pattern(SERVER_LOG_PATH, "Session String:", 100); // 100 * 100ms = 10 seconds
  if (!server_ready) {
    log_error("Server failed to register with ACDS within 10 seconds, killing processes");
    kill_and_wait(g_server_pid, "server");
    kill_and_wait(g_acds_pid, "acds");
    g_server_pid = -1;
    g_acds_pid = -1;
  }
  cr_assert(server_ready, "Server should register with ACDS and get session string within 10 seconds");

  // ========================================================================
  // Step 3: Extract session string from server log
  // ========================================================================
  char session_string[64] = {0};
  int session_found = extract_session_string(SERVER_LOG_PATH, session_string, sizeof(session_string));
  cr_assert(session_found, "Session string should be found in server log");
  cr_assert_neq(session_string[0], '\0', "Session string should not be empty");
  log_info("Extracted session string: %s", session_string);

  // ========================================================================
  // Step 4: Connect client via WebRTC with snapshot mode
  // ========================================================================
  log_info("Connecting client via WebRTC with snapshot...");

  // Run client and capture output to file (stdout to snapshot, stderr to log) with 10 second timeout
  char client_cmd[512];
  snprintf(client_cmd, sizeof(client_cmd),
           "/opt/homebrew/bin/timeout 10 %s --log-level dev \"%s\" --snapshot --snapshot-delay 0 --test-pattern "
           "--discovery-service 127.0.0.1 --discovery-port 27225 --prefer-webrtc > %s 2>/tmp/client_test.log",
           binary_path, session_string, CLIENT_OUTPUT_PATH);

  int client_result = system(client_cmd);
  log_debug("Client command exit status: %d", WEXITSTATUS(client_result));

  // Client may timeout or exit with error, that's OK if we got a frame
  // The important thing is that we captured output

  // ========================================================================
  // Step 5: Validate ASCII frame was captured
  // ========================================================================
  sleep(1); // Give time for output to be written

  // Read client output
  FILE *output_file = fopen(CLIENT_OUTPUT_PATH, "r");
  cr_assert_not_null(output_file, "Client output file should exist");

  // Read entire output
  fseek(output_file, 0, SEEK_END);
  long file_size = ftell(output_file);
  fseek(output_file, 0, SEEK_SET);

  cr_assert_gt(file_size, 0, "Client output should not be empty");

  char *output = SAFE_MALLOC(file_size + 1, char *);
  size_t read_size = fread(output, 1, file_size, output_file);
  output[read_size] = '\0';
  fclose(output_file);

  log_debug("Client output size: %ld bytes", file_size);
  log_debug("First 200 chars: %.200s", output);

  // Validate ASCII frame content
  int is_valid = validate_ascii_frame(output);
  cr_assert(is_valid, "Client output should contain valid ASCII art frame");

  // Verify output contains expected patterns
  cr_assert_not_null(strstr(output, "\n"), "Output should contain newlines (multi-line frame)");

  SAFE_FREE(output);

  // Cleanup will be done automatically by .fini = cleanup_processes
}
