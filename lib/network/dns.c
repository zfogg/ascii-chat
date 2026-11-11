#include <ascii-chat/util/url.h>
/**
 * @file network/dns.c
 * @brief DNS resolution utilities implementation
 */

#include <ascii-chat/otel.h>
#include <ascii-chat/network/dns.h>
#include <ascii-chat/os/network.h>
#include <ascii-chat/log/log.h>
#include <string.h>

bool dns_test_connectivity(const char *hostname) {
  ASCII_OTEL_SCOPE(DNS_TEST_CONNECTIVITY);
  if (!hostname) {
    log_warn("NULL hostname provided to DNS connectivity test");
    return false;
  }

  char ascii_hostname[HOSTNAME_BUFFER_SIZE];
  asciichat_error_t normalized = hostname_to_ascii(hostname, ascii_hostname, sizeof(ascii_hostname));
  if (normalized != ASCIICHAT_OK)
    return false;
  hostname = ascii_hostname;

  struct addrinfo hints, *result = NULL;
  memset(&hints, 0, sizeof(hints));
  /* Connectivity is not IPv4-specific.  Restricting this probe to AF_INET
   * reports a false DNS failure on IPv6-only or IPv6-preferred networks even
   * though the HTTP client can connect successfully. */
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  log_debug("Testing DNS connectivity to %s...", hostname);
  log_debug("★ ABOUT TO CALL getaddrinfo for %s", hostname);

  /* Resolve the same service used by the update request.  Passing a NULL
   * service is rejected by some Windows resolver configurations even though
   * hostname resolution itself is healthy. */
  int ret = getaddrinfo(hostname, "443", &hints, &result);
  log_debug("★ getaddrinfo returned %d", ret);
  if (ret != 0) {
    log_warn("DNS resolution failed for %s: %s", hostname, gai_strerror(ret));
    return false;
  }

  if (result) {
    freeaddrinfo(result);
  }

  log_debug("DNS connectivity test succeeded for %s", hostname);
  return true;
}
