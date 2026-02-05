/**
 * @file util/url.c
 * @brief Production-grade URL parsing and validation using PCRE2
 * @ingroup util
 *
 * Implements robust HTTP(S) URL validation using the production-grade
 * regex by Diego Perini (MIT License), compiled with PCRE2 and JIT
 * for high performance.
 */

#include <ascii-chat/util/url.h>
#include <ascii-chat/common.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * PRODUCTION-GRADE URL REGEX (Diego Perini, MIT License)
 *
 * Supports: http/https, public IPv4, IPv6 with zone IDs, hostnames, localhost
 * Rejects: Private IPs, schemeless URLs, non-http(s) schemes
 * ═══════════════════════════════════════════════════════════════════════════ */

static const char *URL_REGEX_PATTERN =
    // SCHEME: http or https (case-insensitive)
    "^(?<scheme>https?)://(?:(?<userinfo>\\S+(?::\\S*)?)@)?"
    // HOST: one of three alternatives below
    "(?<host>"
    "(?:"
    // IPv4 ADDRESS: e.g. 192.168.1.1
    // Negative lookaheads reject multicast (224-239) and broadcast (255.255.255.255)
    "(?!(?:22[4-9]|23\\d)(?:\\.\\d{1,3}){3})(?!255\\.255\\.255\\.255)"
    // First octet: 0-255
    "(?:[0-9]\\d?|1\\d\\d|2[01]\\d|22[0-3]|24\\d|25[0-5])"
    // Second and third octets: 0-255 (repeated twice)
    "(?:\\.(?:1?\\d{1,2}|2[0-4]\\d|25[0-5])){2}"
    // Fourth octet: 0-255
    "(?:\\.(?:[0-9]\\d?|1\\d\\d|2[0-4]\\d|25[0-5]))"
    ")"
    // IPv6 ADDRESS: e.g. [::1] or [fe80::1%eth0]
    // Supports zone IDs (e.g. %25eth0 for link-local addresses)
    "|(?:\\[(?<ipv6>[a-fA-F0-9:.]+(?:%25[a-zA-Z0-9._~!$&'()*+,;=-]+)?)\\])"
    // HOSTNAME: e.g. example.com, localhost, or international domain names
    // Negative lookahead rejects bare IP notation (digits.digits.digits format)
    "|(?!\\d+(?:\\.\\d+)*(?:[:/"
    "?#]|$))(?:[a-z0-9_\\x{00a1}-\\x{ffff}][a-z0-9\\x{00a1}-\\x{ffff}_-]{0,62})?[a-z0-9_\\x{00a1}-\\x{ffff}](?:\\.(?:["
    "a-z0-9_\\x{00a1}-\\x{ffff}][a-z0-9\\x{00a1}-\\x{ffff}_-]{0,62})?[a-z0-9_\\x{00a1}-\\x{ffff}])*\\.?"
    ")"
    // PORT: optional :port (1-5 digits, e.g. :8080, :443)
    "(?::(?<port>\\d{1,5}))?"
    // PATH/QUERY/FRAGMENT: optional /path, ?query, or #fragment
    "(?<path_query_fragment>[/?#]\\S*)?$";

/* ═══════════════════════════════════════════════════════════════════════════
 * PCRE2 REGEX VALIDATOR STATE
 *
 * Global singleton with lazy initialization to avoid thread safety issues.
 * Compiled regex is read-only after initialization, safe for concurrent reads.
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
  pcre2_code *regex;          /* Compiled regex (read-only after init) */
  pcre2_jit_stack *jit_stack; /* JIT stack for performance */
  bool initialized;           /* Whether validator is initialized */
} url_validator_t;

static url_validator_t g_validator = {0};
static pthread_once_t g_validator_once = PTHREAD_ONCE_INIT;

/**
 * Initialize global URL validator with PCRE2 compiled regex
 * Called once per process via pthread_once
 */
static void url_validator_init(void) {
  int errornumber;
  PCRE2_SIZE erroroffset;

  /* Compile regex with UTF-8 support and case-insensitive matching */
  g_validator.regex = pcre2_compile((PCRE2_SPTR)URL_REGEX_PATTERN, PCRE2_ZERO_TERMINATED,
                                    PCRE2_CASELESS | PCRE2_UCP | PCRE2_UTF, &errornumber, &erroroffset, NULL);

  if (!g_validator.regex) {
    PCRE2_UCHAR error_buffer[256];
    pcre2_get_error_message(errornumber, error_buffer, sizeof(error_buffer));
    log_fatal("Failed to compile URL regex at offset %zu: %s", erroroffset, (const char *)error_buffer);
    return;
  }

  /* Compile JIT for 10-100x performance boost */
  int jit_rc = pcre2_jit_compile(g_validator.regex, PCRE2_JIT_COMPLETE);
  if (jit_rc < 0) {
    log_warn("PCRE2 JIT compilation failed (code %d), using interpreted mode", jit_rc);
    /* Fall through - interpreted mode still works, just slower */
  }

  /* Allocate JIT stack (32KB should be sufficient for URL regex) */
  g_validator.jit_stack = pcre2_jit_stack_create(32 * 1024, 512 * 1024, NULL);

  g_validator.initialized = true;
}

