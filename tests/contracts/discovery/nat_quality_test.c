#include "../worker_assert.h"
#include "discovery/nat.h"
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/util/time.h>
#include <limits.h>
#include <string.h>

static socket_t local = INVALID_SOCKET_VALUE, remote = INVALID_SOCKET_VALUE;
static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
}
static void teardown(void) {
  if (local != INVALID_SOCKET_VALUE)
    socket_close(local);
  if (remote != INVALID_SOCKET_VALUE)
    socket_close(remote);
  local = remote = INVALID_SOCKET_VALUE;
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(nat_quality, .init = setup, .fini = teardown, .timeout = 10);

static struct sockaddr_in bind_loopback(socket_t sock) {
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
  cr_assert_eq(socket_bind(sock, (struct sockaddr *)&address, sizeof(address)), 0);
  socklen_t size = sizeof(address);
  cr_assert_eq(socket_getsockname(sock, (struct sockaddr *)&address, &size), 0);
  cr_assert_eq(socket_set_timeout(sock, 2 * NS_PER_SEC_INT), 0);
  return address;
}

Test(nat_quality, deterministic_host_selection_respects_tiers_and_tiebreakers) {
  nat_quality_t worst;
  nat_quality_init(NULL);
  nat_quality_init(&worst);
  cr_assert_eq(nat_compute_tier(NULL), 4);
  cr_assert_eq(nat_compute_tier(&worst), 4);
  nat_quality_t ranked[] = {{.lan_reachable = true},
                            {.has_public_ip = true},
                            {.upnp_available = true},
                            {.nat_type = ACIP_NAT_TYPE_RESTRICTED},
                            worst};
  for (unsigned i = 0; i < 5; ++i) {
    cr_assert_eq(nat_compute_tier(&ranked[i]), (int)i);
    for (unsigned j = 0; j < 5; ++j) {
      cr_assert_eq(nat_compare_quality(&ranked[i], &ranked[j], true), i <= j ? -1 : 1);
      cr_assert_eq(nat_compare_quality(&ranked[j], &ranked[i], false), j < i ? -1 : 1);
    }
  }
  nat_quality_t a = worst, b = worst;
  a.upload_kbps = 100;
  b.upload_kbps = 10;
  b.lan_reachable = true;
  cr_assert_eq(nat_compare_quality(&a, &b, false), -1);
  cr_assert_eq(nat_compare_quality(&b, &a, true), 1);
  a.upload_kbps = 99;
  cr_assert_eq(nat_compare_quality(&a, &b, true), 1);
  b = worst;
  b.upload_kbps = 98;
  cr_assert_eq(nat_compare_quality(&a, &b, false), -1);
  cr_assert_eq(nat_compare_quality(&b, &a, true), 1);
  b.upload_kbps = a.upload_kbps;
  a.rtt_to_acds_ns = 20;
  b.rtt_to_acds_ns = 21;
  cr_assert_eq(nat_compare_quality(&a, &b, false), -1);
  cr_assert_eq(nat_compare_quality(&b, &a, true), 1);
  cr_assert_eq(nat_compare_quality(NULL, &b, true), -1);
  cr_assert_eq(nat_compare_quality(&b, NULL, false), 1);
  const char *names[] = {"Open (Public IP)", "Full Cone", "Restricted Cone", "Port Restricted", "Symmetric"};
  for (unsigned i = 0; i < 5; ++i)
    cr_assert_str_eq(nat_type_to_string(i), names[i]);
  cr_assert_str_eq(nat_type_to_string((acip_nat_type_t)255), "Unknown");
}

Test(nat_quality, bandwidth_override_does_not_wrap_large_peer_values) {
  nat_quality_t a = {.upload_kbps = 3500000000u, .nat_type = ACIP_NAT_TYPE_SYMMETRIC};
  nat_quality_t b = {.upload_kbps = 3500000000u, .lan_reachable = true};
  cr_assert_eq(nat_compare_quality(&a, &b, true), 1);
  cr_assert_eq(nat_compare_quality(&b, &a, false), -1);
  a.upload_kbps = UINT32_MAX;
  b.upload_kbps = UINT32_MAX / 10;
  cr_assert_eq(nat_compare_quality(&a, &b, false), -1);
  cr_assert_eq(nat_compare_quality(&b, &a, true), 1);
}

Test(nat_quality, acip_roundtrip_preserves_observations_and_candidate_masks) {
  nat_quality_t in = {.has_public_ip = true,
                      .upnp_available = true,
                      .upnp_mapped_port = 0xabcd,
                      .nat_type = ACIP_NAT_TYPE_FULL_CONE,
                      .lan_reachable = true,
                      .stun_latency_ns = 123456789,
                      .public_address = "203.0.113.7",
                      .public_port = 54321,
                      .upload_kbps = 999,
                      .download_kbps = 2000,
                      .rtt_to_acds_ns = 17,
                      .jitter_ns = 3,
                      .packet_loss_pct = 9};
  uint8_t session[16], participant[16];
  memset(session, 0x12, sizeof(session));
  memset(participant, 0x34, sizeof(participant));
  for (unsigned mask = 0; mask < 8; ++mask) {
    in.has_host_candidates = !!(mask & 1);
    in.has_srflx_candidates = !!(mask & 2);
    in.has_relay_candidates = !!(mask & 4);
    acip_nat_quality_t wire;
    nat_quality_to_acip(&in, session, participant, &wire);
    cr_assert_arr_eq(wire.session_id, session, 16);
    cr_assert_arr_eq(wire.participant_id, participant, 16);
    cr_assert_eq(wire.upnp_mapped_port[0], 0xab);
    cr_assert_eq(wire.upnp_mapped_port[1], 0xcd);
    cr_assert_eq(wire.ice_candidate_types, mask);
    nat_quality_t out;
    memset(&out, 0xa5, sizeof(out));
    nat_quality_from_acip(&wire, &out);
    cr_assert(out.detection_complete);
    cr_assert_eq(out.error, ASCIICHAT_OK);
    cr_assert(out.has_public_ip && out.upnp_available && out.lan_reachable);
    cr_assert_eq(out.upnp_mapped_port, 0xabcd);
    cr_assert_eq(out.nat_type, in.nat_type);
    cr_assert_eq(out.stun_latency_ns, in.stun_latency_ns);
    cr_assert_eq(out.public_port, in.public_port);
    cr_assert_str_eq(out.public_address, in.public_address);
    cr_assert_eq(out.upload_kbps, in.upload_kbps);
    cr_assert_eq(out.download_kbps, in.download_kbps);
    cr_assert_eq(out.rtt_to_acds_ns, in.rtt_to_acds_ns);
    cr_assert_eq(out.jitter_ns, in.jitter_ns);
    cr_assert_eq(out.packet_loss_pct, in.packet_loss_pct);
    cr_assert_eq(out.has_host_candidates, in.has_host_candidates);
    cr_assert_eq(out.has_srflx_candidates, in.has_srflx_candidates);
    cr_assert_eq(out.has_relay_candidates, in.has_relay_candidates);
  }
  acip_nat_quality_t wire;
  memset(&wire, 0xa5, sizeof(wire));
  nat_quality_to_acip(NULL, NULL, NULL, &wire);
  cr_assert_eq(wire.session_id[0], 0xa5);
  nat_quality_to_acip(&in, NULL, NULL, &wire);
  uint8_t zeros[16] = {0};
  cr_assert_arr_eq(wire.session_id, zeros, 16);
  cr_assert_arr_eq(wire.participant_id, zeros, 16);
  nat_quality_to_acip(&in, NULL, NULL, NULL);
  nat_quality_from_acip(NULL, &in);
  nat_quality_from_acip(&wire, NULL);
}

typedef struct {
  unsigned mode;
} response_t;
static void *stun_reply(void *data) {
  response_t *response = data;
  uint8_t request[20], reply[40] = {1, 1};
  struct sockaddr_storage peer;
  socklen_t size = sizeof(peer);
  cr_assert_worker(socket_recvfrom(remote, request, sizeof(request), 0, (struct sockaddr *)&peer, &size) == 20);
  cr_assert_worker(request[0] == 0 && request[1] == 1 && request[4] == 0x21 && request[7] == 0x42);
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
  cr_assert_worker(socket_sendto(remote, reply, length, 0, (struct sockaddr *)&peer, size) == (ssize_t)length);
  return NULL;
}

Test(nat_quality, real_stun_probe_records_only_supported_observations_and_recovers_from_bad_replies) {
  cr_assert_eq(nat_detect_quality(NULL, NULL, 0), ERROR_INVALID_PARAM);
  for (unsigned mode = 0; mode < 4; ++mode) {
    remote = socket_create("nat-stun-peer", AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in address = bind_loopback(remote);
    char endpoint[64];
    safe_snprintf(endpoint, sizeof(endpoint), "127.0.0.1:%u", ntohs(address.sin_port));
    response_t response = {.mode = mode};
    asciichat_thread_t thread;
    cr_assert_eq(acthread_create(&thread, "nat-stun-reply", stun_reply, &response), 0);
    nat_quality_t quality;
    cr_assert_eq(nat_detect_quality(&quality, endpoint, 0), ASCIICHAT_OK);
    cr_assert_eq(thread_join(&thread, NULL), 0);
    cr_assert(quality.detection_complete && quality.has_host_candidates);
    cr_assert_not(quality.has_public_ip || quality.upnp_available);
    cr_assert_eq(quality.nat_type, ACIP_NAT_TYPE_SYMMETRIC);
    cr_assert_eq(quality.has_srflx_candidates, mode == 0);
    if (mode == 0) {
      cr_assert_str_eq(quality.public_address, "203.0.113.9");
      cr_assert_eq(quality.public_port, 40000);
    } else {
      cr_assert_eq(quality.public_port, 0);
      cr_assert_eq(quality.public_address[0], 0);
    }
    socket_close(remote);
    remote = INVALID_SOCKET_VALUE;
  }
  cr_assert_eq(options_set_string("turn_servers", "turn:127.0.0.1:3478"), ASCIICHAT_OK);
  nat_quality_t quality;
  cr_assert_eq(nat_detect_quality(&quality, "", 0), ASCIICHAT_OK);
  cr_assert(quality.has_relay_candidates && quality.has_host_candidates);
  cr_assert_not(quality.has_srflx_candidates);
}

static void *bandwidth_reply(void *data) {
  response_t *response = data;
  packet_type_t type;
  void *payload = NULL;
  size_t length = 0;
  cr_assert_worker(packet_receive(remote, &type, &payload, &length) == ASCIICHAT_OK);
  cr_assert_worker(type == PACKET_TYPE_ACIP_BANDWIDTH_TEST && length == sizeof(acip_bandwidth_test_t));
  acip_bandwidth_test_t message;
  memcpy(&message, payload, sizeof(message));
  buffer_release(payload);
  cr_assert_worker(message.test_size_bytes == 65536 && message.client_send_time_ns > 0);
  uint8_t chunk[4096];
  size_t remaining = 65536;
  while (remaining) {
    ssize_t count = socket_recv(remote, chunk, remaining < sizeof(chunk) ? remaining : sizeof(chunk), 0);
    cr_assert_worker(count > 0);
    for (ssize_t i = 0; i < count; ++i)
      cr_assert_worker(chunk[i] == 0xaa);
    remaining -= (size_t)count;
  }
  acip_bandwidth_result_t result = {.measured_upload_kbps = 1234,
                                    .measured_download_kbps = 5678,
                                    .rtt_ns = 123456,
                                    .jitter_ns = 456,
                                    .packet_loss_pct = 7};
  cr_assert_worker(packet_send(remote, response->mode == 1 ? PACKET_TYPE_ACIP_ERROR : PACKET_TYPE_ACIP_BANDWIDTH_RESULT,
                               &result, response->mode == 2 ? sizeof(result) - 1 : sizeof(result)) == ASCIICHAT_OK);
  return NULL;
}

Test(nat_quality, bandwidth_probe_validates_peer_reply_and_preserves_previous_measurement_on_error) {
  nat_quality_t quality = {.upload_kbps = 42};
  cr_assert_eq(nat_measure_bandwidth(NULL, INVALID_SOCKET_VALUE), ERROR_INVALID_PARAM);
  cr_assert_eq(nat_measure_bandwidth(&quality, INVALID_SOCKET_VALUE), ERROR_INVALID_PARAM);
  for (unsigned mode = 0; mode < 3; ++mode) {
    socket_t listener = socket_create("nat-bandwidth-listen", AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address = bind_loopback(listener);
    cr_assert_eq(socket_listen(listener, 1), 0);
    local = socket_create("nat-bandwidth-probe", AF_INET, SOCK_STREAM, 0);
    cr_assert_eq(socket_connect(local, (struct sockaddr *)&address, sizeof(address)), 0);
    remote = socket_accept(listener, NULL, NULL, "nat-bandwidth-peer");
    socket_close(listener);
    cr_assert_neq(remote, INVALID_SOCKET_VALUE);
    response_t response = {.mode = mode};
    asciichat_thread_t thread;
    cr_assert_eq(acthread_create(&thread, "nat-bandwidth-reply", bandwidth_reply, &response), 0);
    cr_assert_eq(nat_measure_bandwidth(&quality, local), mode ? ERROR_NETWORK_PROTOCOL : ASCIICHAT_OK);
    cr_assert_eq(thread_join(&thread, NULL), 0);
    cr_assert_eq(quality.upload_kbps, 1234);
    cr_assert_eq(quality.download_kbps, 5678);
    cr_assert_eq(quality.rtt_to_acds_ns, 123456);
    cr_assert_eq(quality.jitter_ns, 456);
    cr_assert_eq(quality.packet_loss_pct, 7);
    socket_close(local);
    socket_close(remote);
    local = remote = INVALID_SOCKET_VALUE;
  }
}
