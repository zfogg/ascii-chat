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

#include "common.h"
#include "metadata.h"

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
     "",
     0,
     "Set FILE as path for log file.",
     "LOGGING",
     "FILE",
     false,
     "ASCII_CHAT_LOG_FILE",
     NULL,
     parse_log_file,
     false,
     false,
     OPTION_MODE_BINARY,
     {0}},
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
      .enum_count = 6,
      .enum_descriptions = g_log_level_descs,
      .enum_integer_values = g_log_level_integers,
      .input_type = OPTION_INPUT_ENUM}},
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
     {0}},
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
     {0}},
    {"grep",
     '\0',
     OPTION_TYPE_STRING,
     offsetof(options_t, grep_pattern),
     NULL, // Default is empty string - not meaningful to display in help
     sizeof(char *),
     "Filter console logs with perl compatible regular expressions or fixed strings. Format: /pattern/flags. "
     "Flags: i(case-insensitive), m(multiline), s(dotall), x(extended), g(global highlight), "
     "I(invert match), F(fixed string), A<n>(n lines after), B<n>(n lines before), C<n>(n lines of context). "
     "Examples: '/error/i', '/panic/C5', '/buffer pool/Fg', '/server/giB2A5'. Multiple --grep allowed (OR).",
     "LOGGING",
     "PATTERN",
     false,
     "ASCII_CHAT_GREP",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_BINARY,
     {.examples = (const char *[]){"ERROR", "client\\.c", "network.*failed", NULL}}},
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
     {.input_type = OPTION_INPUT_STRING}},

    REGISTRY_TERMINATOR()};
