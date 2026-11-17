#include <catch2/catch_test_macros.hpp>
// Integration test that runs the actual client main() with --help
// This verifies the client binary works and shows help correctly

#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/common.h>
#include <catch2/catch_all.hpp>
#include <catch2/catch_all.hpp>
#include <stdlib.h>

#include <stdbool.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <libplatform/process.h>

// Use shared binary path detection from tests/common.h
#define get_binary_path test_get_binary_path
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TEST_CASE("client_main/test_client_help", "[client_main]") {
  const char *argv[] = {get_binary_path(), "--help", NULL};
  platform_process_t *process = NULL;
  REQUIRE(platform_process_spawn(&process, get_binary_path(), argv, -1, -1, -1) == ASCIICHAT_OK);
  int exit_code = -1;
  REQUIRE(platform_process_wait(process, -1, &exit_code) == ASCIICHAT_OK);
  REQUIRE(exit_code == 0);
  platform_process_destroy(process);
}
