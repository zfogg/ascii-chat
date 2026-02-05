/**
 * @file networking/webrtc/stun.c
 * @brief STUN server configuration and parsing utilities
 * @ingroup webrtc
 */

#define PCRE2_CODE_UNIT_WIDTH 8
#include <ascii-chat/network/webrtc/stun.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/util/pcre2.h>
#include <string.h>
#include <ctype.h>
#include <pcre2.h>

/**
 * @brief PCRE2 regex validator for STUN server list parsing
 *
 * Uses centralized PCRE2 singleton for thread-safe compilation
 * with JIT compilation for 5-10x performance improvement.
 */

static const char *STUN_ENTRY_PATTERN = "\\s*([^\\s,][^,]*?[^\\s,]|[^\\s,])\\s*";

static pcre2_singleton_t *g_stun_entry_regex = NULL;

/**
 * Get compiled STUN entry regex (lazy initialization)
 * Returns NULL if compilation failed
 */
static pcre2_code *stun_entry_regex_get(void) {
  if (g_stun_entry_regex == NULL) {
    g_stun_entry_regex = pcre2_singleton_compile(STUN_ENTRY_PATTERN, PCRE2_MULTILINE);
  }
  return pcre2_singleton_get_code(g_stun_entry_regex);
}

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
int stun_servers_parse(const char *csv_servers, const char *default_csv, stun_server_t *out_servers, int max_count) {
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

  // Get compiled regex (lazy initialization)
  pcre2_code *regex = stun_entry_regex_get();

  // If regex not available, fall back to manual parsing
  if (!regex) {
    log_warn("stun_servers_parse: PCRE2 regex not available, falling back to manual parsing");
    // Fallback: manual character-by-character parsing (original implementation)
    int count = 0;
    const char *current = servers_to_parse;
    const char *end;

    while (count < max_count && *current != '\0') {
      while (isspace(*current)) {
        current++;
      }
      if (*current == '\0')
        break;

      end = strchr(current, ',');
      if (!end) {
        end = current + strlen(current);
      }

      while (end > current && isspace(*(end - 1))) {
        end--;
      }

      size_t len = end - current;
      if (len == 0) {
        current = *end ? end + 1 : end;
        continue;
      }

      if (len >= STUN_MAX_URL_LEN) {
        log_warn("stun_servers_parse: STUN server URL too long (max %d): %.*s", STUN_MAX_URL_LEN, (int)len, current);
        return -1;
      }

      out_servers[count].host_len = (uint8_t)len;
      memcpy(out_servers[count].host, current, len);
      out_servers[count].host[len] = '\0';
      count++;

      current = *end ? end + 1 : end;
    }
    return count;
  }

  // Parse using PCRE2 regex: match each trimmed entry
  int count = 0;
  pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(regex, NULL);
  if (!match_data) {
    log_warn("stun_servers_parse: Failed to allocate PCRE2 match data");
    return -1;
  }

  size_t offset = 0;
  int rc;

  while (count < max_count) {
    rc = pcre2_jit_match(regex, (PCRE2_SPTR8)servers_to_parse, strlen(servers_to_parse), offset, 0, match_data, NULL);

    if (rc < 0) {
      // No more matches (PCRE2_ERROR_NOMATCH or other error)
      break;
    }

    // Extract the captured group (group 1)
    PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
    size_t start = ovector[2]; // Start of group 1
    size_t end = ovector[3];   // End of group 1
    size_t len = end - start;

    if (len >= STUN_MAX_URL_LEN) {
      log_warn("stun_servers_parse: STUN server URL too long (max %d): %.*s", STUN_MAX_URL_LEN, (int)len,
               servers_to_parse + start);
      pcre2_match_data_free(match_data);
      return -1;
    }

    // Copy trimmed entry to output
    out_servers[count].host_len = (uint8_t)len;
    memcpy(out_servers[count].host, servers_to_parse + start, len);
    out_servers[count].host[len] = '\0';

    count++;
    offset = ovector[1]; // Move offset to end of this match
  }

  pcre2_match_data_free(match_data);
  return count;
}
