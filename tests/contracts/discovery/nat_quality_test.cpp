#include "../worker_assert.h"
#include "discovery/nat.h"
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/util/time.h>
#include <limits.h>
#include <string.h>

static socket_t remote = INVALID_SOCKET_VALUE;
static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
  ASCIICHAT_REQUIRE_EQ(options_state_init(), ASCIICHAT_OK);
}
/** @brief Release the loopback STUN fixture and restore per-case options/logging. */
static void teardown(void) {
  if (remote != INVALID_SOCKET_VALUE)
    socket_close(remote);
  remote = INVALID_SOCKET_VALUE;
  options_state_destroy();
  errno_notifications_enable(false);
  CLEAR_ERRNO_ALL();
  log_destroy();
}
static struct sockaddr_in bind_loopback(socket_t sock) {
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
  ASCIICHAT_REQUIRE_EQ(socket_bind(sock, (struct sockaddr *)&address, sizeof(address)), 0);
  socklen_t size = sizeof(address);
  ASCIICHAT_REQUIRE_EQ(socket_getsockname(sock, (struct sockaddr *)&address, &size), 0);
  ASCIICHAT_REQUIRE_EQ(socket_set_timeout(sock, 2 * NS_PER_SEC_INT), 0);
  return address;
}

TEST_CASE("nat_quality/deterministic_host_selection_respects_tiers_and_tiebreakers", "[nat_quality]") {
  nat_quality_t worst;
  nat_quality_init(NULL);
  nat_quality_init(&worst);
  ASCIICHAT_REQUIRE_EQ(nat_compute_tier(NULL), 4);
  ASCIICHAT_REQUIRE_EQ(nat_compute_tier(&worst), 4);
  nat_quality_t ranked[] = {{.lan_reachable = true},
                            {.has_public_ip = true},
                            {.upnp_available = true},
                            {.nat_type = ACIP_NAT_TYPE_RESTRICTED},
                            worst};
  for (unsigned i = 0; i < 5; ++i) {
    ASCIICHAT_REQUIRE_EQ(nat_compute_tier(&ranked[i]), (int)i);
    for (unsigned j = 0; j < 5; ++j) {
      ASCIICHAT_REQUIRE_EQ(nat_compare_quality(&ranked[i], &ranked[j], true), i <= j ? -1 : 1);
      ASCIICHAT_REQUIRE_EQ(nat_compare_quality(&ranked[j], &ranked[i], false), j < i ? -1 : 1);
    }
  }
  nat_quality_t a = worst, b = worst;
  a.upload_kbps = 100;
  b.upload_kbps = 10;
  b.lan_reachable = true;
  ASCIICHAT_REQUIRE_EQ(nat_compare_quality(&a, &b, false), -1);
  ASCIICHAT_REQUIRE_EQ(nat_compare_quality(&b, &a, true), 1);
  a.upload_kbps = 99;
  ASCIICHAT_REQUIRE_EQ(nat_compare_quality(&a, &b, true), 1);
  b = worst;
  b.upload_kbps = 98;
  ASCIICHAT_REQUIRE_EQ(nat_compare_quality(&a, &b, false), -1);
  ASCIICHAT_REQUIRE_EQ(nat_compare_quality(&b, &a, true), 1);
  b.upload_kbps = a.upload_kbps;
  a.rtt_to_acds_ns = 20;
  b.rtt_to_acds_ns = 21;
  ASCIICHAT_REQUIRE_EQ(nat_compare_quality(&a, &b, false), -1);
  ASCIICHAT_REQUIRE_EQ(nat_compare_quality(&b, &a, true), 1);
  ASCIICHAT_REQUIRE_EQ(nat_compare_quality(NULL, &b, true), -1);
  ASCIICHAT_REQUIRE_EQ(nat_compare_quality(&b, NULL, false), 1);
  const char *names[] = {"Open (Public IP)", "Full Cone", "Restricted Cone", "Port Restricted", "Symmetric"};
  for (unsigned i = 0; i < 5; ++i)
    ASCIICHAT_REQUIRE_STR_EQ(nat_type_to_string(i), names[i]);
  ASCIICHAT_REQUIRE_STR_EQ(nat_type_to_string((acip_nat_type_t)255), "Unknown");
}

TEST_CASE("nat_quality/bandwidth_override_does_not_wrap_large_peer_values", "[nat_quality]") {
  nat_quality_t a = {.upload_kbps = 3500000000u, .nat_type = ACIP_NAT_TYPE_SYMMETRIC};
  nat_quality_t b = {.upload_kbps = 3500000000u, .lan_reachable = true};
  ASCIICHAT_REQUIRE_EQ(nat_compare_quality(&a, &b, true), 1);
  ASCIICHAT_REQUIRE_EQ(nat_compare_quality(&b, &a, false), -1);
  a.upload_kbps = UINT32_MAX;
  b.upload_kbps = UINT32_MAX / 10;
  ASCIICHAT_REQUIRE_EQ(nat_compare_quality(&a, &b, false), -1);
  ASCIICHAT_REQUIRE_EQ(nat_compare_quality(&b, &a, true), 1);
}

