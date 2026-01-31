/**
 * @file resources.c
 * @brief Man page resource loading and management
 * @ingroup options_manpage
 *
 * Abstracts loading man page resources from either embedded (production)
 * or filesystem (development) sources. Provides a clean interface that hides
 * the complexity of the underlying embedded_resources API.
 */

#include "resources.h"
#include "../../embedded_resources.h"
#include "../../log/logging.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Load a single resource (template or content) from embedded or filesystem
 *
 * Internal helper to load one resource with memory management.
 *
 * @param[out] out_content Pointer to store const char* content
 * @param[out] out_len Pointer to store content length
 * @param[in] get_resource_func Function to call (get_manpage_template or get_manpage_content)
 * @param[in] resource_name Human-readable name for logging
 * @return ASCIICHAT_OK on success, ERROR_CONFIG on failure
 */
static asciichat_error_t load_single_resource(const char **out_content, size_t *out_len,
                                              int (*get_resource_func)(FILE **out_file, const char **out_content,
                                                                       size_t *out_len),
                                              const char *resource_name) {
  if (!out_content || !out_len) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid output pointers for %s", resource_name);
  }

  FILE *file = NULL;
  const char *content = NULL;
  size_t len = 0;

  // Call the resource function to get template or content
  int result = get_resource_func(&file, &content, &len);
  if (result != 0) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to load %s resource", resource_name);
  }

  // Determine if we got embedded (content) or filesystem (file) resource
  if (content) {
    // Production: embedded resource
    // No allocation needed, content is static
    *out_content = content;
    *out_len = len;
    log_debug("Loaded %s from embedded resources (%zu bytes)", resource_name, len);
    return ASCIICHAT_OK;
  } else if (file) {
    // Development: filesystem resource
    // Need to read entire file into memory for easy access
    char *buffer = NULL;

    // Seek to end to determine file size
    if (fseek(file, 0, SEEK_END) != 0) {
      release_manpage_resources(file);
      return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to seek %s file", resource_name);
    }

    long file_size = ftell(file);
    if (file_size < 0) {
      release_manpage_resources(file);
      return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to get %s file size", resource_name);
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
      release_manpage_resources(file);
      return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to seek %s file", resource_name);
    }

    // Allocate buffer (file_size + 1 for null terminator)
    size_t buffer_size = (size_t)file_size + 1;
    buffer = SAFE_MALLOC(buffer_size, char *);
    if (!buffer) {
      release_manpage_resources(file);
      return SET_ERRNO(ERROR_CONFIG, "Failed to allocate memory for %s (%zu bytes)", resource_name, buffer_size);
    }

    // Read file content
    size_t bytes_read = fread(buffer, 1, (size_t)file_size, file);
    release_manpage_resources(file);

    if (bytes_read != (size_t)file_size) {
      SAFE_FREE(buffer);
      return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to read complete %s file (%zu/%lu bytes read)", resource_name,
                           bytes_read, (unsigned long)file_size);
    }

    // Ensure null termination
    buffer[(size_t)file_size] = '\0';

    *out_content = buffer;
    *out_len = (size_t)file_size;
    log_debug("Loaded %s from filesystem (%zu bytes)", resource_name, *out_len);
    return ASCIICHAT_OK;
  } else {
    // Should not happen - either file or content should be set
    return SET_ERRNO(ERROR_CONFIG, "Invalid %s resource state (neither file nor content available)", resource_name);
  }
}

asciichat_error_t manpage_resources_load(manpage_resources_t *resources) {
  if (!resources) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "resources pointer cannot be NULL");
  }

  // Initialize to safe state
  resources->template_content = NULL;
  resources->template_len = 0;
  resources->content_sections = NULL;
  resources->content_len = 0;
  resources->is_embedded = false;
  resources->allocated = false;

  // Load template resource
  asciichat_error_t err = load_single_resource(&resources->template_content, &resources->template_len,
                                               get_manpage_template, "man page template");
  if (err != ASCIICHAT_OK) {
    return err;
  }

  // Content is now merged into template - no separate content file needed
  resources->content_sections = "";
  resources->content_len = 0;

  // Determine if resources came from embedded binary
  // In production (NDEBUG), both would be embedded; in development, both would be files
#ifdef NDEBUG
  resources->is_embedded = true;
#else
  resources->is_embedded = false;
#endif

  // Mark that we may have allocated memory (only in debug builds for filesystem reads)
#ifdef NDEBUG
  resources->allocated = false;
#else
  resources->allocated = true;
#endif

  log_debug("Resources loaded successfully (embedded=%d, allocated=%d)", resources->is_embedded, resources->allocated);
  return ASCIICHAT_OK;
}

void manpage_resources_cleanup(manpage_resources_t *resources) {
  if (!resources) {
    return;
  }

  // Only free content if it was allocated (i.e., loaded from filesystem in debug builds)
  if (resources->allocated) {
#ifndef NDEBUG
    // In debug builds, template and content were allocated by load_single_resource
    if (resources->template_content) {
      char *template_ptr = (char *)resources->template_content;
      SAFE_FREE(template_ptr);
    }
    if (resources->content_sections) {
      char *content_ptr = (char *)resources->content_sections;
      SAFE_FREE(content_ptr);
    }
#endif
  }

  // Clear the structure
  resources->template_content = NULL;
  resources->template_len = 0;
  resources->content_sections = NULL;
  resources->content_len = 0;
  resources->is_embedded = false;
  resources->allocated = false;
}

bool manpage_resources_is_valid(const manpage_resources_t *resources) {
  if (!resources) {
    return false;
  }

  // Resources are valid if template is loaded (content is now optional/merged into template)
  return resources->template_content != NULL && resources->template_len > 0;
}
