/**
 * @file lib/log/asan.c
 * @brief AddressSanitizer error reporting integration with logging system
 * @ingroup log
 *
 * Integrates ASan error reports into the ascii-chat logging system so that
 * memory errors are captured in log files and respect --grep filtering,
 * rather than spamming stderr and corrupting terminal rendering.
 *
 * Uses __asan_set_error_report_callback() to intercept ASan reports before
 * they hit stderr. Reports are fed through log_fatal() which respects:
 * - --log-file (writes to file instead of stdout/stderr)
 * - --grep (filters error messages)
 * - Proper formatting without terminal corruption
 *
 * CONSTRUCTOR TIMING:
 * The __attribute__((constructor)) runs before main(), ensuring ASan reports
 * are captured from the very first allocation.
 *
 * SAFETY:
 * The callback is intentionally minimal to avoid deadlocks. ASan calls this
 * callback when memory is corrupted, so:
 * - No mutex locks (deadlock risk)
 * - No complex operations
 * - Just pass the report string to logging
 *
 * The actual logging (log_fatal) may be called in an unsafe state, but at
 * that point the process is about to abort anyway, so best effort is fine.
 */

#include <ascii-chat/log/log.h>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HAS_ASAN 1
#endif
#endif

#ifdef HAS_ASAN
#include <sanitizer/asan_interface.h>

/**
 * @brief ASan error report callback - feeds memory errors into logging system
 *
 * Called by AddressSanitizer when it detects memory errors. The report string
 * contains the full formatted ASan error message with stack traces.
 *
 * This callback is called BEFORE ASan aborts, so we can capture the full
 * report in the log file for post-mortem debugging.
 *
 * @param report Full ASan error report as a formatted string
 *
 * @note This function is called when memory is corrupted, so we keep it
 *       simple and don't use mutexes or complex operations.
 * @note The callback is called before process abort, so best-effort logging
 *       is acceptable even if the process state is partially corrupted.
 */
static void asan_error_report_callback(const char *report) {
  if (!report) {
    return;
  }

  // Feed the entire report through the logging system
  // This respects --log-file, --grep, and other logging filters
  log_fatal("AddressSanitizer detected memory error:\n%s", report);

  // Note: log_fatal will abort the process, so we won't return from here
}

/**
 * @brief Initialize ASan error reporting (runs before main)
 *
 * This constructor function is called before main() starts, ensuring ASan
 * reports are captured from the first allocation onward.
 *
 * The __attribute__((constructor)) attribute ensures this runs during
 * dynamic library initialization, before any user code executes.
 */
__attribute__((constructor))
static void init_asan_error_reporting(void) {
  __asan_set_error_report_callback(asan_error_report_callback);
}

#endif  // HAS_ASAN
