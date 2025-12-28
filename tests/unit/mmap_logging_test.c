/**
 * @file tests/unit/mmap_logging_test.c
 * @brief Unit tests for mmap-based logging
 */

#include "tests/common.h"
#include "log/mmap.h"
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>

// Each test uses unique paths to avoid race conditions with parallel execution
// The paths include the test name to ensure isolation

static void cleanup_files(const char *mmap_path, const char *text_path) {
  if (mmap_path) unlink(mmap_path);
  if (text_path) unlink(text_path);
}

// Test initialization and destruction
Test(mmap_logging, init_destroy) {
  const char *mmap_path = "/tmp/mmap_test_init_destroy.mmap";
  const char *text_path = "/tmp/mmap_test_init_destroy.txt";
  cleanup_files(mmap_path, text_path);

  asciichat_error_t result = log_mmap_init_simple(mmap_path, text_path);
  cr_assert_eq(result, ASCIICHAT_OK, "Failed to initialize mmap logging");

  cr_assert(log_mmap_is_active(), "Mmap logging should be active after init");

  log_mmap_destroy();

  cr_assert(!log_mmap_is_active(), "Mmap logging should not be active after destroy");

  cleanup_files(mmap_path, text_path);
}

// Test that mmap file is created
Test(mmap_logging, creates_mmap_file) {
  const char *mmap_path = "/tmp/mmap_test_creates_mmap.mmap";
  const char *text_path = "/tmp/mmap_test_creates_mmap.txt";
  cleanup_files(mmap_path, text_path);

  asciichat_error_t result = log_mmap_init_simple(mmap_path, text_path);
  cr_assert_eq(result, ASCIICHAT_OK, "log_mmap_init_simple failed with error %d", result);

  cr_assert(log_mmap_is_active(), "Mmap logging should be active after successful init");

  // Check mmap file exists
  struct stat st;
  int stat_result = stat(mmap_path, &st);
  cr_assert_eq(stat_result, 0, "Mmap file should exist after init at %s (errno=%d)", mmap_path, errno);
  cr_assert_gt(st.st_size, 0, "Mmap file should have non-zero size");

  log_mmap_destroy();
  cleanup_files(mmap_path, text_path);
}

// Test that text log file is created
Test(mmap_logging, creates_text_log_file) {
  const char *mmap_path = "/tmp/mmap_test_creates_text.mmap";
  const char *text_path = "/tmp/mmap_test_creates_text.txt";
  cleanup_files(mmap_path, text_path);

  asciichat_error_t result = log_mmap_init_simple(mmap_path, text_path);
  cr_assert_eq(result, ASCIICHAT_OK);

  // Text log file should exist immediately after init (created by open() with O_CREAT)
  struct stat st;
  int stat_result = stat(text_path, &st);
  cr_assert_eq(stat_result, 0, "Text log file should exist immediately after init (errno=%d)", errno);

  // Write some entries and flush
  log_mmap_write(LOG_INFO, __FILE__, __LINE__, __func__, "Test message");
  log_mmap_flush();

  // Give flusher thread time to complete
  usleep(100000); // 100ms

  // Check text log file has content
  stat_result = stat(text_path, &st);
  cr_assert_eq(stat_result, 0, "Text log file should exist after flush");
  cr_assert_gt(st.st_size, 0, "Text log file should have non-zero size after flush");

  log_mmap_destroy();
  cleanup_files(mmap_path, text_path);
}

// Test writing and statistics
Test(mmap_logging, write_and_stats) {
  const char *mmap_path = "/tmp/mmap_test_write_stats.mmap";
  const char *text_path = "/tmp/mmap_test_write_stats.txt";
  cleanup_files(mmap_path, text_path);

  asciichat_error_t result = log_mmap_init_simple(mmap_path, text_path);
  cr_assert_eq(result, ASCIICHAT_OK);

  // Write some entries
  for (int i = 0; i < 10; i++) {
    log_mmap_write(LOG_INFO, __FILE__, __LINE__, __func__, "Test message %d", i);
  }

  // Check statistics
  uint64_t total, flushed, dropped;
  log_mmap_get_stats(&total, &flushed, &dropped);

  cr_assert_eq(total, 10, "Should have 10 total entries");
  cr_assert_eq(dropped, 0, "Should have no dropped entries");

  log_mmap_destroy();
  cleanup_files(mmap_path, text_path);
}

// Test immediate flush for errors
Test(mmap_logging, error_immediate_flush) {
  const char *mmap_path = "/tmp/mmap_test_error_flush.mmap";
  const char *text_path = "/tmp/mmap_test_error_flush.txt";
  cleanup_files(mmap_path, text_path);

  asciichat_error_t result = log_mmap_init_simple(mmap_path, text_path);
  cr_assert_eq(result, ASCIICHAT_OK);

  // Write an error - should trigger immediate flush
  log_mmap_write(LOG_ERROR, __FILE__, __LINE__, __func__, "Error message test");

  // Small delay to ensure flush completes
  usleep(50000); // 50ms

  // Check that it was flushed
  uint64_t total, flushed, dropped;
  log_mmap_get_stats(&total, &flushed, &dropped);

  cr_assert_eq(total, 1, "Should have 1 total entry");
  cr_assert_geq(flushed, 1, "Error entry should be flushed immediately");

  log_mmap_destroy();
  cleanup_files(mmap_path, text_path);
}

// Test NULL text log path (no flusher thread)
Test(mmap_logging, no_text_log) {
  const char *mmap_path = "/tmp/mmap_test_no_text.mmap";
  const char *text_path = "/tmp/mmap_test_no_text.txt";
  cleanup_files(mmap_path, text_path);

  log_mmap_config_t config = {
      .mmap_path = mmap_path,
      .text_log_path = NULL, // No text log
      .entry_count = 1024,
      .flush_interval_ms = 100,
      .immediate_error_flush = false,
  };

  asciichat_error_t result = log_mmap_init(&config);
  cr_assert_eq(result, ASCIICHAT_OK);

  cr_assert(log_mmap_is_active());

  // Write some entries
  log_mmap_write(LOG_INFO, __FILE__, __LINE__, __func__, "Test without text log");

  uint64_t total, flushed, dropped;
  log_mmap_get_stats(&total, &flushed, &dropped);
  cr_assert_eq(total, 1);

  log_mmap_destroy();

  // Text log should not exist
  struct stat st;
  int stat_result = stat(text_path, &st);
  cr_assert_neq(stat_result, 0, "Text log file should not exist when path is NULL");

  cleanup_files(mmap_path, NULL);
}

// Test double init (should warn and reinit)
Test(mmap_logging, double_init) {
  const char *mmap_path = "/tmp/mmap_test_double_init.mmap";
  const char *text_path = "/tmp/mmap_test_double_init.txt";
  cleanup_files(mmap_path, text_path);

  asciichat_error_t result1 = log_mmap_init_simple(mmap_path, text_path);
  cr_assert_eq(result1, ASCIICHAT_OK);

  // Second init should succeed (destroys first, reinits)
  asciichat_error_t result2 = log_mmap_init_simple(mmap_path, text_path);
  cr_assert_eq(result2, ASCIICHAT_OK);

  cr_assert(log_mmap_is_active());

  log_mmap_destroy();
  cleanup_files(mmap_path, text_path);
}
