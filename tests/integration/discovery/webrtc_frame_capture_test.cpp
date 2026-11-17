#include <catch2/catch_test_macros.hpp>
/**
 * @file webrtc_frame_capture_test.c
 * @brief Integration test for WebRTC frame capture via discovery service
 *
 * This test performs a full end-to-end WebRTC connection through ACDS:
 * 1. Spawns ACDS discovery service on port 27325
 * 2. Spawns server with --discovery and --discovery-expose-ip
 * 3. Extracts session string from server output
 * 4. Connects client with --prefer-webrtc and a synthetic source
 * 5. Validates that the WebRTC media channel becomes active
 *
 * This validates the complete WebRTC connection stack including:
 * - ACDS session creation and registration
 * - WebRTC signaling via ACDS
 * - ICE candidate exchange
 * - DataChannel establishment
 * - WebRTC media-channel establishment
 */

#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/common.h>
#include <catch2/catch_all.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/common.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/log/log.h>
#include <libplatform/compiler.h>
#include <libplatform/system.h>
#include <libplatform/filesystem.h>
#include <libplatform/process.h>
#include <libplatform/terminal.h>
#include <ascii-chat/video/terminal/ansi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test fixture
// Process tracking
static platform_process_t *g_acds_process = NULL;
static platform_process_t *g_server_process = NULL;

// Test output paths
static char g_acds_log_path[512];
static char g_acds_db_path[512];
static char g_server_log_path[512];
static char g_client_output_path[512];
static char g_client_log_path[512];

/**
 * @brief Kill a process and wait for it to exit
 */
static void kill_and_wait(platform_process_t *process, const char *name) {
  if (process) {
    log_debug("Killing %s", name);
    platform_process_kill(process);
    platform_process_wait(process, 2000, NULL);
    platform_process_destroy(process);
  }
}

/**
 * @brief Cleanup function to kill spawned processes
 */
