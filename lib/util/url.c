/**
 * @file util/url.c
 * @brief Production-grade URL parsing and validation using PCRE2
 * @ingroup util
 *
 * Implements robust HTTP(S) URL validation using the production-grade
 * regex by Diego Perini (MIT License), compiled with PCRE2.
 */

#include "ascii-chat/common/error_codes.h"
#include <ascii-chat/util/url.h>
#include <ascii-chat/util/ip.h>
#include <unicode/uidna.h>
#ifdef __EMSCRIPTEN__
#include <unicode/udata.h>
#include <ascii-chat/util/lifecycle.h>
extern const unsigned char asciichat_icu_idna_data[];
static lifecycle_t g_icu_idna_data_lc = LIFECYCLE_INIT;
#endif
#include <ascii-chat/common.h>
#include <ascii-chat/util/pcre2.h>
#include <pcre2.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static bool diagnostic_url_end(char c) {
  return !c || (unsigned char)c <= 32 || c == '"' || c == '\'' || c == '<' || c == '>';
}

void url_redact_diagnostic(char *text, bool truncated) {
  if (!text)
    return;
  char *search = text;
  while ((search = strstr(search, "://"))) {
    char *authority = search + 3;
    char *end = authority;
    char *at = NULL;
    while (!diagnostic_url_end(*end) && *end != '/' && *end != '?' && *end != '#') {
      if (*end == '@')
        at = end;
      ++end;
    }
    // A truncated authority may end before its '@'; do not export a partial
    // userinfo value when the caller's diagnostic buffer was cut short.
    if (at)
      memset(authority, '*', (size_t)(at - authority));
    else if (truncated && !*end)
      memset(authority, '*', (size_t)(end - authority));
    char *url_end = end;
    char *query = NULL;
    while (!diagnostic_url_end(*url_end)) {
      if (!query && (*url_end == '?' || *url_end == '#'))
        query = url_end;
      ++url_end;
    }
    if (query)
      memset(query + 1, '*', (size_t)(url_end - query - 1));
    search = url_end;
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PRODUCTION-GRADE URL REGEX (Diego Perini, MIT License, extended for WebSocket)
 *
 * Supports: http/https/ws/wss, public IPv4, IPv6 with zone IDs, hostnames, localhost
 * Rejects: Private IPs, schemeless URLs, non-http(s)/ws(s) schemes
 * ═══════════════════════════════════════════════════════════════════════════ */

static const char *URL_REGEX_PATTERN =
    // SCHEME: http, https, ws, wss, or tcp (case-insensitive)
    "^(?<scheme>https?|wss?|tcp)://(?:(?<userinfo>\\S+(?::\\S*)?)@)?"
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
    // Hostname syntax and DNS lengths are checked by ICU after IDNA mapping.
    // Exclude ambiguous numeric-only hostnames and invalid IPv4 spellings.
    "|(?!\\d+(?:\\.\\d+)*(?:[:/"
    "?#]|$))[a-z0-9_.\\x{00a1}-\\x{10ffff}-]+"
    ")"
    // PORT: optional :port (1-5 digits, e.g. :8080, :443)
    "(?::(?<port>\\d{1,5}))?"
    // PATH/QUERY/FRAGMENT: optional /path, ?query, or #fragment
    "(?<path_query_fragment>[/?#]\\S*)?$";

/* ═══════════════════════════════════════════════════════════════════════════
 * PUBLIC API IMPLEMENTATION
 * ═══════════════════════════════════════════════════════════════════════════ */

asciichat_error_t hostname_to_ascii(const char *hostname, char *output, size_t capacity) {
  if (!hostname || !*hostname || !output || capacity == 0)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing hostname or output buffer");
  size_t length = strlen(hostname);
  if (length > INT32_MAX)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Hostname input exceeds ICU length range");

  // IP literals are addresses, not IDNA labels; preserve IPv6 zones and brackets.
  if (length < HOSTNAME_BUFFER_SIZE && (is_valid_ipv4(hostname) || is_valid_ipv6(hostname))) {
    if (length >= capacity)
      return SET_ERRNO(ERROR_INVALID_PARAM, "Address exceeds output capacity");
    memmove(output, hostname, length + 1);
    return ASCIICHAT_OK;
  }

  UErrorCode status = U_ZERO_ERROR;
#ifdef __EMSCRIPTEN__
  while (!lifecycle_is_initialized(&g_icu_idna_data_lc)) {
    if (lifecycle_init_once(&g_icu_idna_data_lc)) {
      udata_setCommonData(asciichat_icu_idna_data, &status);
      if (U_FAILURE(status)) {
        lifecycle_init_abort(&g_icu_idna_data_lc);
        return SET_ERRNO(ERROR_INIT, "Could not load ICU IDNA data: %s", u_errorName(status));
      }
      lifecycle_init_commit(&g_icu_idna_data_lc);
    }
  }
#endif
  UIDNA *idna = uidna_openUTS46(
      UIDNA_NONTRANSITIONAL_TO_ASCII | UIDNA_CHECK_BIDI | UIDNA_CHECK_CONTEXTJ | UIDNA_CHECK_CONTEXTO, &status);
  if (U_FAILURE(status) || !idna)
    return SET_ERRNO(ERROR_INIT, "Could not initialize ICU IDNA: %s", u_errorName(status));
  char ascii[HOSTNAME_BUFFER_SIZE];
  UIDNAInfo info = UIDNA_INFO_INITIALIZER;
  int32_t written = uidna_nameToASCII_UTF8(idna, hostname, (int32_t)length, ascii, sizeof(ascii), &info, &status);
  uidna_close(idna);
  // A trailing root dot is allowed, but does not count towards the DNS name limit.
  size_t max_length = MAX_HOSTNAME_LENGTH;
  if (written > 0 && written < (int32_t)sizeof(ascii) && ascii[written - 1] == '.')
    ++max_length;
  if (U_FAILURE(status) || info.errors || written <= 0 || (size_t)written > max_length ||
      (size_t)written >= sizeof(ascii) || (size_t)written >= capacity)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid IDNA hostname or insufficient capacity (ICU %s, errors 0x%x)",
                     u_errorName(status), (unsigned)info.errors);
  // Retain underscore hostnames used by local services, but reject other
  // non-DNS ASCII punctuation permitted by ICU when STD3 rules are disabled.
  for (int32_t i = 0; i < written; ++i) {
    char c = ascii[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_'))
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid ASCII character in IDNA hostname");
  }
  memcpy(output, ascii, (size_t)written);
  output[written] = '\0';
  return ASCIICHAT_OK;
}

bool url_is_valid(const char *url) {
  url_parts_t parts = {0};
  asciichat_error_t result = url_parse(url, &parts);
  url_parts_destroy(&parts);
  return result == ASCIICHAT_OK;
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

  /* Check if URL needs http:// prefix (bare hostname or IP) */
  char url_with_scheme[URL_BUFFER_SIZE];
  const char *url_to_match = url;
  const char *original_url = url;

  if (!strstr(url, "://")) {
    /* No scheme - check if it looks like a bare hostname/IP */
    if (strchr(url, '@'))
      return SET_ERRNO(ERROR_INVALID_PARAM, "Ambiguous email address without URL scheme");

    /* Reject bare scheme words like "http", "https", "ftp" */
    if (strcmp(url, "http") == 0 || strcmp(url, "https") == 0 || strcmp(url, "ftp") == 0 || strcmp(url, "ftps") == 0) {
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid URL format: %s", url);
    }

    /* Reject URLs that look like malformed schemes (http/ instead of http://) */
    if (strncmp(url, "http/", 5) == 0 || strncmp(url, "https/", 6) == 0) {
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid URL format (looks like malformed scheme): %s", url);
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

  int compile_error;
  PCRE2_SIZE error_offset;
  pcre2_code *regex = pcre2_compile((PCRE2_SPTR)URL_REGEX_PATTERN, PCRE2_ZERO_TERMINATED,
                                    PCRE2_CASELESS | PCRE2_UCP | PCRE2_UTF, &compile_error, &error_offset, NULL);
  if (!regex) {
    return SET_ERRNO(ERROR_CONFIG, "URL validator compilation failed at offset %zu (PCRE2 error %d)",
                     (size_t)error_offset, compile_error);
  }

  pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(regex, NULL);
  if (!match_data) {
    pcre2_code_free(regex);
    return SET_ERRNO(ERROR_MEMORY, "Failed to create match data");
  }

  /* Match using the interpreted pattern. */
  int rc = asciichat_pcre2_match(regex, (PCRE2_SPTR)url_to_match, strlen(url_to_match), 0, /* startoffset */
                                 0,                                                        /* options */
                                 match_data, NULL);                                        /* mcontext */

  if (rc < 0) {
    pcre2_match_data_free(match_data);
    pcre2_code_free(regex);
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid URL format: %s", original_url);
  }

  /* Extract named groups */
  parts_out->scheme = asciichat_pcre2_extract_named_group(regex, match_data, "scheme", url_to_match);
  parts_out->userinfo = asciichat_pcre2_extract_named_group(regex, match_data, "userinfo", url_to_match);
  parts_out->host = asciichat_pcre2_extract_named_group(regex, match_data, "host", url_to_match);
  parts_out->ipv6 = asciichat_pcre2_extract_named_group(regex, match_data, "ipv6", url_to_match);
  parts_out->path = asciichat_pcre2_extract_named_group(regex, match_data, "path_query_fragment", url_to_match);

  /* Extract port number */
  parts_out->port = 0;
  char *port_str = asciichat_pcre2_extract_named_group(regex, match_data, "port", url_to_match);
  if (port_str) {
    parts_out->port = (int)strtol(port_str, NULL, 10);
    SAFE_FREE(port_str);
    if (parts_out->port <= 0 || parts_out->port > UINT16_MAX) {
      pcre2_match_data_free(match_data);
      pcre2_code_free(regex);
      url_parts_destroy(parts_out);
      return SET_ERRNO(ERROR_INVALID_PARAM, "URL port must be in the range 1-65535");
    }
  }

  pcre2_match_data_free(match_data);
  pcre2_code_free(regex);

  /* Verify we got required fields */
  if (!parts_out->scheme || !parts_out->host) {
    url_parts_destroy(parts_out);
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing required URL components");
  }

  if (!parts_out->ipv6) {
    char ascii[HOSTNAME_BUFFER_SIZE];
    asciichat_error_t result = hostname_to_ascii(parts_out->host, ascii, sizeof(ascii));
    if (result != ASCIICHAT_OK) {
      url_parts_destroy(parts_out);
      return result;
    }
    SAFE_FREE(parts_out->host);
    SAFE_STRDUP(parts_out->host, ascii);
  }
  return ASCIICHAT_OK;
}

void url_parts_destroy(url_parts_t *parts) {
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

bool url_is_websocket_scheme(const char *scheme) {
  if (!scheme || !*scheme) {
    SET_ERRNO(ERROR_INVALID_PARAM, "scheme is NULL or empty");
    return false;
  }

  /* Case-insensitive comparison for "ws" or "wss" */
  return (strcasecmp(scheme, "ws") == 0 || strcasecmp(scheme, "wss") == 0);
}

bool url_is_websocket(const char *url) {
  if (!url || !*url) {
    SET_ERRNO(ERROR_INVALID_PARAM, "url is NULL or empty");
    return false;
  }

  /* Parse URL to validate and check scheme */
  url_parts_t parts = {0};
  asciichat_error_t result = url_parse(url, &parts);
  bool is_ws = (result == ASCIICHAT_OK && url_is_websocket_scheme(parts.scheme));
  url_parts_destroy(&parts);

  return is_ws;
}

bool url_looks_like_websocket(const char *url) {
  if (!url || !*url) {
    SET_ERRNO(ERROR_INVALID_PARAM, "url is NULL or empty");
    return false;
  }

  /* Quick check for ws:// or wss:// prefix (case-insensitive) */
  return (strncasecmp(url, "ws://", 5) == 0 || strncasecmp(url, "wss://", 6) == 0);
}

bool url_is_tcp_scheme(const char *scheme) {
  if (!scheme || !*scheme) {
    SET_ERRNO(ERROR_INVALID_PARAM, "scheme is NULL or empty");
    return false;
  }

  /* Case-insensitive comparison for "tcp" */
  return (strcasecmp(scheme, "tcp") == 0);
}

bool url_is_tcp(const char *url) {
  if (!url || !*url) {
    SET_ERRNO(ERROR_INVALID_PARAM, "url is NULL or empty");
    return false;
  }

  /* Parse URL to validate and check scheme */
  url_parts_t parts = {0};
  asciichat_error_t result = url_parse(url, &parts);
  bool is_tcp = (result == ASCIICHAT_OK && url_is_tcp_scheme(parts.scheme));
  url_parts_destroy(&parts);

  return is_tcp;
}

bool url_looks_like_tcp(const char *url) {
  if (!url || !*url) {
    SET_ERRNO(ERROR_INVALID_PARAM, "url is NULL or empty");
    return false;
  }

  /* Quick check for tcp:// prefix (case-insensitive) */
  return (strncasecmp(url, "tcp://", 6) == 0);
}
