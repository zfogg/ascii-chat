/**
 * @file network/dns.c
 * @brief DNS resolution utilities implementation
 */

#include <ascii-chat/network/dns.h>
#include <ascii-chat/platform/network.h>
#include <ascii-chat/log/log.h>
#include <string.h>

bool dns_test_connectivity(const char *hostname) {
  if (!hostname) {
    log_warn("NULL hostname provided to DNS connectivity test");
    return false;
  }

  struct addrinfo hints, *result = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET; // IPv4
  hints.ai_socktype = SOCK_STREAM;

  log_debug("Testing DNS connectivity to %s...", hostname);
  log_info("★ ABOUT TO CALL getaddrinfo for %s", hostname);

  // Use NULL for port to avoid musl strtoul parsing bug with string port numbers
  // The actual port doesn't matter for connectivity testing - we just need DNS resolution
  int ret = getaddrinfo(hostname, NULL, &hints, &result);
  log_info("★ getaddrinfo returned %d", ret);
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
