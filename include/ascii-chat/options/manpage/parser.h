/**
 * @file parser.h
 * @brief Man page template parsing and section extraction
 * @ingroup options_manpage
 *
 * This module handles parsing man page templates to extract sections
 * and metadata about their types (AUTO-generated, MANUAL, or MERGE sections).
 *
 * Supports both FILE* and memory buffer parsing with automatic detection
 * of platform capabilities.
 */

#pragma once

#include <ascii-chat/manpage.h>
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Parse man page sections from a FILE handle
 *
 * Parses a man page template from an open FILE*, detecting:
 * - Section headers (.SH directive)
 * - Section markers (AUTO-START/END, MANUAL-START/END, MERGE-START/END)
 * - Section content and boundaries
 *
 * Memory is allocated for each section. Caller must use
 * manpage_parser_free_sections() to clean up.
 *
 * @param[in] f Open file handle to parse (cannot be NULL)
 * @param[out] out_sections Pointer to array of parsed_section_t (output)
 * @param[out] out_count Number of sections parsed (output)
 * @return ASCIICHAT_OK on success
 * @return ERROR_INVALID_PARAM if parameters are invalid
 * @return ERROR_CONFIG if parsing fails or memory allocation fails
 *
 * @note Caller must call manpage_parser_free_sections(out_sections, out_count)
 * @note The file position is not reset; caller may need to fseek if needed
 *
 * @ingroup options_manpage
 */
asciichat_error_t manpage_parser_parse_file(FILE *f, parsed_section_t **out_sections, size_t *out_count);

/**
 * @brief Parse man page sections from memory buffer
 *
 * Parses man page content from a memory buffer, detecting sections
 * and markers like parse_file() but from pre-loaded content.
 *
 * Uses platform-specific optimizations:
 * - Unix/macOS: fmemopen() for zero-copy parsing (if available)
 * - Windows/fallback: Creates temporary file
 *
 * Memory is allocated for each section. Caller must use
 * manpage_parser_free_sections() to clean up.
 *
 * @param[in] content Pointer to memory buffer containing man page (cannot be NULL)
 * @param[in] content_len Length of buffer in bytes (must be > 0)
 * @param[out] out_sections Pointer to array of parsed_section_t (output)
 * @param[out] out_count Number of sections parsed (output)
 * @return ASCIICHAT_OK on success
 * @return ERROR_INVALID_PARAM if parameters are invalid
 * @return ERROR_CONFIG if parsing fails or memory allocation fails
 *
 * @note Caller must call manpage_parser_free_sections(out_sections, out_count)
 * @note Available in all builds (Debug, Dev, Release)
 *
 * @ingroup options_manpage
 */
asciichat_error_t manpage_parser_parse_memory(const char *content, size_t content_len, parsed_section_t **out_sections,
                                              size_t *out_count);

/**
 * @brief Free parsed sections array
 *
 * Frees all allocated memory for sections array including section names
 * and content. Safe to call with NULL pointers.
 *
 * @param[in] sections Array of parsed sections (can be NULL)
 * @param[in] count Number of sections in array
 *
 * @ingroup options_manpage
 */
void manpage_parser_free_sections(parsed_section_t *sections, size_t count);

/**
 * @brief Find section by name
 *
 * Searches the parsed sections array for a section with matching name.
 * Comparison is case-sensitive.
 *
 * @param[in] sections Array of parsed sections
 * @param[in] count Number of sections
 * @param[in] section_name Section name to search for (case-sensitive)
 * @return Pointer to matching section, or NULL if not found
 *
 * @ingroup options_manpage
 */
const parsed_section_t *manpage_parser_find_section(const parsed_section_t *sections, size_t count,
                                                    const char *section_name);

/** @} */
