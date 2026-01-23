/**
 * @file manpage.h
 * @brief Man page template generation from options builder
 * @ingroup options
 *
 * This module provides functionality to auto-generate man page templates
 * from the options builder configuration. The generated templates include
 * auto-populated sections (SYNOPSIS, OPTIONS, EXAMPLES, USAGE) and placeholders
 * for manual sections (DESCRIPTION, FILES, NOTES, BUGS, AUTHOR, SEE ALSO).
 *
 * Supports merging auto-generated content with existing manual content using
 * section markers (AUTO-START/END, MANUAL-START/END, MERGE-START/END).
 *
 * @note Man pages are generated in groff/troff format (man(5) format)
 */

#pragma once

#include "builder.h"
#include "common.h"
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Section type marker
 */
typedef enum {
  SECTION_TYPE_AUTO,    ///< Fully auto-generated, manual edits lost on regeneration
  SECTION_TYPE_MANUAL,  ///< Manually maintained, never touched by generator
  SECTION_TYPE_MERGE,   ///< Intelligently merged: auto + manual content
  SECTION_TYPE_UNMARKED ///< No marker found, defaults to MANUAL
} section_type_t;

/**
 * @brief Parsed section information
 */
typedef struct {
  char *section_name;  ///< Section name (e.g., "ENVIRONMENT", "OPTIONS")
  section_type_t type; ///< Section type (AUTO/MANUAL/MERGE/UNMARKED)
  char *content;       ///< Full section content (including .SH header)
  size_t content_len;  ///< Length of content
  size_t start_line;   ///< Line number where section starts (1-based)
  size_t end_line;     ///< Line number where section ends (1-based)
  bool has_markers;    ///< True if marked with AUTO/MANUAL/MERGE markers
} parsed_section_t;

/**
 * @brief Generate a man page template from options builder configuration
 *
 * Creates a man page template at the specified path with auto-generated sections:
 * - .TH (title/header)
 * - NAME (auto: program-name — brief description)
 * - SYNOPSIS (auto: generated from usage descriptors)
 * - USAGE (auto: generated from usage descriptors with descriptions)
 * - OPTIONS (auto: generated from option descriptors, grouped by category)
 * - EXAMPLES (auto: generated from example descriptors)
 * - POSITIONAL ARGUMENTS (auto: generated from positional arg descriptors)
 * - ENVIRONMENT VARIABLES (auto: extracted from options with env_var_name set)
 *
 * Manual sections provided as placeholders:
 * - DESCRIPTION (manual: add program overview)
 * - FILES (manual: add configuration files)
 * - NOTES (manual: add additional notes)
 * - BUGS (manual: add known bugs)
 * - AUTHOR (manual: add author information)
 * - SEE ALSO (manual: add related commands)
 *
 * **Format Notes**:
 * - Output is in groff/troff format suitable for `man` command
 * - Section headers use `.SH` directive
 * - Bold/italic/constant formatting uses `.B`, `.I`, `.C` directives
 * - Options are formatted with short form (-x) and long form (--long-name)
 *
 * **Example Usage**:
 * ```c
 * options_builder_t *builder = options_builder_create(sizeof(server_options_t));
 * // ... add options ...
 * asciichat_error_t err = options_builder_generate_manpage_template(
 *     builder,
 *     "ascii-chat",
 *     "server",
 *     "ascii-chat-server.1",
 *     "Interactive terminal-based video chat"
 * );
 * if (err != ASCIICHAT_OK) {
 *     fprintf(stderr, "Failed to generate man page\n");
 * }
 * ```
 *
 * @param config Finalized options configuration (from options_builder_build)
 * @param program_name Program name (e.g., "ascii-chat")
 * @param mode_name Mode name (e.g., "server", "client", or NULL for binary-level)
 * @param output_path File path to write man page template to
 * @param brief_description One-line program description
 * @return ASCIICHAT_OK on success, ERROR_USAGE or ERROR_FILE on failure
 *
 * @note Creates or overwrites the file at output_path
 * @note Brief description should be short and start with lowercase
 * @note Man section number is auto-determined (1 for user commands, 5 for files)
 *
 * @ingroup options
 */
asciichat_error_t options_config_generate_manpage_template(const options_config_t *config, const char *program_name,
                                                           const char *mode_name, const char *output_path,
                                                           const char *brief_description);

