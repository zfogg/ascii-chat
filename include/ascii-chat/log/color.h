#pragma once

/**
 * Color values used by the logging and string-colorization APIs.
 */
typedef enum log_color_t {
  LOG_COLOR_DEV = 0,
  LOG_COLOR_DEBUG = 1,
  LOG_COLOR_INFO = 2,
  LOG_COLOR_WARN = 3,
  LOG_COLOR_ERROR = 4,
  LOG_COLOR_FATAL = 5,
  LOG_COLOR_GREY = 6,
  LOG_COLOR_RESET = 7,
  LOG_COLOR_WHITE = 8
} log_color_t;
