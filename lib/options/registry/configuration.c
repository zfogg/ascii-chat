/**
 * @file configuration.c
 * @brief Configuration file options
 * @ingroup options
 *
 * Options for loading configuration from TOML files and creating
 * default configuration templates.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <ascii-chat/options/registry/common.h>

// ============================================================================
// CONFIGURATION CATEGORY - Configuration file options
// ============================================================================
const registry_entry_t g_configuration_entries[] = {
    // CONFIGURATION GROUP (binary-level)
    {"config",
     '\0',
     OPTION_TYPE_STRING,
     offsetof(options_t, config_file),
     "",
     0,
     "Load configuration from toml FILE.",
     "CONFIGURATION",
     "FILE",
     false,
     NULL,
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_BINARY,
     {0}},
    {"config-create",
     '\0',
     OPTION_TYPE_STRING,
     0,
     NULL,
     0,
     "Create default config file and write to FILE or stdout if not specified.",
     "CONFIGURATION",
     "[FILE]",
     false,
     NULL,
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_BINARY,
     {0}},
    {"completions",
     '\0',
     OPTION_TYPE_STRING,
     0,
     NULL,
     0,
     "Generate shell completions (bash, fish, zsh, powershell) and output to stdout (or optional file path).",
     "CONFIGURATION",
     "SHELL [FILE]",
     false,
     NULL,
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_BINARY,
     {0}},
    {"man-page-create",
     '\0',
     OPTION_TYPE_STRING,
     0,
     NULL,
     0,
     "Generate man page and write to FILE or stdout if not specified.",
     "CONFIGURATION",
     "[FILE]",
     false,
     NULL,
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_BINARY,
     {0}},

    REGISTRY_TERMINATOR()};
