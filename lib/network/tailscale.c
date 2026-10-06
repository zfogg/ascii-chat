/**
 * @file network/tailscale.c
 * @brief Tailscale host identity helpers
 */

#include <ascii-chat/network/tailscale.h>

#include <ascii-chat/platform/network.h>
#include <ascii-chat/util/ip.h>

#include <string.h>

static bool is_tailscale_hostname(const char *hostname) {
  if (!hostname) {
    return false;
  }

  size_t length = strlen(hostname);
  if (length > 0 && hostname[length - 1] == '.') {
    length--;
  }
  static const char suffix[] = ".ts.net";
  size_t suffix_length = sizeof(suffix) - 1;
  if (length <= suffix_length) {
    return false;
  }

  const char *suffix_start = hostname + length - suffix_length;
  for (size_t i = 0; i < suffix_length; i++) {
    char actual = suffix_start[i];
    if (actual >= 'A' && actual <= 'Z') {
      actual = (char)(actual - 'A' + 'a');
    }
    if (actual != suffix[i]) {
      return false;
    }
  }
  return true;
}

static bool address_resolves_to_tailscale(const char *address, bool numeric_only) {
  struct addrinfo hints = {0};
  hints.ai_family = AF_UNSPEC;
  hints.ai_flags = numeric_only ? AI_NUMERICHOST : AI_CANONNAME;

  struct addrinfo *results = NULL;
  if (getaddrinfo(address, NULL, &hints, &results) != 0 || !results) {
    return false;
  }

  bool tailscale = results->ai_canonname && is_tailscale_hostname(results->ai_canonname);
  for (struct addrinfo *entry = results; !tailscale && entry; entry = entry->ai_next) {
    char hostname[NI_MAXHOST] = {0};
    if (getnameinfo(entry->ai_addr, (socklen_t)entry->ai_addrlen, hostname, sizeof(hostname), NULL, 0,
                    NI_NAMEREQD) == 0) {
      tailscale = is_tailscale_hostname(hostname);
    }
  }
  freeaddrinfo(results);
  return tailscale;
}

bool is_tailscale_host(const char *address) {
  if (!address || !address[0]) {
    return false;
  }
  if (is_tailscale_hostname(address)) {
    return true;
  }

  bool is_numeric = is_valid_ipv4(address) || is_valid_ipv6(address);
  bool is_short_hostname = !is_numeric && strchr(address, '.') == NULL;
  return (is_numeric || is_short_hostname) && address_resolves_to_tailscale(address, is_numeric);
}
