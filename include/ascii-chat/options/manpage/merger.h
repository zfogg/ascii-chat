/**
 * @file merger.h
 * @brief Man page content merging and orchestration
 * @ingroup options_manpage
 *
 * This module orchestrates the complete man page generation pipeline:
 * 1. Load resources (template and content files)
 * 2. Parse existing sections to preserve manual content
 * 3. Generate or merge new sections
 * 4. Write final output with proper formatting
 *
 * Handles three section types:
 * - AUTO: Fully auto-generated, regenerated each time
 * - MANUAL: User-maintained, never modified by generator
 * - MERGE: Intelligent merge of auto-generated and manual content
 */

#pragma once

#include <ascii-chat/builder.h>
#include <ascii-chat/manpage.h>
#include <stdio.h>

/**
 * @brief Merge auto-generated options into section content
 *
 * Takes auto-generated options content from config and intelligently merges
 * it with existing manual options (if any). Removes duplicates by option name
 * while preserving manual descriptions.
 *
 * Currently returns the auto-generated content as-is. In a full implementation,
 * this would parse both auto and manual option lists and merge them.
 *
 * @param[in] config Options config with auto-generated options
 * @param[in] existing_section Existing section with manual options (can be NULL)
 * @return Newly allocated merged content (caller must free)
 *
 * @note Caller must free the returned string
 *
 * @ingroup options_manpage
 */
char *manpage_merger_merge_options(const options_config_t *config, const parsed_section_t *existing_section);

/**
 * @brief Merge environment variables from auto-generated and manual sources
 *
 * Takes environment variables from the options config and intelligently merges
 * them with existing manual environment documentation (if any).
 *
 * Handles deduplication and sorting of environment variable names while
 * preserving manual descriptions where they exist.
 *
 * @param[in] config Options config with auto-generated environment variables
 * @param[in] existing_section Existing section with manual documentation (can be NULL)
 * @return Newly allocated merged content (caller must free)
 *
 * @note Caller must free the returned string
 *
 * @ingroup options_manpage
 */
char *manpage_merger_merge_environment(const options_config_t *config, const parsed_section_t *existing_section);

/**
 * @brief Merge positional arguments and examples
 *
 * Takes auto-generated positional arguments and examples and merges with
 * existing manual content.
 *
 * @param[in] config Options config with auto-generated positional args
 * @param[in] existing_section Existing section (can be NULL)
 * @return Newly allocated merged content (caller must free)
 *
 * @note Caller must free the returned string
 *
 * @ingroup options_manpage
 */
char *manpage_merger_merge_positional(const options_config_t *config, const parsed_section_t *existing_section);

/**
 * @brief Get auto-generated usage content
 *
 * Generates the USAGE section from options config usage descriptors.
 *
 * @param[in] config Options config (can be NULL to generate all modes)
 * @param[out] out_content Pointer to store generated content
 * @param[out] out_len Pointer to store content length
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note Caller must free out_content using SAFE_FREE
 *
 * @ingroup options_manpage
 */
asciichat_error_t manpage_merger_generate_usage(const options_config_t *config, char **out_content, size_t *out_len);

/**
 * @brief Get auto-generated synopsis content
 *
 * Generates the SYNOPSIS section for the specified mode.
 *
 * @param[in] mode_name Mode name or NULL for binary-level
 * @param[out] out_content Pointer to store generated content
 * @param[out] out_len Pointer to store content length
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note Caller must free out_content using SAFE_FREE
 *
 * @ingroup options_manpage
 */
asciichat_error_t manpage_merger_generate_synopsis(const char *mode_name, char **out_content, size_t *out_len);

/**
 * @brief Free merged section content
 *
 * Frees memory allocated by merger functions. Safe to call with NULL.
 *
 * @param[in] content Content to free (can be NULL)
 *
 * @ingroup options_manpage
 */
void manpage_merger_free_content(char *content);

/** @} */
