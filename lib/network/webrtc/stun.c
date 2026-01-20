/**
 * @file networking/webrtc/stun.c
 * @brief STUN server configuration and parsing utilities
 * @ingroup webrtc
 */

#include "stun.h"
#include "common.h"
#include "log/logging.h"
#include <string.h>
#include <ctype.h>

/**
 * @brief Parse comma-separated STUN server URLs into stun_server_t array
 *
 * Parses a comma-separated string of STUN server URLs into stun_server_t structs.
 * If the input string is empty or NULL, uses the provided default string.
 *
 * @param csv_servers Comma-separated STUN server URLs (can be empty)
 * @param default_csv Default servers to use if csv_servers is empty
 * @param out_servers Output array of stun_server_t structs (must be pre-allocated)
 * @param max_count Maximum number of servers to parse
 * @return Number of servers parsed (0-max_count), or -1 on error
 */
int stun_servers_parse(const char *csv_servers, const char *default_csv,
                       stun_server_t *out_servers, int max_count) {
  if (!out_servers || max_count <= 0) {
    log_warn("stun_servers_parse: Invalid output array or max_count");
    return -1;
  }

  // Use defaults if input is empty
  const char *servers_to_parse = csv_servers;
  if (!servers_to_parse || servers_to_parse[0] == '\0') {
    servers_to_parse = default_csv;
  }

  if (!servers_to_parse || servers_to_parse[0] == '\0') {
    log_warn("stun_servers_parse: No servers to parse and no defaults provided");
    return 0;
  }

  int count = 0;
  const char *current = servers_to_parse;
  const char *end;

  while (count < max_count && *current != '\0') {
    // Skip leading whitespace
    while (isspace(*current)) {
      current++;
    }

    if (*current == '\0') {
      break;
    }

    // Find the end of this server (comma or end of string)
    end = strchr(current, ',');
    if (!end) {
      end = current + strlen(current);
    }

    // Trim trailing whitespace
    while (end > current && isspace(*(end - 1))) {
      end--;
    }

    // Calculate length
    size_t len = end - current;
    if (len == 0) {
      // Empty entry, skip
      current = *end ? end + 1 : end;
      continue;
    }

    if (len >= STUN_MAX_URL_LEN) {
      log_warn("stun_servers_parse: STUN server URL too long (max %d): %.*s", STUN_MAX_URL_LEN,
               (int)len, current);
      return -1;
    }

    // Copy to output array
    out_servers[count].host_len = (uint8_t)len;
    memcpy(out_servers[count].host, current, len);
    out_servers[count].host[len] = '\0'; // Null-terminate for convenience

    count++;

    // Move to next entry
    current = *end ? end + 1 : end;
  }

  return count;
}
