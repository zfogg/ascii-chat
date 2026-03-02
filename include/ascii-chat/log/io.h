/**
 * @file log/io.h
 * @brief Capture stdout/stderr and redirect to logging system
 *
 * Provides functions and macros to capture stdout and stderr output
 * and redirect it to the logging system instead of suppressing it.
 *
 * Usage:
 *   // Capture specific code:
 *   log_io_t capture = log_io_start();
 *   ffmpeg_function_that_prints_stuff();
 *   log_io_stop(capture, "ffmpeg");
 *
 *   // Capture a code block (recommended):
 *   LOG_IO("ffmpeg", {
 *       ffmpeg_function_that_prints_stuff();
 *   });
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle for IO capture state
 */
typedef struct {
  int saved_stdout_fd;  ///< Saved stdout file descriptor
  int saved_stderr_fd;  ///< Saved stderr file descriptor
  int pipe_fd;          ///< Read end of pipe for captured output
} log_io_t;

/**
 * @brief Start capturing stdout and stderr
 *
 * Redirects both stdout and stderr to a pipe so their output can be
 * captured and logged. Call log_io_stop() to end capture and
 * log the accumulated output.
 *
 * @return Capture handle (use with log_io_stop)
 *
 * @note If capture fails, returns a handle with saved_stdout_fd = -1
 * @note Do not use stdout/stderr directly while capturing
 */
log_io_t log_io_start(void);

/**
 * @brief Stop capturing and log the accumulated output
 *
 * Restores stdout and stderr to their original state, reads all captured
 * output from the pipe, and logs it at INFO level with the given prefix.
 *
 * @param capture Capture handle returned by log_io_start()
 * @param prefix Prefix to use in log messages (e.g., "ffmpeg", "portaudio")
 *
 * @note Safe to call with invalid handle (saved_stdout_fd = -1)
 * @note After calling this, the capture handle is invalidated
 */
void log_io_stop(log_io_t capture, const char *prefix);

/**
 * @brief Capture output from a code block and log it
 *
 * This macro redirects stdout and stderr for the duration of the
 * code block, then logs any captured output with the given prefix.
 *
 * Example:
 *   LOG_IO("ffmpeg", {
 *       av_log_set_callback(NULL);  // Use default ffmpeg logging
 *       process_file("input.mp4");
 *   });
 *
 * @param prefix Prefix for log messages (const char *)
 * @param block Code block to capture (must be valid C statement block)
 *
 * @note Prefix and block are only evaluated once
 * @note Variables in the enclosing scope are accessible in the block
 */
#define LOG_IO(prefix, block)                                                 \
  do {                                                                        \
    log_io_t _log_io = log_io_start();                                       \
    block;                                                                    \
    log_io_stop(_log_io, (prefix));                                         \
  } while (0)

#ifdef __cplusplus
}
#endif
