/**
 * @file networking/webrtc/stun.c
 * @brief STUN server configuration and parsing utilities
 * @ingroup webrtc
 */

#include <ascii-chat/network/webrtc/stun.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <string.h>
#include <ctype.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <pthread.h>

/**
 * @brief PCRE2 regex validator for STUN server list parsing
 *
 * Patterns compiled once and reused with JIT compilation for 5-10x performance.
 * Thread-safe singleton initialized via pthread_once.
 */
typedef struct {
  pcre2_code *stun_entry_regex; ///< Pattern: match individual STUN server entries
  pcre2_jit_stack *jit_stack;   ///< JIT compilation stack
  bool initialized;             ///< Initialization flag
} stun_validator_t;

static stun_validator_t g_stun_validator = {0};
static pthread_once_t g_stun_once = PTHREAD_ONCE_INIT;

/**
 * @brief Initialize STUN PCRE2 regex patterns (pthread_once callback)
 */
static void stun_regex_init(void) {
  int errornumber;
  PCRE2_SIZE erroroffset;

  // Pattern: Match individual STUN server entries with whitespace trimming
  // \s*([^\s,][^,]*?[^\s,]|[^\s,])\s*
  // Matches: optional leading whitespace, then:
  //  - Either: non-ws/non-comma + anything except comma + non-ws/non-comma (entries with spaces)
  //  - Or: single non-ws/non-comma char (short entries)
  // Then: optional trailing whitespace
  const char *stun_pattern = "\\s*([^\\s,][^,]*?[^\\s,]|[^\\s,])\\s*";

  g_stun_validator.stun_entry_regex = pcre2_compile((PCRE2_SPTR8)stun_pattern, PCRE2_ZERO_TERMINATED, PCRE2_MULTILINE,
                                                    &errornumber, &erroroffset, NULL);

  if (!g_stun_validator.stun_entry_regex) {
    PCRE2_UCHAR8 error_msg[120];
    pcre2_get_error_message(errornumber, error_msg, sizeof(error_msg));
    log_warn("stun_servers_parse: Failed to compile STUN pattern: %s at offset %zu", error_msg, erroroffset);
    return;
  }

  // Attempt JIT compilation (non-fatal if fails)
  if (pcre2_jit_compile(g_stun_validator.stun_entry_regex, PCRE2_JIT_COMPLETE) > 0) {
    g_stun_validator.jit_stack = pcre2_jit_stack_create(32 * 1024, 512 * 1024, NULL);
  }

  g_stun_validator.initialized = true;
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

  // Initialize regex singleton (thread-safe)
  pthread_once(&g_stun_once, stun_regex_init);

  // If regex not available, fall back to manual parsing
  if (!g_stun_validator.initialized || !g_stun_validator.stun_entry_regex) {
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
  pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(g_stun_validator.stun_entry_regex, NULL);
  if (!match_data) {
    log_warn("stun_servers_parse: Failed to allocate PCRE2 match data");
    return -1;
  }

  size_t offset = 0;
  int rc;

  while (count < max_count) {
    rc = pcre2_match(g_stun_validator.stun_entry_regex, (PCRE2_SPTR8)servers_to_parse, strlen(servers_to_parse), offset,
                     0, match_data, NULL);

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
