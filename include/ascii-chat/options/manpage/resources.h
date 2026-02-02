/**
 * @file resources.h
 * @brief Resource management for man page generation
 * @ingroup options_manpage
 *
 * This module abstracts loading man page resources from either embedded
 * (production) or filesystem (development) sources. Provides a clean interface
 * for accessing template and content sections.
 */

#pragma once

#include <ascii-chat/common.h>
#include <stddef.h>

/**
 * @brief Man page resource container
 *
 * Holds loaded man page resources with metadata about their origin
 * and lifecycle management.
 */
typedef struct {
  const char *template_content; ///< Template file content (.1.in)
  size_t template_len;          ///< Length of template_content
  const char *content_sections; ///< Content sections file (.1.content)
  size_t content_len;           ///< Length of content_sections
  bool is_embedded;             ///< True if resources from embedded binary
  bool allocated;               ///< True if resources were allocated (need freeing)
} manpage_resources_t;

/**
 * @brief Load man page resources from embedded or filesystem sources
 *
 * Automatically selects resource source based on build type:
 * - Debug builds: Load from filesystem (share/man/man1/)
 * - Release builds: Load from embedded resources in binary
 *
 * The caller should call manpage_resources_cleanup() when done.
 *
 * @param[out] resources Populated with loaded resource pointers
 * @return ASCIICHAT_OK on success
 * @return ERROR_CONFIG if resources cannot be loaded
 * @return ERROR_INVALID_PARAM if resources is NULL
 *
 * @note On success, caller must call manpage_resources_cleanup()
 * @note For embedded resources, no cleanup needed (pointers to static data)
 * @note For filesystem resources, content is allocated and must be freed
 *
 * @ingroup options_manpage
 */
asciichat_error_t manpage_resources_load(manpage_resources_t *resources);

/**
 * @brief Cleanup allocated man page resources
 *
 * Frees any dynamically allocated resources. Safe to call even if
 * manpage_resources_load() failed or resources were from embedded binary.
 *
 * @param[in] resources Resources to cleanup (can be NULL)
 *
 * @ingroup options_manpage
 */
void manpage_resources_cleanup(manpage_resources_t *resources);

/**
 * @brief Check if resources are available
 *
 * Returns true only if both template and content resources are loaded.
 *
 * @param[in] resources Resources to check (can be NULL)
 * @return true if resources are complete and valid
 *
 * @ingroup options_manpage
 */
bool manpage_resources_is_valid(const manpage_resources_t *resources);

/** @} */
