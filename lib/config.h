#pragma once

/**
 * @defgroup config Config Module
 * @ingroup module_core
 * @brief The toml text file configuration file module.
 *
 * @file config.h
 * @ingroup config
 * @brief TOML configuration file support for ascii-chat
 *
 * This module provides functionality for loading configuration from TOML files
 * (typically located at ~/.ascii-chat/config.toml). Configuration values are
 * applied to global options, but command-line arguments always take precedence
 * over config file values.
 *
 * The interface provides:
 * - TOML file parsing and validation
 * - Automatic configuration loading from standard location
 * - Non-fatal error handling (missing or malformed config files are ignored)
 * - Support for network, client, palette, crypto, and logging configuration
 *
 * @note Configuration Priority: Command-line arguments override config file values.
 *       Config file values override default values. The config file is loaded
 *       before CLI argument parsing to ensure this precedence.
 *
 * @note Configuration File Location: The config file is loaded from
 *       `~/.ascii-chat/config.toml` (or `%USERPROFILE%\.ascii-chat\config.toml`
 *       on Windows). The `~` is expanded using platform-specific path expansion.
 *
 * @note Error Handling: Config file parsing errors are non-fatal. If the file
 *       is missing, malformed, or contains invalid values, warnings are printed
 *       to stderr and the application continues with default values. Invalid
 *       individual values are skipped with warnings, but valid values are still
 *       applied.
 *
 * @note Validation: All configuration values are validated using the same
 *       validation functions used by CLI argument parsing, ensuring consistency
 *       between config file and CLI option handling.
 *
 * @warning Password Storage: While passwords can be stored in the config file
 *          (via `crypto.password`), this is strongly discouraged for security
 *          reasons. A warning is printed if a password is found in the config file.
 *          Use CLI `--password` or environment variables instead.
 *
 * @warning File Permissions: Users should secure their config file to prevent
 *          unauthorized access, especially if it contains sensitive information
 *          like encryption keys or passwords.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stdbool.h>
#include "common.h"

/**
 * @brief Load configuration from TOML file and apply to global options
 * @param is_client `true` if loading client configuration, `false` for server configuration
 * @param config_path Optional path to config file (NULL uses default location)
 * @param strict If true, errors are fatal; if false, errors are non-fatal warnings
 * @return ASCIICHAT_OK on success, error code on failure (if strict) or non-fatal (if !strict)
 *
 * Loads configuration from the specified path (or default location if config_path is NULL)
 * and applies values to global options.
 *
 * Default config file location (when config_path is NULL):
 * - Uses XDG_CONFIG_HOME/ascii-chat/config.toml if XDG_CONFIG_HOME is set (Unix)
 * - Falls back to ~/.config/ascii-chat/config.toml (Unix)
 * - Falls back to ~/.ascii-chat/config.toml (cross-platform)
 * - Uses %APPDATA%\.ascii-chat\config.toml on Windows
 *
 * Only applies configuration values that haven't already been set (though in practice,
 * CLI arguments will override config values anyway since this is called before
 * CLI parsing).
 *
 * Supported configuration sections:
 * - `[network]`: `port`
 * - `[server]`: `bind_ipv4`, `bind_ipv6`
 * - `[client]`: `address`, `width`, `height`, `webcam_index`, `webcam_flip`,
 *               `color_mode`, `render_mode`, `fps`, `stretch`, `quiet`,
 *               `snapshot_mode`, `snapshot_delay`, `test_pattern`,
 *               `show_capabilities`, `force_utf8`
 * - `[audio]`: `enabled`, `device`
 * - `[palette]`: `type`, `chars`
 * - `[crypto]`: `encrypt_enabled`, `key`, `password`, `keyfile`, `no_encrypt`,
 *               `server_key` (client only), `client_keys` (server only)
 * - `[logging]` or root: `log_file`
 *
 * @note This function should be called before `options_init()` parses command-line
 *       arguments, so that CLI arguments can override config file values.
 *
 * @note If strict is false and the config file doesn't exist, is not a regular file,
 *       or fails to parse, the function returns ASCIICHAT_OK (non-fatal). Individual
 *       invalid values are skipped with warnings, but valid values are still applied.
 *
 * @note If strict is true, any error (file not found, parse error, etc.) causes the
 *       function to return an error code immediately.
 *
 * @warning Configuration warnings are printed directly to stderr because logging
 *          may not be initialized yet when this function is called.
 *
 * @ingroup config
 */
asciichat_error_t config_load_and_apply(bool is_client, const char *config_path, bool strict);

/**
 * @brief Create default configuration file with all default values
 * @param config_path Path to config file to create (NULL uses default location)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Creates a new configuration file at the specified path (or default location
 * if config_path is NULL) with all configuration options set to their default
 * values from options.h.
 *
 * The created file includes:
 * - Version comment at the top (current ascii-chat version)
 * - All supported configuration sections with default values
 * - Comments explaining each option
 *
 * @note The function will create the directory structure if needed.
 * @note If the file already exists, it will not be overwritten (returns error).
 *
 * @ingroup config
 */
asciichat_error_t config_create_default(const char *config_path);
