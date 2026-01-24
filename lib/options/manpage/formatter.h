/**
 * @file formatter.h
 * @brief Groff/troff formatting utilities for man page generation
 * @ingroup options_manpage
 *
 * This module provides utilities for generating properly formatted groff/troff
 * output for man pages. Handles:
 * - Section headers (.SH directive)
 * - Text formatting (bold .B, italic .I, constant .C)
 * - Paragraph and item formatting (.TP for tagged paragraphs)
 * - Special character escaping
 * - Section markers (AUTO/MANUAL/MERGE)
 *
 * All functions write directly to FILE* for efficient streaming output.
 */

#pragma once

#include <stdio.h>
#include <stdbool.h>

/**
 * @brief Escape special characters for groff output
 *
 * Escapes characters that have special meaning in groff format.
 * Currently returns the string as-is since most man page content
 * doesn't contain problematic characters. Can be extended for
 * more robust escaping if needed.
 *
 * @param[in] str String to escape (can be NULL)
 * @return Escaped string (never NULL, returns "" for NULL input)
 *
 * @note This is a simple implementation. For production use with
 *       arbitrary content, consider a more robust escaping strategy.
 *
 * @ingroup options_manpage
 */
const char *manpage_fmt_escape_groff(const char *str);

/**
 * @brief Write a section header directive
 *
 * Writes ".SH SECTION_NAME" directive to output.
 * Example: manpage_fmt_write_section(f, "OPTIONS") writes ".SH OPTIONS\n"
 *
 * @param[in] f Output file handle (cannot be NULL)
 * @param[in] section_name Section name to write (cannot be NULL)
 *
 * @ingroup options_manpage
 */
void manpage_fmt_write_section(FILE *f, const char *section_name);

/**
 * @brief Write a blank line (for spacing between sections)
 *
 * @param[in] f Output file handle (cannot be NULL)
 *
 * @ingroup options_manpage
 */
void manpage_fmt_write_blank_line(FILE *f);

/**
 * @brief Write text in bold format
 *
 * Writes text with bold formatting directive.
 * Example: manpage_fmt_write_bold(f, "ascii-chat") writes ".B ascii-chat\n"
 *
 * @param[in] f Output file handle (cannot be NULL)
 * @param[in] text Text to write in bold (cannot be NULL)
 *
 * @ingroup options_manpage
 */
void manpage_fmt_write_bold(FILE *f, const char *text);

/**
 * @brief Write text in italic format
 *
 * Writes text with italic formatting directive.
 * Example: manpage_fmt_write_italic(f, "options") writes ".I options\n"
 *
 * @param[in] f Output file handle (cannot be NULL)
 * @param[in] text Text to write in italic (cannot be NULL)
 *
 * @ingroup options_manpage
 */
void manpage_fmt_write_italic(FILE *f, const char *text);

/**
 * @brief Write a tagged paragraph header
 *
 * Writes ".TP" directive to start a tagged paragraph (for option descriptions).
 * Should be followed by manpage_fmt_write_bold() for the tag and then
 * regular text for the description.
 *
 * @param[in] f Output file handle (cannot be NULL)
 *
 * @ingroup options_manpage
 */
void manpage_fmt_write_tagged_paragraph(FILE *f);

/**
 * @brief Write a section marker comment
 *
 * Writes comment directives marking section types (AUTO/MANUAL/MERGE).
 * Example: manpage_fmt_write_marker(f, "AUTO", "OPTIONS", true)
 * writes:
 *   .\" AUTO-START: OPTIONS
 *   .\" This section is auto-generated. Manual edits will be lost.
 *
 * @param[in] f Output file handle (cannot be NULL)
 * @param[in] type Marker type: "AUTO", "MANUAL", or "MERGE" (cannot be NULL)
 * @param[in] section_name Section name (cannot be NULL)
 * @param[in] is_start true for START marker, false for END marker
 *
 * @ingroup options_manpage
 */
void manpage_fmt_write_marker(FILE *f, const char *type, const char *section_name, bool is_start);

/**
 * @brief Write plain text line (without directive)
 *
 * Writes text directly without any formatting directive.
 * Useful for description text and content lines.
 *
 * @param[in] f Output file handle (cannot be NULL)
 * @param[in] text Text to write (can be NULL, in which case only newline written)
 *
 * @ingroup options_manpage
 */
void manpage_fmt_write_text(FILE *f, const char *text);

/**
 * @brief Write groff title/header (.TH directive)
 *
 * Writes the full title header for a man page with current date.
 * Format: .TH NAME SECTION DATE SOURCE MANUAL
 *
 * @param[in] f Output file handle (cannot be NULL)
 * @param[in] program_name Program name (e.g., "ascii-chat") (cannot be NULL)
 * @param[in] mode_name Mode name or NULL (e.g., "server", "client", or NULL for binary-level)
 * @param[in] brief_description One-line description (cannot be NULL)
 *
 * @ingroup options_manpage
 */
void manpage_fmt_write_title(FILE *f, const char *program_name, const char *mode_name, const char *brief_description);

/** @} */