typedef struct {
  unsigned mode;
} response_t;
static void *stun_reply(void *data) {
  response_t *response = data;
  uint8_t request[20], reply[40] = {1, 1};
  struct sockaddr_storage peer;
  socklen_t size = sizeof(peer);
  ASCIICHAT_WORKER_ASSERT(socket_recvfrom(remote, request, sizeof(request), 0, (struct sockaddr *)&peer, &size) == 20);
  ASCIICHAT_WORKER_ASSERT(request[0] == 0 && request[1] == 1 && request[4] == 0x21 && request[7] == 0x42);
  memcpy(reply + 4, request + 4, 16);
  // An unknown one-byte attribute exercises four-byte padding before MAPPED-ADDRESS.
  const uint8_t attrs[] = {0x80, 0x22, 0, 1, 0x11, 0, 0, 0, 0, 1, 0, 8, 0, 1, 0x9c, 0x40, 203, 0, 113, 9};
  memcpy(reply + 20, attrs, sizeof(attrs));
  reply[3] = sizeof(attrs);
  size_t length = sizeof(reply);
  if (response->mode == 1)
    length = 19;
  if (response->mode == 2)
    reply[31] = 30;
  if (response->mode == 3)
    reply[33] = 2;
  if (response->mode == 4) {
    reply[29] = 0x20;
    reply[34] ^= 0x21;
    reply[35] ^= 0x12;
    reply[36] ^= 0x21;
    reply[37] ^= 0x12;
    reply[38] ^= 0xa4;
    reply[39] ^= 0x42;
  }
  ASCIICHAT_WORKER_ASSERT(socket_sendto(remote, reply, length, 0, (struct sockaddr *)&peer, size) == (ssize_t)length);
  return NULL;
}

TEST_CASE("nat_quality/real_stun_probe_records_only_supported_observations_and_recovers_from_bad_replies", "[nat_quality]") {
  ASCIICHAT_REQUIRE_EQ(nat_detect_quality(NULL, NULL, 0), ERROR_INVALID_PARAM);
  CLEAR_ERRNO_ALL();
  errno_notifications_enable(true);
  for (unsigned mode = 0; mode < 5; ++mode) {
    remote = socket_create("nat-stun-peer", AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in address = bind_loopback(remote);
    char endpoint[64];
    safe_snprintf(endpoint, sizeof(endpoint), "127.0.0.1:%u", ntohs(address.sin_port));
    response_t response = {.mode = mode};
    platform_thread_t thread;
    ASCIICHAT_REQUIRE_EQ(platform_thread_create(&thread, "nat-stun-reply", stun_reply, &response), 0);
    nat_quality_t quality;
    ASCIICHAT_REQUIRE_EQ(nat_detect_quality(&quality, endpoint, 0), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_NOT(errno_notifications_pending(), "Recovered STUN failure must not notify the UI");
    ASCIICHAT_REQUIRE_NOT(HAS_ERRNO(NULL), "Recovered STUN failure must not remain pending");
    ASCIICHAT_REQUIRE_EQ(thread_join(&thread, NULL), 0);
    ASCIICHAT_REQUIRE(quality.detection_complete && quality.has_host_candidates);
    ASCIICHAT_REQUIRE_NOT(quality.has_public_ip || quality.upnp_available);
    ASCIICHAT_REQUIRE_EQ(quality.nat_type, ACIP_NAT_TYPE_SYMMETRIC);
    ASCIICHAT_REQUIRE_EQ(quality.has_srflx_candidates, mode == 0 || mode == 4);
    if (mode == 0 || mode == 4) {
      ASCIICHAT_REQUIRE_STR_EQ(quality.public_address, "203.0.113.9");
      ASCIICHAT_REQUIRE_EQ(quality.public_port, 40000);
    } else {
      ASCIICHAT_REQUIRE_EQ(quality.public_port, 0);
      ASCIICHAT_REQUIRE_EQ(quality.public_address[0], 0);
    }
    socket_close(remote);
    remote = INVALID_SOCKET_VALUE;
  }
  ASCIICHAT_REQUIRE_EQ(options_set_string("turn_servers", "turn:127.0.0.1:3478"), ASCIICHAT_OK);
  nat_quality_t quality;
  ASCIICHAT_REQUIRE_EQ(nat_detect_quality(&quality, "", 0), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(quality.has_relay_candidates && quality.has_host_candidates);
  ASCIICHAT_REQUIRE_NOT(quality.has_srflx_candidates);
}
