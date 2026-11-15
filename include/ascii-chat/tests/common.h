#pragma once

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * @file tests/common.h
 * @brief Common test utilities and environment detection
 * @ingroup testing
 * @addtogroup testing
 * @{
 *
 * This header provides common utilities and helpers for writing ascii-chat tests.
 * It includes standard test headers, platform detection, and environment checks
 * to help tests run reliably across different environments (CI, Docker, WSL).
 *
 * CORE FEATURES:
 * ==============
 * - Test environment detection (CI, Docker, WSL)
 * - Standard test framework includes (Catch2)
 * - Common project headers and utilities
 * - Platform-specific includes (POSIX/Windows)
 * - Headless environment detection for webcam tests
 *
 * TEST ENVIRONMENT:
 * ================
 * This header automatically includes:
 * - Catch2 test helpers
 * - Project common headers
 * - Test logging utilities
 * - Platform-specific system headers
 *
 * HEADLESS ENVIRONMENT DETECTION:
 * ===============================
 * Tests that require hardware (like webcam) can use
 * test_is_in_headless_environment() to skip when running in CI/Docker/WSL.
 *
 * @note This header must be included before any test code that uses
 *       the test environment detection functions.
 * @note All test files should include this header for consistent
 *       test infrastructure.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date August 2025
 */

/* Storage capacities in elements; text buffers include the terminating NUL. */
#define TESTS_COMMON_VERSION_BUFFER_SIZE 256

#include <stdint.h>
#include <stdbool.h>

// Standard C headers
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// System headers needed by tests
#ifndef _WIN32
#include <unistd.h> // For unlink(), access(), etc.
#endif
#include <sys/stat.h> // For stat(), struct stat

#ifdef __cplusplus
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators_all.hpp>
#include <cstring>
#endif

// Project headers
#include "../common.h"
#include "../log/log.h"
#include "../tests/logging.h"
#include "test_env.h"

#ifdef __cplusplus
extern "C" {
#include <ascii-chat/crypto/gpg/agent.h>
#include <ascii-chat/crypto/gpg/gpg.h>
#include <ascii-chat/crypto/gpg/openpgp.h>
#include <ascii-chat/crypto/gpg/signing.h>
#include <ascii-chat/crypto/gpg/verification.h>
#include <ascii-chat/crypto/handshake/common.h>
#include <ascii-chat/crypto/https_keys.h>
#include <ascii-chat/crypto/keys.h>
#include <ascii-chat/crypto/known_hosts.h>
#include <ascii-chat/crypto/pem.h>
#include <ascii-chat/crypto/ssh/ssh_agent.h>
#include <ascii-chat/crypto/ssh/ssh_keys.h>
#include <ascii-chat/crypto/record.h>
#include <ascii-chat/crypto/rekey.h>
#include <ascii-chat/crypto/key_identity/keymask.h>
#include <ascii-chat/network/acip/security.h>
#include <ascii-chat/network/acip/prepare.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/discovery/database.h>
#include <ascii-chat/discovery/session.h>
#include <ascii-chat/discovery/key_set.h>
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/network/acip/acds_handlers.h>
#include <ascii-chat/log/grep.h>
#include <ascii-chat/log/json.h>
#include <ascii-chat/log/format.h>
#include <ascii-chat/log/colorize.h>
#include <ascii-chat/log/search.h>
#include <ascii-chat/session/session_log_buffer.h>
#include <ascii-chat/tui/notice.h>
#include <ascii-chat/util/format.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/util/url.h>
#include <ascii-chat/util/stdout_writer.h>
#include <ascii-chat/font.h>
#include <ascii-chat/media/kitty/image.h>
#include <ascii-chat/crypto/session.h>
#include <ascii-chat/network/compression.h>
#include <ascii-chat/network/consensus/coordinator.h>
#include <ascii-chat/network/acip/handlers.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/packet/queue.h>
#include <ascii-chat/network/packet/parsing.h>
#include <ascii-chat/terminal/fd/reader.h>
#include <ascii-chat/terminal/fd/writer.h>
#include <ascii-chat/tooling/query/query.h>
#include <ascii-chat/util/display.h>
#include <ascii-chat/ui/popup.h>
#include <ascii-chat/ui/help_menu.h>
#include <ascii-chat/ui/viewport.h>
#include <ascii-chat/ui/scrollbar.h>
#include <ascii-chat/ui/text.h>
#include <ascii-chat/ui/table.h>
#include <ascii-chat/ui/input.h>
#include <ascii-chat/ui/buttons.h>
#include <ascii-chat/ui/layout.h>
#include <ascii-chat/ui/progress.h>
#include <ascii-chat/tui/prompt.h>
#include <ascii-chat/ui/fps_counter.h>
#include <ascii-chat/tui/invitation.h>
#include <ascii-chat/tui/terminal_screen.h>
#include <ascii-chat/tui/too_small.h>
#include <ascii-chat/ui/selectable_list.h>
#include <ascii-chat/util/aspect_ratio.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/util/pcre2.h>
#include <ascii-chat/video/anim/test_pattern.h>
#include <ascii-chat/video/anim/digital_rain.h>
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/ascii/simd/cells.h>
#include <ascii-chat/network/consensus/election.h>
#include <ascii-chat/network/consensus/handoff.h>
#include <ascii-chat/network/consensus/metrics.h>
#include <ascii-chat/network/consensus/state.h>
#include <ascii-chat/network/consensus/topology.h>
#include <ascii-chat/network/frame_validator.h>
#include <ascii-chat/network/connection_endpoint.h>
#include <ascii-chat/network/http/server/basic_auth.h>
#include <ascii-chat/network/http/server/server.h>
#include <ascii-chat/network/http/client/client.h>
#include <ascii-chat/network/update_checker.h>
#include <ascii-chat/network/webrtc/turn_credentials.h>
#include <ascii-chat/network/webrtc/turn.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/options/builder.h>
#include <ascii-chat/options/config.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/options/strings.h>
#include <ascii-chat/options/enums.h>
#include <ascii-chat/options/actions.h>
#include <ascii-chat/options/schema.h>
#include <ascii-chat/options/manpage.h>
#include <ascii-chat/options/manpage/parser.h>
#include <ascii-chat/options/parsers.h>
#include <ascii-chat/options/validation.h>
#include <ascii-chat/util/parsing.h>
#include <ascii-chat/audio/analysis.h>
#include <ascii-chat/otel.h>
#include <ascii-chat/tui/input.h>
#include <ascii-chat/tui/session.h>
#include <ascii-chat/video/rgba/video_frame.h>
#include <ascii-chat/video/ascii/output_buffer.h>
#include <common/session/capture.h>
#include <common/session/display.h>
#include <common/session/host.h>
#include <common/session/audio.h>
#include <common/session/participant.h>
#include <common/session/pipeline.h>
#include <common/session/frame_validation.h>
#include <common/session/server_like.h>
#include <ascii-chat/options/runtime.h>
extern uint64_t g_snapshot_last_capture_elapsed_ns;
}
#endif

// =============================================================================
// Test Environment Detection
// =============================================================================

/**
 * @brief Check if running in a headless environment without hardware support
 *
 * Detects if tests are running in an environment without access to hardware
 * devices like webcams. This is useful for skipping hardware-dependent tests
 * in CI, Docker, or WSL environments.
 *
 * Checks for:
 * - CI environment variables (CI=1)
 * - Docker container (/.dockerenv file)
 * - WSL (microsoft/WSL in /proc/version)
 *
 * @return true if running in CI/Docker/WSL (headless), false otherwise
 *
 * @note This function is safe to call on both POSIX and Windows systems.
 *       On Windows, Docker and CI checks still work, but WSL detection
 *       is POSIX-only.
 *
 * @ingroup testing
 *
 * @par Example
 * @code
 * Test(my_suite, webcam_test) {
 *   if (test_is_in_headless_environment()) {
 *     ASCIICHAT_SKIP("Skipping webcam test in headless environment");
 *   }
 *   // ... perform webcam test ...
 * }
 * @endcode
 */
static inline bool test_is_in_headless_environment(void) {
  // Check for CI environment
  if (getenv("CI") != NULL) {
    return true;
  }

  // Check for Docker
  if (access("/.dockerenv", F_OK) == 0) {
    return true;
  }

  // Check for WSL (look for "microsoft" or "WSL" in /proc/version)
  FILE *version_file = fopen("/proc/version", "r");
  if (version_file) {
    char version_buf[TESTS_COMMON_VERSION_BUFFER_SIZE];
    if (fgets(version_buf, sizeof(version_buf), version_file)) {
      if (strstr(version_buf, "microsoft") || strstr(version_buf, "Microsoft") || strstr(version_buf, "WSL")) {
        (void)fclose(version_file);
        return true;
      }
    }
    (void)fclose(version_file);
  }

  return false;
}

// =============================================================================
// Test Binary Path Detection
// =============================================================================

/**
 * @brief Get the path to the ascii-chat binary for integration tests
 *
 * This function finds the ascii-chat binary by trying multiple candidate paths.
 * It handles both direct test invocation from the repo root and ctest invocation
 * from the build directory.
 *
 * Search order:
 * 1. BUILD_DIR environment variable (if set)
 * 2. ./build_docker/bin/ascii-chat (Docker from repo root)
 * 3. ./build/bin/ascii-chat (local from repo root)
 * 4. ./bin/ascii-chat (from build directory - ctest)
 * 5. /app/build_docker/bin/ascii-chat (Docker absolute)
 *
 * @return Path to the ascii-chat binary, or a fallback path if not found
 *
 * @note The returned string is static - do not free it.
 * @note On Windows, returns paths with .exe extension.
 *
 * @ingroup testing
 *
 * @par Example
 * @code
 * Test(my_suite, integration_test) {
 *   const char *binary = test_get_binary_path();
 *   pid_t pid = fork();
 *   if (pid == 0) {
 *     execl(binary, "ascii-chat", "server", "--help", NULL);
 *     exit(127);
 *   }
 *   // ...
 * }
 * @endcode
 */
#ifdef __cplusplus
extern "C" {
#endif
const char *test_get_binary_path(void);
#ifdef __cplusplus
}
#endif

// =============================================================================
// Test Options Helpers (for RCU pattern)
// =============================================================================

#include "../options/rcu.h"

/**
 * @brief Helper to set test_pattern option in tests
 */
static inline void test_set_test_pattern(bool value) {
  options_set_bool("test_pattern", value);
}

/**
 * @brief Helper to set flip_x option in tests
 */
static inline void test_set_flip_x(bool value) {
  options_set_bool("flip_x", value);
}

/**
 * @brief Helper to set flip_y option in tests
 */
static inline void test_set_flip_y(bool value) {
  options_set_bool("flip_y", value);
}

/**
 * @brief Helper to set width option in tests
 */
static inline void test_set_width(unsigned short int value) {
  options_set_int("width", (int)value);
}

/**
 * @brief Helper to set height option in tests
 */
static inline void test_set_height(unsigned short int value) {
  options_set_int("height", (int)value);
}

/** @} */

#ifdef __cplusplus
#define ASCIICHAT_REQUIRE(condition, ...) REQUIRE(static_cast<bool>(condition))
#define ASCIICHAT_CHECK(condition, ...) CHECK(static_cast<bool>(condition))
#define ASCIICHAT_REQUIRE_EQ(lhs, rhs, ...) REQUIRE((lhs) == (rhs))
#define ASCIICHAT_CHECK_EQ(lhs, rhs, ...) CHECK((lhs) == (rhs))
#define ASCIICHAT_REQUIRE_NE(lhs, rhs, ...) REQUIRE((lhs) != (rhs))
#define ASCIICHAT_CHECK_NE(lhs, rhs, ...) CHECK((lhs) != (rhs))
#define ASCIICHAT_CHECK_GT(lhs, rhs, ...) CHECK((lhs) > (rhs))
#define ASCIICHAT_CHECK_GE(lhs, rhs, ...) CHECK((lhs) >= (rhs))
#define ASCIICHAT_CHECK_LT(lhs, rhs, ...) CHECK((lhs) < (rhs))
#define ASCIICHAT_CHECK_LE(lhs, rhs, ...) CHECK((lhs) <= (rhs))
#define ASCIICHAT_REQUIRE_GT(lhs, rhs, ...) REQUIRE((lhs) > (rhs))
#define ASCIICHAT_REQUIRE_GE(lhs, rhs, ...) REQUIRE((lhs) >= (rhs))
#define ASCIICHAT_REQUIRE_LT(lhs, rhs, ...) REQUIRE((lhs) < (rhs))
#define ASCIICHAT_REQUIRE_LE(lhs, rhs, ...) REQUIRE((lhs) <= (rhs))
#define ASCIICHAT_REQUIRE_NOT(condition, ...) REQUIRE(!(condition))
#define ASCIICHAT_REQUIRE_NOT_NULL(value, ...) REQUIRE((value) != nullptr)
#define ASCIICHAT_REQUIRE_NULL(value, ...) REQUIRE((value) == nullptr)
#define ASCIICHAT_CHECK_NOT_NULL(value, ...) CHECK((value) != nullptr)
#define ASCIICHAT_CHECK_NULL(value, ...) CHECK((value) == nullptr)
#define ASCIICHAT_CHECK_NOT(condition, ...) CHECK(!(condition))
#define ASCIICHAT_REQUIRE_STR_EQ(lhs, rhs, ...) REQUIRE(std::strcmp((lhs), (rhs)) == 0)
#define ASCIICHAT_CHECK_STR_EQ(lhs, rhs, ...) CHECK(std::strcmp((lhs), (rhs)) == 0)
#define ASCIICHAT_REQUIRE_STR_NE(lhs, rhs, ...) REQUIRE(std::strcmp((lhs), (rhs)) != 0)
#define ASCIICHAT_CHECK_STR_NE(lhs, rhs, ...) CHECK(std::strcmp((lhs), (rhs)) != 0)
#define ASCIICHAT_REQUIRE_FLOAT_EQ(lhs, rhs, ...) REQUIRE((lhs) == Catch::Approx(rhs))
#define ASCIICHAT_REQUIRE_FLOAT_NE(lhs, rhs, ...) REQUIRE((lhs) != Catch::Approx(rhs))
#define ASCIICHAT_REQUIRE_ARRAY_EQ(lhs, rhs, count, ...) REQUIRE(std::memcmp((lhs), (rhs), (count)) == 0)
#define ASCIICHAT_REQUIRE_STR_EMPTY(value, ...) REQUIRE((value)[0] == '\0')
#define ASCIICHAT_REQUIRE_STR_NOT_EMPTY(value, ...) REQUIRE((value)[0] != '\0')
#define ASCIICHAT_SKIP(...) SKIP("test skipped")
#define ASCIICHAT_ASSUME(condition)                                                                                                                                                                    \
  do {                                                                                                                                                                                                 \
    if (!(condition))                                                                                                                                                                                  \
      SKIP("assumption not satisfied");                                                                                                                                                                \
  } while (false)
#define ASCIICHAT_FAIL(...) FAIL(__VA_ARGS__)
#define ASCIICHAT_INFO(...) INFO(__VA_ARGS__)
#define ASCIICHAT_WARN(...) WARN(__VA_ARGS__)
#define ASCIICHAT_ERROR(...) INFO(__VA_ARGS__)
#endif
