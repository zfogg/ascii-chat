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
  memcpy(m->session, p + 4, 16);
  memcpy(m->participant, p + 20, 16);
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
  memcpy(out + 4, m->session, 16);
  memcpy(out + 20, m->participant, 16);
  write32(out + 36, m->generation);
  write32(out + 40, m->sequence);
  write32(out + 44, m->upload_kbps);
  write32(out + 48, m->rtt_ms);
  out[52] = m->capabilities;
  out[53] = m->nat_tier;
  out[54] = (uint8_t)(m->port >> 8);
  out[55] = (uint8_t)m->port;
}
void consensus_handoff_init(consensus_handoff_t *s, const uint8_t session[16], const uint8_t host[16]) {
  memset(s, 0, sizeof(*s));
  memcpy(s->session, session, 16);
  memcpy(s->host, host, 16);
  s->generation = 1;
}
static consensus_handoff_peer_t *find_peer(consensus_handoff_t *s, const uint8_t id[16]) {
  for (size_t i = 0; i < s->count; ++i)
    if (memcmp(s->peers[i].id, id, 16) == 0)
      return &s->peers[i];
  return NULL;
}
asciichat_error_t consensus_handoff_join(consensus_handoff_t *s, const uint8_t id[16]) {
  if (find_peer(s, id))
    return ASCIICHAT_OK;
  if (s->count == PARTICIPANT_LIMIT)
    return SET_ERRNO(ERROR_INVALID_STATE, "Handoff membership full");
  consensus_handoff_peer_t *p = &s->peers[s->count++];
  memset(p, 0, sizeof(*p));
  memcpy(p->id, id, 16);
  p->nat_tier = 255;
  ++s->generation;
  s->ready = false;
  memset(s->successor, 0, 16);
  return ASCIICHAT_OK;
}
void consensus_handoff_leave(consensus_handoff_t *s, const uint8_t id[16]) {
  // The incumbent can leave before its first measurement creates a peer entry.
  if (memcmp(s->host, id, 16) == 0)
    s->host_lost = true;
  for (size_t i = 0; i < s->count; ++i) {
    if (memcmp(s->peers[i].id, id, 16) != 0)
      continue;
    memmove(&s->peers[i], &s->peers[i + 1], (s->count - i - 1) * sizeof(s->peers[0]));
    --s->count;
    ++s->generation;
    if (memcmp(s->host, id, 16) == 0)
      s->host_lost = true;
    if (memcmp(s->successor, id, 16) == 0) {
      memset(s->successor, 0, 16);
      s->ready = false;
    }
    return;
  }
}
asciichat_error_t consensus_handoff_receive(consensus_handoff_t *s, const consensus_handoff_message_t *m, size_t length,
                                            uint64_t now) {
  if (memcmp(s->session, m->session, 16) != 0)
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
    p->capabilities = m->capabilities & 3;
    p->nat_tier = m->nat_tier <= 4 ? m->nat_tier : 255;
    p->port = m->port;
  } else if (m->kind == HANDOFF_SAMPLE_DATA) {
    if (!p->measuring || m->sequence != p->sequence ||
        length != CONSENSUS_HANDOFF_HEADER + CONSENSUS_HANDOFF_PROBE_BYTES)
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unmatched handoff probe");
    p->measuring = false;
    uint64_t elapsed = now - p->probe_started_ms;
    if (elapsed == 0)
      elapsed = 1;
    p->valid = elapsed <= 5000;
    if (p->valid)
      p->rejected = false;
    p->upload_kbps = (uint32_t)((uint64_t)CONSENSUS_HANDOFF_PROBE_BYTES * 8 / elapsed);
    p->measured_ms = now;
  } else if (m->kind == HANDOFF_READY || m->kind == HANDOFF_REJECT) {
    if (m->generation != s->generation || m->sequence != s->round || memcmp(m->participant, s->successor, 16))
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Obsolete handoff preparation");
    if (m->kind == HANDOFF_READY && now <= s->prepare_deadline_ms)
      s->ready = true;
    else {
      p->rejected = true;
      s->ready = false;
      memset(s->successor, 0, 16);
    }
  } else
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Client cannot announce a handoff decision");
  return ASCIICHAT_OK;
}
bool consensus_handoff_tick(consensus_handoff_t *s, uint64_t now, consensus_handoff_message_t *a) {
  memset(a, 0, sizeof(*a));
  memcpy(a->session, s->session, 16);
  a->generation = s->generation;
  consensus_handoff_peer_t *incumbent = find_peer(s, s->host);
  if (incumbent && incumbent->valid && now - incumbent->measured_ms > CONSENSUS_HANDOFF_FRESH_MS)
    s->host_lost = true;
  if (s->host_lost && !s->failure_deadline_ms)
    s->failure_deadline_ms = now + CONSENSUS_HANDOFF_PREPARE_MS;
  if (s->ready && s->host_lost) {
    consensus_handoff_peer_t *p = find_peer(s, s->successor);
    if (p && p->valid && now - p->measured_ms <= CONSENSUS_HANDOFF_FRESH_MS) {
      a->kind = HANDOFF_COMMIT;
      memcpy(a->participant, s->successor, 16);
      a->sequence = s->round;
      a->port = p->port;
      a->capabilities = p->capabilities;
      memcpy(s->host, s->successor, 16);
      s->host_lost = false;
      s->ready = false;
      s->failure_deadline_ms = 0;
      s->failure_announced = false;
      for (size_t i = 0; i < s->count; ++i)
        s->peers[i].rejected = false;
      memset(s->successor, 0, 16);
      return true;
    }
    s->ready = false;
    memset(s->successor, 0, 16);
  }
  uint8_t zero[16] = {0};
  if (memcmp(s->successor, zero, 16)) {
    consensus_handoff_peer_t *p = find_peer(s, s->successor);
    if (p && p->valid && now - p->measured_ms <= CONSENSUS_HANDOFF_FRESH_MS &&
        (s->ready || now < s->prepare_deadline_ms))
      return false;
    if (p && !s->ready)
      p->rejected = true;
    memset(s->successor, 0, 16);
    s->ready = false;
  }
  participant_metrics_t metrics[PARTICIPANT_LIMIT];
  size_t count = 0;
  for (size_t i = 0; i < s->count; ++i) {
    consensus_handoff_peer_t *p = &s->peers[i];
    if (!p->valid || p->rejected || !p->capabilities || now - p->measured_ms > CONSENSUS_HANDOFF_FRESH_MS ||
        memcmp(p->id, s->host, 16) == 0)
      continue;
    participant_metrics_t *m = &metrics[count++];
    memset(m, 0, sizeof(*m));
    memcpy(m->participant_id, p->id, 16);
    m->nat_tier = p->nat_tier <= 4 ? p->nat_tier : 4;
    m->upload_kbps = p->upload_kbps;
    m->rtt_ns = 500000000; /* Unknown RTT earns no latency bonus. */
  }
  if (!count) {
    if (s->host_lost && !s->failure_announced && now >= s->failure_deadline_ms) {
      s->failure_announced = true;
      a->kind = HANDOFF_FAILED;
      a->sequence = s->round;
      return true;
    }
    return false;
  }
  int best = -1, backup = -1;
  if (consensus_election_choose_hosts(metrics, (int)count, &best, &backup) != ASCIICHAT_OK)
    return false;
  memcpy(s->successor, metrics[best].participant_id, 16);
  ++s->round;
  s->prepare_deadline_ms = now + CONSENSUS_HANDOFF_PREPARE_MS;
  a->kind = HANDOFF_PREPARE;
  memcpy(a->participant, s->successor, 16);
  a->sequence = s->round;
  return true;
}