/**
 * @brief Generate man page template from builder (before build())
 *
 * Convenience wrapper that builds the config from the builder,
 * generates the template, and cleans up.
 *
 * @param builder Options builder
 * @param program_name Program name
 * @param mode_name Mode name (or NULL)
 * @param output_path Output file path
 * @param brief_description Brief description
 * @return ASCIICHAT_OK on success, error code otherwise
 *
 * @ingroup options
 */
asciichat_error_t options_builder_generate_manpage_template(options_builder_t *builder, const char *program_name,
                                                            const char *mode_name, const char *output_path,
                                                            const char *brief_description);

/**
 * @brief Generate merged man page template preserving manual content
 *
 * Parses existing template (if exists) and merges auto-generated content
 * with manual content based on section markers:
 * - AUTO sections: Fully regenerated from builder
 * - MANUAL sections: Preserved exactly as-is
 * - MERGE sections: Intelligently merged (e.g., ENVIRONMENT vars)
 *
 * @param config Finalized options configuration
 * @param program_name Program name (e.g., "ascii-chat")
 * @param mode_name Mode name (e.g., "server", "client", or NULL)
 * @param output_path File path to write merged template to
 * @param brief_description One-line program description
 * @param existing_template_path Path to existing template (NULL if none)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note If existing_template_path is NULL, generates fresh template
 * @note Unmarked sections default to MANUAL (preserved)
 *
 * @ingroup options
 */
/**
 * @brief Generate merged man page from options builder and embedded resources
 *
 * Generates a merged man page by combining auto-generated content from the options
 * builder with manual sections from embedded resources. Automatically selects between
 * embedded resources (production builds) and filesystem resources (development builds).
 *
 * **Resource Loading:**
 * - **Template**: Loads ascii-chat.1.in template from embedded resources or filesystem
 * - **Content**: Loads ascii-chat.1.content sections from embedded resources or filesystem
 * - **Merging**: Intelligently merges AUTO, MANUAL, and MERGE-marked sections
 *
 * @param config Finalized options configuration
 * @param program_name Program name (e.g., "ascii-chat")
 * @param mode_name Mode name (e.g., "server", "client", or NULL for binary-level)
 * @param output_path Output file path, or NULL to write to stdout
 * @param brief_description One-line program description
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note If output_path is NULL, writes to stdout
 * @note Resources are loaded automatically from embedded or filesystem based on build type
 *
 * @deprecated The old signature with explicit file paths is no longer supported.
 *             Resources are now loaded automatically from embedded resources.
 */
asciichat_error_t options_config_generate_manpage_merged(const options_config_t *config, const char *program_name,
                                                         const char *mode_name, const char *output_path,
                                                         const char *brief_description);

/**
 * @brief Parse existing man page template to extract sections
 *
 * @param filepath Path to existing man page template (.1.in file)
 * @param num_sections Output: number of sections found
 * @return Array of parsed sections (caller must free with free_parsed_sections)
 * @return NULL on error
 *
 * @ingroup options
 */
parsed_section_t *parse_manpage_sections(const char *filepath, size_t *num_sections);

/**
 * @brief Free parsed sections array
 *
 * @param sections Array of parsed sections (can be NULL)
 * @param num_sections Number of sections
 *
 * @ingroup options
 */
void free_parsed_sections(parsed_section_t *sections, size_t num_sections);

/**
 * @brief Find section by name
 *
 * @param sections Array of parsed sections
 * @param num_sections Number of sections
 * @param section_name Section name to find (case-sensitive)
 * @return Pointer to section, or NULL if not found
 *
 * @ingroup options
 */
const parsed_section_t *find_section(const parsed_section_t *sections, size_t num_sections, const char *section_name);

/**
 * @brief Generate final man page (.1) from template (.1.in) with version substitution and optional content file
 *
 * @param template_path Path to input template (.1.in file)
 * @param output_path Path to output man page (.1 file)
 * @param version_string Version string to substitute for @PROJECT_VERSION@
 * @param content_file_path Optional path to file containing additional content to insert (NULL if none)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note This performs the same substitution as ConfigureManPage.cmake
 * @note If content_file_path is provided, its content is parsed and merged into appropriate sections
 * @note Only available in debug builds (when NDEBUG is not defined)
 *
 * @ingroup options
 */
#ifndef NDEBUG
asciichat_error_t options_config_generate_final_manpage(const char *template_path, const char *output_path,
                                                        const char *version_string, const char *content_file_path);
#endif

/** @} */