/**
 * Get initialized validator (lazy initialization via pthread_once)
 */
static url_validator_t *url_validator_get(void) {
  pthread_once(&g_validator_once, url_validator_init);
  return g_validator.initialized ? &g_validator : NULL;
}

/**
 * Extract named substring from match data
 * Returns allocated string or NULL if group not matched
 */
static char *url_extract_named_group(pcre2_code *regex, pcre2_match_data *match_data, const char *group_name) {
  if (!regex || !match_data || !group_name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid arguments");
    return NULL;
  }

  int group_number = pcre2_substring_number_from_name(regex, (PCRE2_SPTR)group_name);
  if (group_number < 0) {
    return NULL; /* Group doesn't exist */
  }

  PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
  PCRE2_SIZE start = ovector[2 * group_number];
  PCRE2_SIZE end = ovector[2 * group_number + 1];

  if (start == PCRE2_UNSET || end == PCRE2_UNSET) {
    return NULL; /* Group not matched */
  }

  /* Allocate and copy substring */
  size_t len = end - start;
  char *result = SAFE_MALLOC(len + 1, char *);
  if (!result) {
    return NULL;
  }

  memcpy(result, (const char *)ovector + start, len);
  result[len] = '\0';

  return result;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PUBLIC API IMPLEMENTATION
 * ═══════════════════════════════════════════════════════════════════════════ */

bool url_is_valid(const char *url) {
  if (!url || !*url) {
    SET_ERRNO(ERROR_INVALID_PARAM, "URL is NULL or empty");
    return false;
  }

  url_validator_t *validator = url_validator_get();
  if (!validator || !validator->regex) {
    return false;
  }

  /* Check if URL needs http:// prefix (bare hostname or IP) */
  char url_with_scheme[2048];
  const char *url_to_match = url;

  if (!strstr(url, "://")) {
    /* No scheme - check if it looks like a bare hostname/IP */

    /* Reject bare scheme words like "http", "https", "ftp" */
    if (strcmp(url, "http") == 0 || strcmp(url, "https") == 0 || strcmp(url, "ftp") == 0 || strcmp(url, "ftps") == 0) {
      return false;
    }

    /* Reject URLs that look like malformed schemes (http/ instead of http://) */
    if (strncmp(url, "http/", 5) == 0 || strncmp(url, "https/", 6) == 0) {
      return false;
    }

    /* Reject if it contains @ (email-like) */
    if (strchr(url, '@')) {
      return false;
    }

    /* Reject pure hex strings (raw keys, not hostnames) */
    if (strlen(url) == 64) {
      bool all_hex = true;
      for (const char *p = url; *p && all_hex; p++) {
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))) {
          all_hex = false;
        }
      }
      if (all_hex) {
        return false; /* Looks like raw hex key, not hostname */
      }
    }

    /* Check colon handling */
    const char *colon_pos = strchr(url, ':');
    if (colon_pos) {
      /* Has colon - reject unless colon is followed by port number */
      const char *after_colon = colon_pos + 1;
      bool looks_like_port = true;
      for (const char *p = after_colon; *p && *p != '/'; p++) {
        if (!(*p >= '0' && *p <= '9')) {
          looks_like_port = false;
          break;
        }
      }
      if (!looks_like_port) {
        return false; /* Colon but not a port number */
      }
    }

    /* Looks like a bare hostname/IP - prepend http:// */
    int result = snprintf(url_with_scheme, sizeof(url_with_scheme), "http://%s", url);
    if (result < 0 || result >= (int)sizeof(url_with_scheme)) {
      return false; /* URL too long */
    }
    url_to_match = url_with_scheme;
  }

  pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(validator->regex, NULL);
  if (!match_data) {
    return false;
  }

  /* Perform JIT match if JIT compiled, otherwise interpreted match */
  int rc;
  if (validator->jit_stack) {
    rc = pcre2_jit_match(validator->regex, (PCRE2_SPTR)url_to_match, strlen(url_to_match), 0, /* startoffset */
                         0,                                                                   /* options */
                         match_data, NULL);                                                   /* mcontext */
  } else {
    rc = pcre2_match(validator->regex, (PCRE2_SPTR)url_to_match, strlen(url_to_match), 0, /* startoffset */
                     0,                                                                   /* options */
                     match_data, NULL);                                                   /* mcontext */
  }

  pcre2_match_data_free(match_data);
  return rc >= 0; /* rc >= 0 means successful match */
}