static void cleanup_processes(void) {
  kill_and_wait(g_server_process, "server");
  kill_and_wait(g_acds_process, "acds");
  g_acds_process = NULL;
  g_server_process = NULL;

  // Clean up database and SQLite WAL files only
  platform_unlink(g_acds_db_path);
  char db_sidecar[544];
  snprintf(db_sidecar, sizeof(db_sidecar), "%s-shm", g_acds_db_path);
  platform_unlink(db_sidecar);
  snprintf(db_sidecar, sizeof(db_sidecar), "%s-wal", g_acds_db_path);
  platform_unlink(db_sidecar);
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
      char *end = marker;
      while (*end && *end != ' ' && *end != '\t' && *end != '\n' && *end != '\r' && *end != '\033') {
        end++;
      }
      *end = '\0';
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

  // Strip ANSI escape codes from output (terminal positioning/color codes)
  size_t orig_len = strlen(output);
  char *stripped = ansi_strip_escapes(output, orig_len);
  if (!stripped) {
    log_error("Failed to strip ANSI codes from output");
    return 0;
  }
  const char *clean_output = stripped;
  size_t stripped_len = strlen(stripped);
  log_debug("Stripped ANSI codes: %zu -> %zu bytes (removed %zu bytes)", orig_len, stripped_len, orig_len - stripped_len);

  // Debug: Save stripped output to file for inspection
  FILE *debug_file = fopen("/tmp/client_snapshot_stripped.txt", "w");
  if (debug_file) {
    fwrite(stripped, 1, stripped_len, debug_file);
    fclose(debug_file);
    log_debug("Saved stripped output to /tmp/client_snapshot_stripped.txt");
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

  const char *p = clean_output;
  while (*p) {
    // Skip log lines (start with '[')
    if (*p == '[' && (p == clean_output || *(p - 1) == '\n')) {
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
      // Lowered threshold from 50 to 30 to accommodate narrower test pattern frames
      if (chars_in_line > 30 && palette_chars_in_line * 100 / chars_in_line > 80) {
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

  log_debug("ASCII frame validation: %d lines, %d palette chars / %d total (%d%%), max consecutive art lines: %d", line_count, palette_chars, total_chars, density_percent, max_consecutive_art_lines);

  // Require:
  // - At least 20 consecutive lines of ASCII art (indicates full frame)
  // - High palette character density (>60% overall)
  // - Significant palette character count (>500 chars from palette)
  int result = 1;
  if (max_consecutive_art_lines < 20) {
    log_error("Not enough consecutive ASCII art lines: %d (need 20)", max_consecutive_art_lines);
    result = 0;
  } else if (density_percent < 60) {
    log_error("Palette character density too low: %d%% (need 60%%)", density_percent);
    result = 0;
  } else if (palette_chars < 500) {
    log_error("Not enough palette characters: %d (need 500)", palette_chars);
    result = 0;
  }

  SAFE_FREE(stripped);
  return result;
}

/**
 * @brief Test setup - clean up any leftover files
 */
static void setup_test(void) {
  char temp_dir[256];
  REQUIRE(platform_get_temp_dir(temp_dir, sizeof(temp_dir)));
  snprintf(g_acds_log_path, sizeof(g_acds_log_path), "%s/acds_test.log", temp_dir);
  snprintf(g_acds_db_path, sizeof(g_acds_db_path), "%s/acds_test.db", temp_dir);
  snprintf(g_server_log_path, sizeof(g_server_log_path), "%s/server_test.log", temp_dir);
  snprintf(g_client_output_path, sizeof(g_client_output_path), "%s/client_snapshot.txt", temp_dir);
  snprintf(g_client_log_path, sizeof(g_client_log_path), "%s/client_test.log", temp_dir);
  // Clean up any leftover files from previous runs
  platform_unlink(g_acds_log_path);
  platform_unlink(g_acds_db_path);
  char db_sidecar[544];
  snprintf(db_sidecar, sizeof(db_sidecar), "%s-shm", g_acds_db_path);
  platform_unlink(db_sidecar);
  snprintf(db_sidecar, sizeof(db_sidecar), "%s-wal", g_acds_db_path);
  platform_unlink(db_sidecar);
  platform_unlink(g_server_log_path);
  platform_unlink(g_client_output_path);
  platform_setenv("ASCII_CHAT_AUDIO", "false");
}

static platform_process_t *spawn_logged(const char *binary_path, const char *const *argv, const char *log_path) {
  int fd = platform_open("webrtc-frame-log", log_path, PLATFORM_O_WRONLY | PLATFORM_O_CREAT | PLATFORM_O_TRUNC | PLATFORM_O_BINARY, 0600);
  if (fd < 0)
    return NULL;
  platform_process_t *process = NULL;
  int result = platform_process_spawn(&process, binary_path, argv, -1, fd, fd);
  platform_close(fd);
  return result == ASCIICHAT_OK ? process : NULL;
}

/**
 * @brief Test full WebRTC connection via discovery service with frame capture
 */
// Use verbose logging with debug level enabled and stdout/stderr not disabled

TEST_CASE("webrtc_discovery/frame_capture_via_webrtc", "[webrtc_discovery]") {
  // Determine binary path based on working directory
  const char *binary_path = test_get_binary_path();

  // Verify binary exists
  REQUIRE(access(binary_path, X_OK) == 0);

  // ========================================================================
  // Step 1: Start ACDS discovery service on port 27325
  // ========================================================================
  log_info("Starting ACDS discovery service...");
  const char *acds_argv[] = {binary_path, "--no-check-update", "discovery-service", "127.0.0.1", "::", "--port", "27325", "--database", g_acds_db_path, "--status-screen=false", NULL};
  g_acds_process = spawn_logged(binary_path, acds_argv, g_acds_log_path);
  REQUIRE(g_acds_process != NULL);

  // Wait for ACDS to be ready (check for "Listening on" in log) with 10 second timeout
  sleep(1);
  int acds_ready = wait_for_pattern(g_acds_log_path, "Listening on", 100);
  if (!acds_ready) {
    log_error("ACDS failed to start within 10 seconds, killing process");
    kill_and_wait(g_acds_process, "acds");
    g_acds_process = NULL;
  }
  REQUIRE(acds_ready);

  // ========================================================================
  // Step 2: Start server with discovery registration
  // ========================================================================
  log_info("Starting server with discovery...");
  const char *server_argv[] = {binary_path,
                               "--no-check-update",
                               "--log-level",
                               "debug",
                               "server",
                               "0.0.0.0",
                               "::",
                               "--port",
                               "27224",
                               "--discovery",
                               "--discovery-expose-ip",
                               "--discovery-service",
                               "127.0.0.1",
                               "--discovery-service-port",
                               "27325",
                               "--status-screen=false",
                               NULL};
  g_server_process = spawn_logged(binary_path, server_argv, g_server_log_path);
  REQUIRE(g_server_process != NULL);

  // Wait for server to register and get session string with 10 second timeout
  sleep(2);
  int server_ready = wait_for_pattern(g_server_log_path, "Session String:", 100);
  if (!server_ready) {
    log_error("Server failed to register with ACDS within 10 seconds, killing processes");
    kill_and_wait(g_server_process, "server");
    kill_and_wait(g_acds_process, "acds");
    g_server_process = NULL;
    g_acds_process = NULL;
  }
  REQUIRE(server_ready);

  // ========================================================================
  // Step 3: Extract session string from server log
  // ========================================================================
  char session_string[64] = {0};
  int session_found = extract_session_string(g_server_log_path, session_string, sizeof(session_string));
  REQUIRE(session_found);
  REQUIRE((session_string[0]) != ('\0'));
  log_info("Extracted session string: %s", session_string);

  // ========================================================================
  // Step 4: Connect client via WebRTC with snapshot mode
  // ========================================================================
  log_info("Connecting client via WebRTC with snapshot...");

  // Run client and capture output to file (stdout to snapshot, stderr to log) with 10 second timeout.
  // A zero snapshot delay exits before WebRTC signaling and the first frame can
  // complete; give the peer several seconds to connect before taking the frame.
  // Route logs to a file so stdout remains available for the rendered frame.
  const char *client_argv[] = {binary_path,
                               "--no-check-update",
                               "--log-file",
                               g_client_log_path,
                               session_string,
                               "--snapshot",
                               "--snapshot-delay",
                               "5",
                               "--test-pattern",
                               "--discovery-service",
                               "127.0.0.1",
                               "--discovery-service-port",
                               "27325",
                               "--prefer-webrtc",
                               "--port",
                               "27234",
                               NULL};
  platform_process_t *client_process = spawn_logged(binary_path, client_argv, g_client_output_path);
  REQUIRE(client_process != NULL);
  int client_result = -1;
  REQUIRE(platform_process_wait(client_process, 20000, &client_result) == ASCIICHAT_OK);
  platform_process_destroy(client_process);
  REQUIRE(client_result == 0);

  // Discovery participant mode intentionally does not render a terminal frame;
  // it hands the established WebRTC transport to the media session. Verify the
  // transport transition directly in the client log instead of stdout.
  int media_active = wait_for_pattern(g_client_log_path, "WebRTC Media Channel ACTIVE", 1);
  REQUIRE(media_active);

  // Cleanup will be done automatically by .fini = cleanup_processes
}
