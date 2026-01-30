/**
 * @file completions.h
 * @brief Auto-generated shell completions from options registry
 * @ingroup options
 *
 * This module provides auto-generated completion scripts for various shells
 * (bash, fish, zsh, powershell) by extracting option metadata from the
 * centralized options registry.
 *
 * **Usage**:
 *
 * ```bash
 * # Generate bash completions
 * eval "$(ascii-chat --completions bash)"
 *
 * # Generate fish completions
 * ascii-chat --completions fish | source
 *
 * # Generate zsh completions
 * eval "$(ascii-chat --completions zsh)"
 *
 * # Generate PowerShell completions
 * ascii-chat --completions powershell | Out-String | Invoke-Expression
 * ```
 *
 * **Architecture**:
 *
 * The completion system has three layers:
 *
 * 1. **Format Enum** (`completion_format_t`): Identifies target shell
 * 2. **Format Generators**: Shell-specific implementations (bash.c, fish.c, etc.)
 * 3. **Dispatcher**: Routes to appropriate generator based on format
 *
 * All generators extract metadata from the centralized options registry via:
 * - `options_registry_get_for_mode()` - Get options by mode
 * - `options_registry_find_by_name()` - Look up individual options
 * - Option descriptor fields (long_name, short_name, help_text, mode_bitmask)
 *
 * **Adding New Shells**:
 *
 * To add support for a new shell:
 *
 * 1. Create `lib/options/completions/SHELL_NAME.h` and `.c`
 * 2. Implement `completions_generate_SHELL_NAME()` function
 * 3. Add case to `completions_generate_for_shell()` dispatcher
 * 4. Update `completion_parse_shell_name()` to recognize new shell
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include <stdio.h>
#include "common.h"
#include "options/options.h"
#include "options/registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Supported shell completion formats
 */
typedef enum {
  COMPLETION_FORMAT_BASH,       /**< Bash shell completion */
  COMPLETION_FORMAT_FISH,       /**< Fish shell completion */
  COMPLETION_FORMAT_ZSH,        /**< Zsh shell completion */
  COMPLETION_FORMAT_POWERSHELL, /**< PowerShell completion */
  COMPLETION_FORMAT_UNKNOWN     /**< Unknown/invalid format */
} completion_format_t;

/**
 * @brief Generate shell completions and write to output stream
 *
 * Generates a complete shell completion script for the specified shell format
 * by extracting option metadata from the centralized registry.
 *
 * @param format Shell format to generate completions for
 * @param output FILE stream to write completion script to (e.g., stdout)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t completions_generate_for_shell(completion_format_t format, FILE *output);

/**
 * @brief Get human-readable shell name for format
 *
 * @param format Completion format
 * @return Shell name string (e.g., "bash", "fish"), or "unknown" if invalid
 */
const char *completions_get_shell_name(completion_format_t format);

/**
 * @brief Parse shell name string to completion format
 *
 * Converts a shell name (e.g., "bash", "fish") to the corresponding
 * completion format enum. Case-insensitive.
 *
 * @param shell_name Shell name string (e.g., "bash", "fish", "zsh")
 * @return Completion format, or COMPLETION_FORMAT_UNKNOWN if not recognized
 */
completion_format_t completions_parse_shell_name(const char *shell_name);

/**
 * @brief Collect options from all modes with deduplication
 *
 * Iterates through all completion modes (MODE_DISCOVERY, MODE_SERVER, MODE_CLIENT,
 * MODE_MIRROR, MODE_DISCOVERY_SERVICE) and collects unique options by long_name.
 * Useful for generators that need to show completions for options across multiple modes.
 *
 * @param[out] count Pointer to receive the count of unique options
 * @return Dynamically allocated array of option_descriptor_t, must be freed by caller.
 *         Returns NULL if no options found.
 *
 * @note The caller must free the returned pointer with SAFE_FREE()
 */
option_descriptor_t *completions_collect_all_modes_unique(size_t *count);

#ifdef __cplusplus
}
#endif
