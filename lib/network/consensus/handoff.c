#include <ascii-chat/common/limits.h>
#include <ascii-chat/crypto/sizes.h>
#include <ascii-chat/otel.h>
#include <ascii-chat/network/consensus/handoff.h>
#include <string.h>

static uint32_t read32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void write32(uint8_t *p, uint32_t n) {
  p[0] = (uint8_t)(n >> 24);
  p[1] = (uint8_t)(n >> 16);
  p[2] = (uint8_t)(n >> 8);
  p[3] = (uint8_t)n;
}
asciichat_error_t consensus_handoff_decode(const void *data, size_t length, consensus_handoff_message_t *m) {
  if (!data || !m || length < CONSENSUS_HANDOFF_HEADER)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Truncated handoff frame");
  const uint8_t *p = data;
  if (p[0] != CONSENSUS_HANDOFF_VERSION || p[1] < HANDOFF_SAMPLE_START || p[1] > HANDOFF_SAMPLE_ACK ||
      (p[1] == HANDOFF_SAMPLE_DATA ? length != CONSENSUS_HANDOFF_HEADER + CONSENSUS_HANDOFF_PROBE_BYTES
                                   : length != CONSENSUS_HANDOFF_HEADER))
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid handoff version, kind or length");
  memset(m, 0, sizeof(*m));
  m->kind = p[1];
  memcpy(m->session, p + 4, SESSION_ID_SIZE);
  memcpy(m->participant, p + 20, SESSION_ID_SIZE);
  m->generation = read32(p + 36);
  m->sequence = read32(p + 40);
  m->upload_kbps = read32(p + 44);
  m->rtt_ms = read32(p + 48);
  m->capabilities = p[52];
  m->nat_tier = p[53];
  m->port = (uint16_t)((p[54] << 8) | p[55]);
  return ASCIICHAT_OK;
}
void consensus_handoff_encode(const consensus_handoff_message_t *m, uint8_t out[CONSENSUS_HANDOFF_HEADER]) {
  memset(out, 0, CONSENSUS_HANDOFF_HEADER);
  out[0] = CONSENSUS_HANDOFF_VERSION;
  out[1] = m->kind;
  memcpy(out + 4, m->session, SESSION_ID_SIZE);
  memcpy(out + 20, m->participant, SESSION_ID_SIZE);
  write32(out + 36, m->generation);
  write32(out + 40, m->sequence);
  write32(out + 44, m->upload_kbps);
  write32(out + 48, m->rtt_ms);
  out[52] = m->capabilities;
  out[53] = m->nat_tier;
  out[54] = (uint8_t)(m->port >> 8);
  out[55] = (uint8_t)m->port;
}
void consensus_handoff_init(consensus_handoff_t *s, const uint8_t session[SESSION_ID_SIZE],
                            const uint8_t host[SESSION_ID_SIZE]) {
  memset(s, 0, sizeof(*s));
  memcpy(s->session, session, SESSION_ID_SIZE);
  memcpy(s->host, host, SESSION_ID_SIZE);
  s->generation = 1;
}
static consensus_handoff_peer_t *find_peer(consensus_handoff_t *s, const uint8_t id[SESSION_ID_SIZE]) {
  for (size_t i = 0; i < s->count; ++i)
    if (memcmp(s->peers[i].id, id, SESSION_ID_SIZE) == 0)
      return &s->peers[i];
  return NULL;
}
asciichat_error_t consensus_handoff_join(consensus_handoff_t *s, const uint8_t id[SESSION_ID_SIZE]) {
  if (find_peer(s, id))
    return ASCIICHAT_OK;
  if (s->count == MAX_SESSION_PARTICIPANTS)
    return SET_ERRNO(ERROR_INVALID_STATE, "Handoff membership full");
  consensus_handoff_peer_t *p = &s->peers[s->count++];
  memset(p, 0, sizeof(*p));
  memcpy(p->id, id, SESSION_ID_SIZE);
  p->nat_tier = 255;
  ++s->generation;
  s->ready = false;
  memset(s->successor, 0, SESSION_ID_SIZE);
  return ASCIICHAT_OK;
}
void consensus_handoff_leave(consensus_handoff_t *s, const uint8_t id[SESSION_ID_SIZE]) {
  // The incumbent can leave before its first measurement creates a peer entry.
  if (memcmp(s->host, id, SESSION_ID_SIZE) == 0)
    s->host_lost = true;
  for (size_t i = 0; i < s->count; ++i) {
    if (memcmp(s->peers[i].id, id, SESSION_ID_SIZE) != 0)
      continue;
    memmove(&s->peers[i], &s->peers[i + 1], (s->count - i - 1) * sizeof(s->peers[0]));
    --s->count;
    ++s->generation;
    if (memcmp(s->host, id, SESSION_ID_SIZE) == 0)
      s->host_lost = true;
    if (memcmp(s->successor, id, SESSION_ID_SIZE) == 0) {
      memset(s->successor, 0, SESSION_ID_SIZE);
      s->ready = false;
    }
    return;
  }
}
/**
 * @brief Record loss of the incumbent, ignoring delayed reports about old hosts.
 * The caller authenticates the reporter before the next tick starts recovery.
 */
