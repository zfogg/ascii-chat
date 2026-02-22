/**
 * @file embedded_resources.h
 * @brief Embedded resource management for production builds
 * @ingroup common
 *
 * This module provides unified access to embedded documentation resources
 * in production builds while maintaining fast iteration in development.
 *
 * **Build-Time Behavior:**
 * - **Production builds** (Release, RelWithDebInfo): Resources are embedded
 *   at compile time using CMake scripts. The binary is self-contained.
 * - **Development builds** (Debug, Dev): Resources are read from filesystem
 *   for fast iteration (edit → rebuild → test → no wait).
 *
 * **Resource Types:**
 * 1. Man page template (`share/man/man1/ascii-chat.1.in`)
 * 2. Man page content (`share/man/man1/ascii-chat.1.content`)
 *
 * **Example Usage:**
 * ```c
 * // Get man page template (works in both production and development)
 * FILE *template_file = NULL;
 * const char *template_str = NULL;
 * size_t template_len = 0;
 *
 * int result = get_manpage_template(&template_file, &template_str, &template_len);
 * if (result != 0) {
 *     log_error("Failed to load man page template");
 *     return;
 * }
 *
 * // Use template (either from FILE* or const char*)
 * if (template_str) {
 *     // Production: parse in-memory string
 *     parse_from_memory(template_str, template_len);
 * } else {
 *     // Development: parse from file
 *     parse_from_file(template_file);
 * }
 *
 * // Always clean up
 * release_manpage_resources(template_file);
 * ```
 */

#pragma once

#include <stddef.h>
#include <stdio.h>

// =============================================================================
// Build Type Detection
// =============================================================================

/**
 * @brief Behavior based on build type
 *
 * In CMake:
 * - `NDEBUG` is defined in Release/RelWithDebInfo builds → use embedded resources
 * - `NDEBUG` is undefined in Debug/Dev builds → use filesystem resources
 */

// =============================================================================
// External Declarations for Embedded Data
// =============================================================================

/**
 * @brief Embedded man page template content (auto-generated at build time)
 * @ingroup embedded
 * @internal
 */
extern const char embedded_manpage_template[];

/**
 * @brief Size of embedded_manpage_template (excluding null terminator)
 * @ingroup embedded
 * @internal
 */
extern const size_t embedded_manpage_template_size;

// =============================================================================
// Resource Access Functions
// =============================================================================

/**
 * @brief Get man page template source (embedded or filesystem)
 *
 * Automatically selects between embedded and filesystem resources based
 * on build type:
 * - **Release builds** (NDEBUG defined): Returns embedded string
 * - **Debug builds** (NDEBUG undefined): Reads from filesystem
 *
 * **Parameter Usage:**
 * - In production: `out_content` and `out_len` are set, `out_file` is NULL
 * - In development: `out_file` is set to open FILE*, others are NULL
 * - Caller must check which one is non-NULL to determine source
 *
 * @param out_file Output pointer for FILE* (development) or NULL (production)
 * @param out_content Output pointer for const char* (production) or NULL (dev)
 * @param out_len Output pointer for size_t (production) or NULL (development)
 * @return 0 on success, -1 on error
 *
 * @note Caller must call release_manpage_resources(out_file) when done
 * @note Do NOT free out_content - it's either static or managed elsewhere
 *
 * @ingroup embedded
 */
int get_manpage_template(FILE **out_file, const char **out_content, size_t *out_len);

/**
 * @brief Get man page content source (embedded or filesystem)
 *
 * Same behavior as get_manpage_template() but for the content file.
 * See get_manpage_template() documentation for usage details.
 *
 * @param out_file Output pointer for FILE* (development) or NULL (production)
 * @param out_content Output pointer for const char* (production) or NULL (dev)
 * @param out_len Output pointer for size_t (production) or NULL (development)
 * @return 0 on success, -1 on error
 *
 * @ingroup embedded
 */
int get_manpage_content(FILE **out_file, const char **out_content, size_t *out_len);

/**
 * @brief Release resources obtained from get_manpage_*
 *
 * Properly cleans up resources based on build type:
 * - **Production**: No-op (embedded strings are static)
 * - **Development**: Closes FILE* if non-NULL
 *
 * @param file FILE* obtained from get_manpage_template/content(), or NULL
 *
 * @note Safe to call with NULL
 * @note Only closes file, does NOT free content pointer
 *
 * @ingroup embedded
 */
void release_manpage_resources(FILE *file);

/** @} */
