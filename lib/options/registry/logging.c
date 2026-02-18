/**
 * @file logging.c
 * @brief Logging category options
 * @ingroup options
 *
 * Binary-level logging and output control options including log files,
 * verbosity levels, and grep filtering.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <ascii-chat/options/registry/common.h>
#include <ascii-chat/options/registry/metadata.h>
#include <ascii-chat/options/registry/mode_defaults.h>
#include <ascii-chat/options/parsers.h>

// ============================================================================
// LOGGING CATEGORY - Binary-level logging options
// ============================================================================
const registry_entry_t g_logging_entries[] = {
    // LOGGING GROUP (binary-level)
    {"log-file",
     'L',
     OPTION_TYPE_CALLBACK,
     offsetof(options_t, log_file),
     NULL, // Use mode_default_getter instead
     0,
     "Set FILE as path for log file (default: /tmp/ascii-chat-<mode>.log or Windows temp dir).",
     "LOGGING",
     "FILE",
     false,
     "ASCII_CHAT_LOG_FILE",
     NULL,
     parse_log_file,
     false,
     false,
     OPTION_MODE_BINARY,
     {0},
     get_default_log_file},
    {"log-level",
     '\0',
     OPTION_TYPE_CALLBACK,
     offsetof(options_t, log_level),
     &default_log_level_value,
     sizeof(log_level_t),
     "Set log level: dev, debug, info, warn, error, fatal. Logs at or above this level are written.",
     "LOGGING",
     NULL,
     false,
     "ASCII_CHAT_LOG_LEVEL",
     NULL,
     parse_log_level,
     false,
     false,
     OPTION_MODE_BINARY,
     {.enum_values = g_log_level_values,
      .enum_descriptions = g_log_level_descs,
      .enum_integer_values = g_log_level_integers,
      .input_type = OPTION_INPUT_ENUM},
     NULL},
    {"verbose",
     'V',
     OPTION_TYPE_CALLBACK,
     offsetof(options_t, verbose_level),
     0,
     sizeof(unsigned short int),
     "Increase log verbosity (stackable: -VV, -VVV).",
     "LOGGING",
     NULL,
     false,
     "ASCII_CHAT_VERBOSE",
     NULL,
     parse_verbose_flag,
     false,
     true,
     OPTION_MODE_BINARY,
     {0},
     NULL},
    {"quiet",
     'q',
     OPTION_TYPE_BOOL,
     offsetof(options_t, quiet),
     &default_quiet_value,
     sizeof(bool),
     "Disable console logging (log to file only).",
     "LOGGING",
     NULL,
     false,
     "ASCII_CHAT_QUIET",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_BINARY,
     {0},
     NULL},
    {"grep",
     '\0',
     OPTION_TYPE_STRING,
     offsetof(options_t, grep_pattern),
     NULL, // Default is empty string - not meaningful to display in help
     sizeof(char *),
     "Filter console logs with perl compatible regular expressions or fixed strings. "
     "Format 1: /pattern/flags (with flags). Format 2: pattern (plain regex, no flags). "
     "Flags (format 1 only): i(case-insensitive), m(multiline), s(dotall), x(extended), g(global highlight), "
     "I(invert match), F(fixed string), A<n>(n lines after), B<n>(n lines before), C<n>(n lines of context). "
     "Examples: '/error/i', 'error', '/panic/C5', 'warn|error', '/buffer pool/Fg'. Multiple --grep allowed (OR).",
     "LOGGING",
     "PATTERN",
     false,
     "ASCII_CHAT_GREP",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_BINARY,
     {.examples = (const char *[]){"ERROR", "client\\.c", "network.*failed", NULL}},
     NULL},
    {"color-scheme",
     '\0',
     OPTION_TYPE_STRING,
     offsetof(options_t, color_scheme_name),
     OPT_COLOR_SCHEME_NAME_DEFAULT,
     sizeof(((options_t *)0)->color_scheme_name),
     "Color scheme for logging output. Built-in schemes: pastel, nord, solarized, dracula, gruvbox, "
     "monokai, base16-default. All schemes with light variants auto-adapt to terminal background.",
     "LOGGING",
     "NAME",
     false,
     "ASCII_CHAT_COLOR_SCHEME",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_BINARY,
     {.input_type = OPTION_INPUT_STRING},
     NULL},
    {"log-format",
     '\0',
     OPTION_TYPE_CALLBACK,
     offsetof(options_t, log_format_output),
     NULL,
     sizeof(log_format_output_t),
     "Set log output format: text (human-readable, default) or json (machine-readable NDJSON).",
     "LOGGING",
     "FORMAT",
     false,
     "ASCII_CHAT_LOG_FORMAT",
     NULL,
     parse_log_format_output,
     false,
     false,
     OPTION_MODE_BINARY,
     {.enum_values = g_log_format_output_values,
      .enum_descriptions = g_log_format_output_descs,
      .enum_integer_values = g_log_format_output_integers,
      .input_type = OPTION_INPUT_ENUM},
     NULL},
    {"log-template",
     '\0',
     OPTION_TYPE_STRING,
     offsetof(options_t, log_template),
     NULL,
     sizeof(((options_t *)0)->log_template),
     "Custom log format string. Format specifiers: %time(fmt) - time with strftime format (see 'man 3 strftime' for "
     "codes like %Y, %m, %d, %H, %M, %S); "
     "%level - log level (DEV/DEBUG/INFO/WARN/ERROR/FATAL); %level_aligned - level padded to 5 chars; "
     "%file - full file path; %file_relative - file path relative to project root; %line - line number; "
     "%func - function name; %tid - thread ID; %message - log message; "
     "%color(LEVEL, text) - colorize text using LEVEL's color from current scheme (e.g., %color(INFO, %tid)). "
     "Example: '[%time(%Y-%m-%d %H:%M:%S)] [%level_aligned] %file_relative:%line %message'. "
     "Escape %% for literal %, \\\\ for literal backslash. "
     "Default: release mode '[%time(%H:%M:%S)] [%level_aligned] %message' or debug mode '[%time(%H:%M:%S)] "
     "[%level_aligned] [tid:%tid] %file_relative:%line in %func(): %message'.",
     "LOGGING",
     "TEMPLATE",
     false,
     "ASCII_CHAT_LOG_TEMPLATE",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_BINARY,
     {.input_type = OPTION_INPUT_STRING},
     NULL},
    {"log-format-console",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, log_format_console_only),
     NULL,
     sizeof(bool),
     "Apply custom log template (--log-template) only to console output (file logs use default format). See "
     "--log-template "
     "for supported format specifiers.",
     "LOGGING",
     NULL,
     false,
     "ASCII_CHAT_LOG_FORMAT_CONSOLE",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_BINARY,
     {0},
     NULL},

    REGISTRY_TERMINATOR()};