void consensus_handoff_host_lost(consensus_handoff_t *s, const uint8_t host[SESSION_ID_SIZE]) {
  if (!memcmp(s->host, host, sizeof(s->host)))
    s->host_lost = true;
}

/**
 * @brief Accept a member's probe or preparation reply using receiver-clock time.
 * Probe metadata becomes eligible only with a matched, timely body. Preparation
 * replies must match the current generation, round, and selected successor.
 * @return ASCIICHAT_OK if accepted, or a protocol error without accepting the frame.
 */
asciichat_error_t consensus_handoff_receive(consensus_handoff_t *s, const consensus_handoff_message_t *m, size_t length,
                                            uint64_t now) {
  if (memcmp(s->session, m->session, SESSION_ID_SIZE) != 0)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Wrong handoff session");
  consensus_handoff_peer_t *p = find_peer(s, m->participant);
  if (!p)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Handoff sender is not a member");
  if (m->kind == HANDOFF_SAMPLE_START) {
    if (m->sequence <= p->sequence || (p->measured_ms && now - p->measured_ms < 1000))
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Repeated or excessive handoff probe");
    p->sequence = m->sequence;
    p->probe_started_ms = now;
    p->measuring = true;
    p->pending_capabilities = m->capabilities & 3;
    p->pending_nat_tier = m->nat_tier <= 4 ? m->nat_tier : 255;
    p->pending_port = m->port;
  } else if (m->kind == HANDOFF_SAMPLE_DATA) {
    if (!p->measuring || m->sequence != p->sequence ||
        length != CONSENSUS_HANDOFF_HEADER + CONSENSUS_HANDOFF_PROBE_BYTES)
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unmatched handoff probe");
    p->measuring = false;
    uint64_t elapsed = now - p->probe_started_ms;
    if (elapsed == 0)
      elapsed = 1;
    p->valid = elapsed <= 5000;
    if (p->valid) {
      p->capabilities = p->pending_capabilities;
      p->nat_tier = p->pending_nat_tier;
      p->port = p->pending_port;
      // A quality refresh cannot extend a failed recovery by retrying the same host.
      if (!s->host_lost)
        p->rejected = false;
    }
    p->upload_kbps = (uint32_t)((uint64_t)CONSENSUS_HANDOFF_PROBE_BYTES * 8 / elapsed);
    p->measured_ms = now;
  } else if (m->kind == HANDOFF_READY || m->kind == HANDOFF_REJECT) {
    if (m->generation != s->generation || m->sequence != s->round ||
        memcmp(m->participant, s->successor, SESSION_ID_SIZE))
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Obsolete handoff preparation");
    if (m->kind == HANDOFF_READY && now <= s->prepare_deadline_ms)
      s->ready = true;
    else {
      p->rejected = true;
      s->ready = false;
      memset(s->successor, 0, SESSION_ID_SIZE);
    }
  } else
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Client cannot announce a handoff decision");
  return ASCIICHAT_OK;
}
// Record decisions, not periodic ticks or per-peer measurements.
static void handoff_record_action(consensus_handoff_t *state, const consensus_handoff_message_t *action, uint64_t now,
                                  size_t candidates) {
  ASCII_OTEL_SCOPE(DISCOVERY_HOST_SELECTION);
  const char *outcome = action->kind == HANDOFF_PREPARE  ? "selected"
                        : action->kind == HANDOFF_COMMIT ? "committed"
                                                         : "failed";
  ASCII_OTEL_ATTR_STRING("host.selection.phase", "successor");
  ASCII_OTEL_ATTR_STRING("host.selection.outcome", outcome);
  ASCII_OTEL_ATTR_STRING("host.selection.reason", action->kind == HANDOFF_PREPARE  ? "best_eligible_quality"
                                                  : action->kind == HANDOFF_COMMIT ? "prepared_successor"
                                                                                   : "no_eligible_successor");
  ASCII_OTEL_ATTR_INT("host.generation", state->generation);
  ASCII_OTEL_ATTR_INT("host.selection.round", state->round);
  if (action->kind != HANDOFF_COMMIT)
    ASCII_OTEL_ATTR_INT("host.selection.eligible_candidates", (int64_t)candidates);
  ASCII_OTEL_ATTR_INT("host.selection.host_lost", state->host_lost);
  consensus_handoff_peer_t *peer = find_peer(state, action->participant);
  if (peer) {
    ASCII_OTEL_ATTR_INT("host.selection.upload_kbps", peer->upload_kbps);
    ASCII_OTEL_ATTR_INT("host.selection.nat_tier", peer->nat_tier);
  }
  ascii_otel_metric(action->kind == HANDOFF_PREPARE  ? ASCII_OTEL_METRIC_HOST_SELECTIONS
                    : action->kind == HANDOFF_COMMIT ? ASCII_OTEL_METRIC_HOST_COMMITS
                                                     : ASCII_OTEL_METRIC_HOST_FAILURES,
                    1);
  if (action->kind != HANDOFF_PREPARE && state->failure_deadline_ms >= CONSENSUS_HANDOFF_RECOVERY_MS) {
    uint64_t started = state->failure_deadline_ms - CONSENSUS_HANDOFF_RECOVERY_MS;
    if (now >= started)
      ascii_otel_metric(ASCII_OTEL_METRIC_HOST_RECOVERY_DURATION, (double)(now - started) / 1000.0);
  }
}

