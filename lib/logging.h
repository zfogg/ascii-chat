#pragma once

#include "platform/mutex.h"
#include "platform/terminal.h"

/* Logging levels */
typedef enum { LOG_DEBUG = 0, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL, LOG_DEV } log_level_t;

#ifdef NDEBUG
#define DEFAULT_LOG_LEVEL LOG_INFO /* Release build: INFO and above */
#else
#define DEFAULT_LOG_LEVEL LOG_DEBUG /* Debug build: DEBUG and above */
#endif

#define MAX_LOG_SIZE (3 * 1024 * 1024) /* 3MB max log file size */

static struct {
  int file;
  log_level_t level;
  mutex_t mutex;
  bool initialized;
  char filename[256];           /* Store filename for rotation */
  size_t current_size;          /* Track current file size */
  bool terminal_output_enabled; /* Control stderr output to terminal */
  bool level_manually_set;      /* Track if level was set manually */
  bool env_checked;             /* Track if we've checked environment */
} g_log = {.file = 0,
           .level = DEFAULT_LOG_LEVEL,
           .initialized = false,
           .filename = {0},
           .current_size = 0,
           .terminal_output_enabled = true,
           .level_manually_set = false,
           .env_checked = false};

static const char *level_strings[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL", "DEV"};

// 256-color palette colors
static const char *level_colors_256[] = {
    "\x1b[96m", /* DEBUG: Bright Cyan */
    "\x1b[92m", /* INFO: Bright Green */
    "\x1b[33m", /* WARN: Yellow */
    "\x1b[31m", /* ERROR: Red */
    "\x1b[35m", /* FATAL: Magenta */
    "\x1b[94m", /* DEV: Bright Blue */
    "\x1b[0m",  /* Reset */
};

// Truecolor RGB colors
static const char *level_colors_truecolor[] = {
    "\x1b[38;2;30;205;255m",   /* DEBUG: Cyan */
    "\x1b[38;2;50;205;100m",   /* INFO: Green */
    "\x1b[38;2;205;185;40m",   /* WARN: Yellow */
    "\x1b[38;2;205;30;100m",   /* ERROR: Red */
    "\x1b[38;2;205;80;205m",   /* FATAL: Magenta */
    "\x1b[38;2;150;100;205m",  /* DEV: Blue */
    "\x1b[0m",                 /* Reset */
};

// Legacy 16-color palette colors (fallback)
static const char *level_colors_16[] = {
    "\x1b[36m", /* DEBUG: Cyan */
    "\x1b[32m", /* INFO: Green */
    "\x1b[33m", /* WARN: Yellow */
    "\x1b[31m", /* ERROR: Red */
    "\x1b[35m", /* FATAL: Magenta */
    "\x1b[34m", /* DEV: Blue */
    "\x1b[0m",  /* Reset */
};

/* Color names enum for better readability */
typedef enum {
  COLOR_CYAN = 0,
  COLOR_GREEN = 1,
  COLOR_YELLOW = 2,
  COLOR_RED = 3,
  COLOR_MAGENTA = 4,
  COLOR_BLUE = 5,
  COLOR_RESET = 6
} color_t;

/* Logging-specific color enum that maps to color_t values */
typedef enum {
  LOGGING_COLOR_DEBUG = COLOR_CYAN,
  LOGGING_COLOR_INFO = COLOR_GREEN,
  LOGGING_COLOR_WARN = COLOR_YELLOW,
  LOGGING_COLOR_ERROR = COLOR_RED,
  LOGGING_COLOR_FATAL = COLOR_MAGENTA,
  LOGGING_COLOR_DEV = COLOR_BLUE,
  LOGGING_COLOR_RESET = COLOR_RESET
} logging_color_t;

void log_init(const char *filename, log_level_t level);
void log_destroy(void);
void log_set_level(log_level_t level);
log_level_t log_get_level(void);            /* Get current log level */
void log_set_terminal_output(bool enabled); /* Control stderr output to terminal */
bool log_get_terminal_output(void);         /* Get current terminal output setting */
void log_truncate_if_large(void);           /* Manually truncate large log files */
void log_msg(log_level_t level, const char *file, int line, const char *func, const char *fmt, ...);
void log_plain_msg(const char *fmt, ...); /* Plain logging without timestamps or levels */
void log_file_msg(const char *fmt, ...);  /* Log to file only, no stderr output */

/* Helper function to get color string for a given color enum */
const char *log_level_color(logging_color_t color);

/* Get the appropriate color array based on terminal capabilities */
const char **log_get_color_array(void);

/* Re-detect terminal capabilities after logging is initialized */
void log_redetect_terminal_capabilities(void);

/* Logging macros */
#define log_debug(...) log_msg(LOG_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_info(...) log_msg(LOG_INFO, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_warn(...) log_msg(LOG_WARN, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_error(...) log_msg(LOG_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_fatal(...) log_msg(LOG_FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__)

/* Plain logging - writes to both log file and stderr without timestamps or log levels */
#define log_plain(...) log_plain_msg(__VA_ARGS__)

/* File-only logging - writes to log file only, no stderr output */
#define log_file(...) log_file_msg(__VA_ARGS__)
