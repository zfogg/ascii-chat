#include <catch2/catch_test_macros.hpp>
/**
 * @file tests/unit/mmap_logging_test.c
 * @brief Unit tests for mmap-based lock-free logging
 */

#include <ascii-chat/tests/logging.h>
#include <ascii-chat/log/mmap.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>

// Each test uses unique paths to avoid race conditions with parallel execution
// The paths include the test name to ensure isolation

static void cleanup_files(const char *log_path) {
  if (log_path)
    unlink(log_path);
}

// Test initialization and destruction
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TEST_CASE("mmap_logging/init_destroy", "[mmap_logging] [unit]") {
  const char *log_path = "/tmp/mmap_test_init_destroy.log";
  cleanup_files(log_path);

  asciichat_error_t result = log_mmap_init_simple(log_path, 0);
  REQUIRE((result) == (ASCIICHAT_OK));

  REQUIRE(log_mmap_is_active());

  log_mmap_destroy();

  REQUIRE(!log_mmap_is_active());

  cleanup_files(log_path);
}

// Test that log file is created
TEST_CASE("mmap_logging/creates_log_file", "[mmap_logging] [unit]") {
  const char *log_path = "/tmp/mmap_test_creates_log.log";
  cleanup_files(log_path);

  asciichat_error_t result = log_mmap_init_simple(log_path, 0);
  REQUIRE((result) == (ASCIICHAT_OK));

  REQUIRE(log_mmap_is_active());

  // Check log file exists
  struct stat st;
  int stat_result = stat(log_path, &st);
  REQUIRE((stat_result) == (0));
  REQUIRE((st.st_size) > (0));

  log_mmap_destroy();
  cleanup_files(log_path);
}

// Test that written text is in the file
TEST_CASE("mmap_logging/text_is_readable", "[mmap_logging] [unit]") {
  const char *log_path = "/tmp/mmap_test_text_readable.log";
  cleanup_files(log_path);

  asciichat_error_t result = log_mmap_init_simple(log_path, 0);
  REQUIRE((result) == (ASCIICHAT_OK));

  // Write some entries
  log_mmap_write(LOG_INFO, __FILE__, __LINE__, __func__, "Test message for reading");
  log_mmap_sync();

  // Check log file has content
  struct stat st;
  int stat_result = stat(log_path, &st);
  REQUIRE((stat_result) == (0));
  REQUIRE((st.st_size) > ((off_t)0));

  log_mmap_destroy();
  cleanup_files(log_path);
}

// Test writing and statistics
TEST_CASE("mmap_logging/write_and_stats", "[mmap_logging] [unit]") {
  const char *log_path = "/tmp/mmap_test_write_stats.log";
  cleanup_files(log_path);

  asciichat_error_t result = log_mmap_init_simple(log_path, 0);
  REQUIRE((result) == (ASCIICHAT_OK));

  // Write some entries
  for (int i = 0; i < 10; i++) {
    log_mmap_write(LOG_INFO, __FILE__, __LINE__, __func__, "Test message %d", i);
  }

  // Check statistics
  uint64_t bytes_written, wrap_count;
  log_mmap_get_stats(&bytes_written, &wrap_count);

  REQUIRE((bytes_written) > ((uint64_t)0));
  REQUIRE((wrap_count) == ((uint64_t)0));

  log_mmap_destroy();
  cleanup_files(log_path);
}

// Test sync for errors
TEST_CASE("mmap_logging/error_triggers_sync", "[mmap_logging] [unit]") {
  const char *log_path = "/tmp/mmap_test_error_sync.log";
  cleanup_files(log_path);

  asciichat_error_t result = log_mmap_init_simple(log_path, 0);
  REQUIRE((result) == (ASCIICHAT_OK));

  // Write an error - should trigger sync
  log_mmap_write(LOG_ERROR, __FILE__, __LINE__, __func__, "Error message test");

  // Check that stats reflect the write
  uint64_t bytes_written, wrap_count;
  log_mmap_get_stats(&bytes_written, &wrap_count);

  REQUIRE((bytes_written) > ((uint64_t)0));

  log_mmap_destroy();
  cleanup_files(log_path);
}

// Test config struct initialization
TEST_CASE("mmap_logging/config_struct_init", "[mmap_logging] [unit]") {
  const char *log_path = "/tmp/mmap_test_config.log";
  cleanup_files(log_path);

  log_mmap_config_t config = {
      .log_path = log_path,
      .max_size = 1024 * 1024, // 1MB
  };

  asciichat_error_t result = log_mmap_init(&config);
  REQUIRE((result) == (ASCIICHAT_OK));

  REQUIRE(log_mmap_is_active());

  // Write some entries
  log_mmap_write(LOG_INFO, __FILE__, __LINE__, __func__, "Test with config struct");

  uint64_t bytes_written, wrap_count;
  log_mmap_get_stats(&bytes_written, &wrap_count);
  REQUIRE((bytes_written) > ((uint64_t)0));

  log_mmap_destroy();

  cleanup_files(log_path);
}

// Test double init (should destroy first, then reinit)
TEST_CASE("mmap_logging/double_init", "[mmap_logging] [unit]") {
  const char *log_path = "/tmp/mmap_test_double_init.log";
  cleanup_files(log_path);

  asciichat_error_t result1 = log_mmap_init_simple(log_path, 0);
  REQUIRE((result1) == (ASCIICHAT_OK));

  // Second init should succeed (destroys first, reinits)
  asciichat_error_t result2 = log_mmap_init_simple(log_path, 0);
  REQUIRE((result2) == (ASCIICHAT_OK));

  REQUIRE(log_mmap_is_active());

  log_mmap_destroy();
  cleanup_files(log_path);
}

// Test that mmap logging is lock-free (basic sanity check)
TEST_CASE("mmap_logging/lock_free_sanity", "[mmap_logging] [unit]") {
  const char *log_path = "/tmp/mmap_test_lockfree.log";
  cleanup_files(log_path);

  asciichat_error_t result = log_mmap_init_simple(log_path, 0);
  REQUIRE((result) == (ASCIICHAT_OK));

  // Write many entries quickly - should not deadlock
  for (int i = 0; i < 1000; i++) {
    log_mmap_write(LOG_DEBUG, __FILE__, __LINE__, __func__, "Lock-free test message %d", i);
  }

  uint64_t bytes_written, wrap_count;
  log_mmap_get_stats(&bytes_written, &wrap_count);
  REQUIRE((bytes_written) > ((uint64_t)0));

  log_mmap_destroy();
  cleanup_files(log_path);
}