/**
 * @brief Advance measured successor selection and bounded host-loss recovery.
 * Fresh eligible peers prepare within five seconds; all recovery attempts share
 * one 15-second budget. Only a fresh prepared peer can commit, and failure is terminal.
 * @param now Monotonic receiver time in milliseconds.
 * @param a Output action to send when the function returns true.
 * @return True if a PREPARE, COMMIT, or terminal FAILED action was produced.
 */
bool consensus_handoff_tick(consensus_handoff_t *s, uint64_t now, consensus_handoff_message_t *a) {
  memset(a, 0, sizeof(*a));
  memcpy(a->session, s->session, SESSION_ID_SIZE);
  a->generation = s->generation;
  if (s->failure_announced)
    return false; // Terminal until the session is recreated.
  if (!s->monitoring) {
    s->monitoring = true;
    s->monitoring_started_ms = now;
  }
  consensus_handoff_peer_t *incumbent = find_peer(s, s->host);
  uint64_t last_host_sample = incumbent && incumbent->valid ? incumbent->measured_ms : s->monitoring_started_ms;
  if (now - last_host_sample > CONSENSUS_HANDOFF_FRESH_MS)
    s->host_lost = true;
  if (s->host_lost && !s->failure_deadline_ms)
    s->failure_deadline_ms = now + CONSENSUS_HANDOFF_RECOVERY_MS;
  // The recovery budget is shared by all candidates, including silent/rejecting
  // candidates that continue sending fresh probes. A late READY cannot revive it.
  if (s->host_lost && now >= s->failure_deadline_ms) {
    s->failure_announced = true;
    s->ready = false;
    memset(s->successor, 0, sizeof(s->successor));
    a->kind = HANDOFF_FAILED;
    a->sequence = s->round;
    handoff_record_action(s, a, now, 0);
    return true;
  }
  if (s->ready && s->host_lost) {
    consensus_handoff_peer_t *p = find_peer(s, s->successor);
    if (p && p->valid && p->capabilities && now - p->measured_ms <= CONSENSUS_HANDOFF_FRESH_MS) {
      a->kind = HANDOFF_COMMIT;
      memcpy(a->participant, s->successor, SESSION_ID_SIZE);
      a->sequence = s->round;
      a->port = p->port;
      a->capabilities = p->capabilities;
      handoff_record_action(s, a, now, 1);
      s->host_sequence = a->sequence;
      memcpy(s->host, s->successor, SESSION_ID_SIZE);
      s->host_lost = false;
      s->monitoring_started_ms = now;
      s->ready = false;
      s->failure_deadline_ms = 0;
      s->failure_announced = false;
      for (size_t i = 0; i < s->count; ++i)
        s->peers[i].rejected = false;
      memset(s->successor, 0, SESSION_ID_SIZE);
      return true;
    }
    s->ready = false;
    memset(s->successor, 0, SESSION_ID_SIZE);
  }
  uint8_t zero[SESSION_ID_SIZE] = {0};
  if (memcmp(s->successor, zero, SESSION_ID_SIZE)) {
    consensus_handoff_peer_t *p = find_peer(s, s->successor);
    if (p && p->valid && p->capabilities && now - p->measured_ms <= CONSENSUS_HANDOFF_FRESH_MS &&
        (s->ready || now < s->prepare_deadline_ms))
      return false;
    if (p && !s->ready)
      p->rejected = true;
    memset(s->successor, 0, SESSION_ID_SIZE);
    s->ready = false;
  }
  participant_metrics_t metrics[MAX_SESSION_PARTICIPANTS];
  size_t count = 0;
  for (size_t i = 0; i < s->count; ++i) {
    consensus_handoff_peer_t *p = &s->peers[i];
    if (!p->valid || p->rejected || !p->capabilities || now - p->measured_ms > CONSENSUS_HANDOFF_FRESH_MS ||
        memcmp(p->id, s->host, SESSION_ID_SIZE) == 0)
      continue;
    participant_metrics_t *m = &metrics[count++];
    memset(m, 0, sizeof(*m));
    memcpy(m->participant_id, p->id, SESSION_ID_SIZE);
    m->nat_tier = p->nat_tier <= 4 ? p->nat_tier : 4;
    m->upload_kbps = p->upload_kbps;
    m->rtt_ns = 500000000; /* Unknown RTT earns no latency bonus. */
  }
  if (!count)

    return false;
  int best = -1, backup = -1;
  if (consensus_election_choose_hosts(metrics, (int)count, &best, &backup) != ASCIICHAT_OK)
    return false;
  memcpy(s->successor, metrics[best].participant_id, SESSION_ID_SIZE);
  ++s->round;
  s->prepare_deadline_ms = now + CONSENSUS_HANDOFF_PREPARE_MS;
  a->kind = HANDOFF_PREPARE;
  memcpy(a->participant, s->successor, SESSION_ID_SIZE);
  a->sequence = s->round;
  handoff_record_action(s, a, now, count);
  return true;
}