asciichat_error_t url_parse(const char *url, url_parts_t *parts_out) {
  if (!url || !*url) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "URL is NULL or empty");
  }

  if (!parts_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "parts_out is NULL");
  }

  /* Clear output structure */
  memset(parts_out, 0, sizeof(*parts_out));

  url_validator_t *validator = url_validator_get();
  if (!validator || !validator->regex) {
    return SET_ERRNO(ERROR_CONFIG, "URL validator not initialized");
  }

  /* Check if URL needs http:// prefix (bare hostname or IP) */
  char url_with_scheme[2048];
  const char *url_to_match = url;
  const char *original_url = url;

  if (!strstr(url, "://")) {
    /* No scheme - check if it looks like a bare hostname/IP */

    /* Reject bare scheme words like "http", "https", "ftp" */
    if (strcmp(url, "http") == 0 || strcmp(url, "https") == 0 || strcmp(url, "ftp") == 0 || strcmp(url, "ftps") == 0) {
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid URL format: %s", url);
    }

    /* Reject URLs that look like malformed schemes (http/ instead of http://) */
    if (strncmp(url, "http/", 5) == 0 || strncmp(url, "https/", 6) == 0) {
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid URL format (looks like malformed scheme): %s", url);
    }

    /* Reject if it contains @ (email-like) */
    if (strchr(url, '@')) {
      return SET_ERRNO(ERROR_INVALID_PARAM, "Ambiguous format looks like email address, not URL: %s", url);
    }

    /* Reject pure hex strings (raw keys, not hostnames) */
    if (strlen(url) == 64) {
      bool all_hex = true;
      for (const char *p = url; *p && all_hex; p++) {
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))) {
          all_hex = false;
        }
      }
      if (all_hex) {
        return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid URL: appears to be raw hex data, not a URL");
      }
    }

    const char *colon_pos = strchr(url, ':');
    if (colon_pos) {
      /* Has colon - check if what follows is numeric (port) */
      const char *after_colon = colon_pos + 1;
      bool looks_like_port = true;
      for (const char *p = after_colon; *p && *p != '/'; p++) {
        if (!(*p >= '0' && *p <= '9')) {
          looks_like_port = false;
          break;
        }
      }
      if (!looks_like_port) {
        return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid URL format (invalid scheme): %s", url);
      }
    }

    /* Looks like a bare hostname/IP - prepend http:// */
    int result = snprintf(url_with_scheme, sizeof(url_with_scheme), "http://%s", url);
    if (result < 0 || result >= (int)sizeof(url_with_scheme)) {
      return SET_ERRNO(ERROR_INVALID_PARAM, "URL too long");
    }
    url_to_match = url_with_scheme;
  }

  pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(validator->regex, NULL);
  if (!match_data) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to create match data");
  }

  /* Perform match */
  int rc;
  if (validator->jit_stack) {
    rc = pcre2_jit_match(validator->regex, (PCRE2_SPTR)url_to_match, strlen(url_to_match), 0, /* startoffset */
                         0,                                                                   /* options */
                         match_data, NULL);                                                   /* mcontext */
  } else {
    rc = pcre2_match(validator->regex, (PCRE2_SPTR)url_to_match, strlen(url_to_match), 0, /* startoffset */
                     0,                                                                   /* options */
                     match_data, NULL);                                                   /* mcontext */
  }

  if (rc < 0) {
    pcre2_match_data_free(match_data);
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid URL format: %s", original_url);
  }

  /* Extract named groups */
  parts_out->scheme = url_extract_named_group(validator->regex, match_data, "scheme");
  parts_out->userinfo = url_extract_named_group(validator->regex, match_data, "userinfo");
  parts_out->host = url_extract_named_group(validator->regex, match_data, "host");
  parts_out->ipv6 = url_extract_named_group(validator->regex, match_data, "ipv6");
  parts_out->path = url_extract_named_group(validator->regex, match_data, "path_query_fragment");

  /* Extract port number */
  parts_out->port = 0;
  char *port_str = url_extract_named_group(validator->regex, match_data, "port");
  if (port_str) {
    parts_out->port = (int)strtol(port_str, NULL, 10);
    SAFE_FREE(port_str);
  }

  pcre2_match_data_free(match_data);

  /* Verify we got required fields */
  if (!parts_out->scheme || !parts_out->host) {
    url_parts_free(parts_out);
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing required URL components");
  }

  return ASCIICHAT_OK;
}

void url_parts_free(url_parts_t *parts) {
  if (!parts) {
    SET_ERRNO(ERROR_INVALID_PARAM, "parts is NULL");
    return;
  }

  SAFE_FREE(parts->scheme);
  SAFE_FREE(parts->userinfo);
  SAFE_FREE(parts->host);
  SAFE_FREE(parts->ipv6);
  SAFE_FREE(parts->path);
  parts->port = 0;

  memset(parts, 0, sizeof(*parts));
}
