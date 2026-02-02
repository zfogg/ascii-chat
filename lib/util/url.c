/**
 * @file util/url.c
 * @brief HTTPS URL parsing utilities implementation
 * @ingroup util
 */

#include <ascii-chat/util/url.h>
#include <ascii-chat/common.h>
#include <string.h>
#include <stdlib.h>

asciichat_error_t parse_https_url(const char *url, https_url_parts_t *parts_out) {
  if (!url || !parts_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: url=%p, parts_out=%p", url, parts_out);
  }

  /* Clear output structure */
  parts_out->hostname = NULL;
  parts_out->path = NULL;

  /* Validate HTTPS prefix */
  if (strncmp(url, "https://", 8) != 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "URL must start with https://: %s", url);
  }

  /* Skip "https://" prefix to get to hostname */
  const char *hostname_start = url + 8;

  /* Find the first '/' to separate hostname from path */
  const char *path_start = strchr(hostname_start, '/');
  if (!path_start) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "HTTPS URL must include a path: %s", url);
  }

  /* Calculate hostname length */
  size_t hostname_len = (size_t)(path_start - hostname_start);
  if (hostname_len == 0 || hostname_len > 255) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid hostname length: %zu (expected 1-255)", hostname_len);
  }

  /* Allocate and copy hostname */
  parts_out->hostname = SAFE_MALLOC(hostname_len + 1, char *);
  if (!parts_out->hostname) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate hostname buffer");
  }
  memcpy(parts_out->hostname, hostname_start, hostname_len);
  parts_out->hostname[hostname_len] = '\0';

  /* Allocate and copy path (includes leading '/') */
  size_t path_len = strlen(path_start);
  parts_out->path = SAFE_MALLOC(path_len + 1, char *);
  if (!parts_out->path) {
    SAFE_FREE(parts_out->hostname);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate path buffer");
  }
  memcpy(parts_out->path, path_start, path_len);
  parts_out->path[path_len] = '\0';

  return ASCIICHAT_OK;
}
