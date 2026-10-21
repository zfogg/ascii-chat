#include <ascii-chat/network/acip/backpressure.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/platform/cond.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <string.h>

#define OUTBOUND_MAX_PACKETS 512
#define OUTBOUND_MAX_STREAMS 64
#define WIRE_OVERHEAD (sizeof(packet_header_t) + CRYPTO_NONCE_SIZE + CRYPTO_MAC_SIZE)

typedef struct outbound_packet {
  struct outbound_packet *next;
  uint8_t *data;
  size_t size, cost;
  uint64_t created;
  uint16_t type;
  uint32_t stream;
  bool video, encoded, keyframe, audio, wait, done;
  asciichat_error_t result;
} outbound_packet_t;

typedef struct {
  uint16_t type;
  uint32_t stream;
  _Atomic(bool) needs_keyframe;
} outbound_stream_t;

struct acip_outbound {
  acip_transport_t *transport;
  mutex_t mutex;
  cond_t changed;
  asciichat_thread_t worker;
  bool stopping, failed;
  asciichat_error_t failure_code;
  asciichat_error_context_t failure;
  asciichat_error_context_t failure_detail;
  outbound_packet_t *head;
  outbound_packet_t *recycled;
  size_t recycled_count;
  size_t count, bytes, active_bytes;
  outbound_stream_t streams[OUTBOUND_MAX_STREAMS];
  _Atomic(size_t) stream_count;
  acip_outbound_stats_t stats;
};

static size_t backend_bytes(acip_outbound_t *out) {
  return out->transport->methods->queued_bytes ? out->transport->methods->queued_bytes(out->transport) : 0;
}

static outbound_stream_t *stream_state(acip_outbound_t *out, uint16_t type, uint32_t id) {
  for (size_t i = 0; i < out->stream_count; ++i)
    if (out->streams[i].type == type && out->streams[i].stream == id)
      return &out->streams[i];
  if (out->stream_count == OUTBOUND_MAX_STREAMS)
    return NULL;
  size_t index = out->stream_count;
  outbound_stream_t *stream = &out->streams[index];
  *stream = (outbound_stream_t){.type = type, .stream = id, .needs_keyframe = true};
  // Publish immutable identity only after initialization. The keyframe bit is
  // atomic so the capture worker can query it without locking the packet queue.
  out->stream_count = index + 1;
  return stream;
}

// Keep a small connection-owned working set instead of visiting the global
// allocator and buffer domain for every media packet. All access holds mutex.
static void packet_free(acip_outbound_t *out, outbound_packet_t *packet) {
  if (!out->stopping && !out->failed && out->recycled_count < 8 && buffer_capacity(packet->data) <= 256 * 1024) {
    packet->next = out->recycled;
    out->recycled = packet;
    ++out->recycled_count;
    return;
  }
  buffer_release(packet->data);
  SAFE_FREE(packet);
}

static bool same_stream(const outbound_packet_t *a, const outbound_packet_t *b) {
  return a->type == b->type && a->stream == b->stream;
}

/* Conservatively invalidate all unsent encoded output for this stream. The
 * producer requests an IDR with parameter sets before resuming its chain. */
static void discard_stream(acip_outbound_t *out, const outbound_packet_t *video) {
  if (video->encoded) {
    outbound_stream_t *stream = stream_state(out, video->type, video->stream);
    if (stream)
      stream->needs_keyframe = true;
  }
  outbound_packet_t **link = &out->head;
  while (*link) {
    outbound_packet_t *packet = *link;
    if (packet->video && same_stream(packet, video)) {
      *link = packet->next;
      out->bytes -= packet->cost;
      --out->count;
      ++out->stats.dropped;
      packet_free(out, packet);
    } else {
      link = &packet->next;
    }
  }
}

static void expire_video(acip_outbound_t *out, uint64_t now) {
  for (;;) {
    outbound_packet_t *expired = NULL;
    for (outbound_packet_t *p = out->head; p; p = p->next)
      if (p->video && now - p->created > ACIP_OUTBOUND_VIDEO_AGE_NS) {
        expired = p;
        break;
      }
    if (!expired)
      break;
    outbound_packet_t identity = *expired;
    discard_stream(out, &identity);
  }
}

// Keep the terminal wrapper plus its relevant nested cause. Follow causal IDs,
// not merely older registry entries, and never cross this send's checkpoint.
static asciichat_error_context_t backend_failure_detail(asciichat_errno_scope_t scope,
                                                        const asciichat_error_context_t *cause) {
  asciichat_error_context_t frames[ASCIICHAT_ERRNO_MAX_DEPTH];
  size_t count = asciichat_errno_snapshot(scope.thread_id, frames, ASCIICHAT_ERRNO_MAX_DEPTH);
  if (count > ASCIICHAT_ERRNO_MAX_DEPTH)
    count = ASCIICHAT_ERRNO_MAX_DEPTH;
  asciichat_error_context_t detail = {0};
  uint64_t next = cause->cause_id;
  for (size_t i = 0; i < count && next; ++i) {
    if (frames[i].generation != scope.generation || frames[i].error_id <= scope.after_id)
      continue;
    if (frames[i].error_id != next)
      continue;
    if (!detail.has_system_error && !detail.has_wsa_error)
      detail = frames[i];
    else if (frames[i].has_system_error || frames[i].has_wsa_error)
      detail = frames[i];
    next = frames[i].cause_id;
  }
  return detail;
}

static asciichat_error_t import_failure(const asciichat_error_context_t *cause,
                                        const asciichat_error_context_t *detail) {
  if (detail->code != ASCIICHAT_OK)
    asciichat_errno_import(detail);
  return asciichat_errno_import(cause);
}

static void fail_queue(acip_outbound_t *out, asciichat_error_t code, const asciichat_error_context_t *cause,
                       const asciichat_error_context_t *detail) {
  if (!out->failed) {
    out->failure_code = code;
    if (cause)
      out->failure = *cause;
    if (detail)
      out->failure_detail = *detail;
  }
  out->failed = true;
  while (out->head) {
    outbound_packet_t *p = out->head;
    out->head = p->next;
    out->bytes -= p->cost;
    --out->count;
    if (p->wait) {
      p->result = out->failure_code;
      p->done = true;
    } else {
      packet_free(out, p);
    }
  }
  cond_broadcast(&out->changed);
}

static void *outbound_worker(void *arg) {
  acip_outbound_t *out = arg;
  mutex_lock(&out->mutex);
  while (!out->stopping && !out->failed) {
    uint64_t now = time_get_ns();
    expire_video(out, now);
    outbound_packet_t *p = out->head;
    if (!p) {
      cond_wait(&out->changed, &out->mutex);
      continue;
    }
    /* Admitted reliable traffic expires by closing the connection, never by
     * silently removing an audio or control packet. */
    uint64_t deadline = p->audio ? 5 * NS_PER_SEC_INT : 10 * NS_PER_SEC_INT;
    if (!p->video && now - p->created > deadline) {
      asciichat_errno_scope_t scope = asciichat_errno_scope_begin();
      asciichat_error_context_t cause;
      SET_ERRNO(ERROR_NETWORK_TIMEOUT, "Outbound reliable packet expired before submission");
      bool has_cause = asciichat_errno_peek_since(scope, &cause);
      fail_queue(out, ERROR_NETWORK_TIMEOUT, has_cause ? &cause : NULL, NULL);
      asciichat_errno_scope_end(scope, ASCIICHAT_ERRNO_HANDLED);
      break;
    }
    size_t backend = backend_bytes(out);
    // Keep scheduling decisions in the plaintext queue. Even tiny reliable
    // packets must not accumulate behind video in a backend's opaque FIFO.
    if (backend != 0) {
      cond_timedwait(&out->changed, &out->mutex, 2 * NS_PER_MS_INT);
      continue;
    }
    out->head = p->next;
    out->bytes -= p->cost;
    // Admission reserves plaintext plus two wire owners. During handoff one
    // wire owner moves into the backend's counter, so do not count it twice.
    out->active_bytes = p->cost - (p->size + WIRE_OVERHEAD);
    mutex_unlock(&out->mutex);
    /* One worker fixes wire/encryption order. Backends receive only complete
     * packets and must finish a partially submitted packet or fail the stream. */
    asciichat_errno_scope_t scope = asciichat_errno_scope_begin();
    asciichat_error_t result = out->transport->methods->send(out->transport, p->data, p->size);
    asciichat_error_context_t cause = {0};
    asciichat_error_context_t detail = {0};
    if (result != ASCIICHAT_OK) {
      // Only this send's matching failure belongs to the submission. A backend
      // may return a status without context, or leave an unrelated nested error.
      if (!asciichat_errno_peek_since(scope, &cause) || cause.code != result) {
        SET_ERRNO(result, "Outbound backend submission failed");
        asciichat_errno_peek_since(scope, &cause);
      } else {
        detail = backend_failure_detail(scope, &cause);
      }
    }
    asciichat_errno_scope_end(scope, ASCIICHAT_ERRNO_HANDLED);
    mutex_lock(&out->mutex);
    out->active_bytes = 0;
    --out->count;
    if (result == ASCIICHAT_OK) {
      ++out->stats.submitted;
      if (p->encoded && p->keyframe)
        ++out->stats.recovery_frames;
    }
    if (p->wait) {
      p->result = result;
      p->done = true;
      cond_broadcast(&out->changed);
    } else {
      packet_free(out, p);
    }
    if (result != ASCIICHAT_OK) {
      fail_queue(out, result, &cause, &detail);
      break;
    }
  }
  bool failed = out->failed;
  mutex_unlock(&out->mutex);
  if (failed)
    out->transport->methods->close(out->transport);
  return NULL;
}

asciichat_error_t acip_outbound_create(acip_transport_t *transport) {
  if (!transport || !transport->methods || !transport->methods->send)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing outbound backend");
  if (transport->outbound)
    return ASCIICHAT_OK;
  acip_outbound_t *out = SAFE_CALLOC(1, sizeof(*out), acip_outbound_t *);
  if (mutex_init(&out->mutex, "connection_outgoing_packet_queue") != ASCIICHAT_OK) {
    SAFE_FREE(out);
    return GET_ERRNO();
  }
  if (cond_init(&out->changed, "outgoing_packet_ready_or_done") != ASCIICHAT_OK) {
    mutex_destroy(&out->mutex);
    SAFE_FREE(out);
    return GET_ERRNO();
  }
  out->transport = transport;
  asciichat_error_t result = asciichat_thread_create(&out->worker, "outbound", outbound_worker, out);
  if (result != ASCIICHAT_OK) {
    cond_destroy(&out->changed);
    mutex_destroy(&out->mutex);
    SAFE_FREE(out);
    return result;
  }
  transport->outbound = out;
  return ASCIICHAT_OK;
}

void acip_outbound_destroy(acip_transport_t *transport) {
  acip_outbound_t *out = transport->outbound;
  if (!out)
    return;
  mutex_lock(&out->mutex);
  out->stopping = true;
  cond_broadcast(&out->changed);
  mutex_unlock(&out->mutex);
  /* The caller has quiesced producers. Interrupt a blocking legacy TCP write
   * before joining, while every backend resource is still alive. */
  transport->methods->close(transport);
  asciichat_thread_join(&out->worker, NULL);
  mutex_lock(&out->mutex);
  fail_queue(out, ERROR_NETWORK, NULL, NULL);
  while (out->recycled) {
    outbound_packet_t *p = out->recycled;
    out->recycled = p->next;
    packet_free(out, p);
  }
  mutex_unlock(&out->mutex);
  cond_destroy(&out->changed);
  mutex_destroy(&out->mutex);
  SAFE_FREE(transport->outbound);
}

asciichat_error_t acip_outbound_submitv(acip_transport_t *transport, const socket_buffer_t *buffers, size_t count,
                                        acip_admission_t *admission) {
  if (!transport || !transport->outbound || !buffers || !count || count > SOCKET_IOV_MAX || !admission)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid outbound submission");
  *admission = ACIP_ADMISSION_CONGESTED;
  size_t size = 0;
  for (size_t i = 0; i < count; ++i) {
    if ((buffers[i].len && !buffers[i].data) || buffers[i].len > ACIP_OUTBOUND_BUDGET - WIRE_OVERHEAD - size)
      return SET_ERRNO(ERROR_NETWORK_SIZE, "Outbound packet exceeds byte budget");
    size += buffers[i].len;
  }
  packet_header_t header;
  if (buffers[0].len < sizeof(header))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing outbound ACIP header");
  memcpy(&header, buffers[0].data, sizeof(header));
  outbound_packet_t info = {.size = size,
                            .cost = 3 * (size + WIRE_OVERHEAD) + sizeof(outbound_packet_t) + 128,
                            .type = NET_TO_HOST_U16(header.type),
                            .stream = NET_TO_HOST_U32(header.client_id)};
  info.encoded = info.type == PACKET_TYPE_IMAGE_FRAME_H264 || info.type == PACKET_TYPE_IMAGE_FRAME_H265;
  info.video = info.encoded || info.type == PACKET_TYPE_IMAGE_FRAME || info.type == PACKET_TYPE_ASCII_FRAME;
  info.audio = info.type == PACKET_TYPE_AUDIO_BATCH || info.type == PACKET_TYPE_AUDIO_OPUS_BATCH;
  info.wait = !info.video && !info.audio && info.type != PACKET_TYPE_PING && info.type != PACKET_TYPE_PONG;
  if (info.encoded) {
    const uint8_t *flags = NULL;
    if (buffers[0].len > sizeof(header))
      flags = (const uint8_t *)buffers[0].data + sizeof(header);
    else if (count > 1 && buffers[1].len)
      flags = buffers[1].data;
    if (!flags)
      return SET_ERRNO(ERROR_INVALID_FRAME, "Missing encoded frame flags");
    info.keyframe = (*flags & 1) != 0;
  }
  acip_outbound_t *out = transport->outbound;
  mutex_lock(&out->mutex);
  info.created = time_get_ns();
  if (out->stopping || out->failed) {
    asciichat_error_t code = out->failed ? out->failure_code : ERROR_NETWORK;
    asciichat_error_context_t cause = out->failure;
    asciichat_error_context_t detail = out->failure_detail;
    mutex_unlock(&out->mutex);
    return cause.code == code ? import_failure(&cause, &detail) : SET_ERRNO(code, "Outbound connection closed");
  }
  expire_video(out, info.created);
  outbound_stream_t *stream = info.encoded ? stream_state(out, info.type, info.stream) : NULL;
  if (info.encoded && (!stream || (stream->needs_keyframe && !info.keyframe))) {
    ++out->stats.dropped;
    *admission = ACIP_ADMISSION_NEEDS_KEYFRAME;
    mutex_unlock(&out->mutex);
    return ASCIICHAT_OK;
  }
  if (info.video && !info.encoded)
    discard_stream(out, &info);
  size_t backend = backend_bytes(out);
  size_t reserved = info.video   ? ACIP_OUTBOUND_CONTROL_RESERVE + ACIP_OUTBOUND_AUDIO_RESERVE
                    : info.audio ? ACIP_OUTBOUND_CONTROL_RESERVE
                                 : 0;
  size_t limit = ACIP_OUTBOUND_BUDGET - reserved;
  size_t pending = out->bytes + out->active_bytes + backend + transport->methods->reserved_bytes;
  if (out->count >= OUTBOUND_MAX_PACKETS - (info.video   ? 64
                                            : info.audio ? 32
                                                         : 0) ||
      pending > limit || info.cost > limit - pending) {
    if (info.encoded)
      discard_stream(out, &info);
    ++out->stats.congestion;
    mutex_unlock(&out->mutex);
    return ASCIICHAT_OK;
  }
  outbound_packet_t **cached = &out->recycled;
  while (*cached && ((*cached)->wait != info.wait || buffer_capacity((*cached)->data) < size))
    cached = &(*cached)->next;
  outbound_packet_t *p = *cached;
  uint8_t *storage = NULL;
  if (p) {
    *cached = p->next;
    --out->recycled_count;
    storage = p->data;
  } else {
    p = SAFE_MALLOC(sizeof(*p), outbound_packet_t *);
  }
  *p = info;
  p->data = storage ? storage : buffer_alloc(info.wait ? buffer_control_domain() : NULL, size);
  if (!p->data) {
    SAFE_FREE(p);
    mutex_unlock(&out->mutex);
    return SET_ERRNO(ERROR_MEMORY, "Cannot own outbound plaintext");
  }
  size_t offset = 0;
  for (size_t i = 0; i < count; ++i) {
    if (buffers[i].len)
      memcpy(p->data + offset, buffers[i].data, buffers[i].len);
    offset += buffers[i].len;
  }
  /* Audio and ping/pong may bypass whole unsent video. Other controls remain
   * FIFO barriers (stream setup/teardown and negotiation cannot be reordered). */
  bool priority = info.audio || info.type == PACKET_TYPE_PING || info.type == PACKET_TYPE_PONG;
  outbound_packet_t **link = &out->head;
  if (priority) {
    // Do not move audio or liveness traffic across an earlier protocol barrier.
    for (outbound_packet_t **scan = &out->head; *scan; scan = &(*scan)->next)
      if (!(*scan)->video && !(*scan)->audio && (*scan)->type != PACKET_TYPE_PING && (*scan)->type != PACKET_TYPE_PONG)
        link = &(*scan)->next;
  }
  while (*link && (!priority || !(*link)->video))
    link = &(*link)->next;
  bool was_empty = out->head == NULL;
  p->next = *link;
  *link = p;
  out->bytes += info.cost;
  ++out->count;
  ++out->stats.accepted;
  pending += info.cost;
  if (pending > out->stats.peak_bytes)
    out->stats.peak_bytes = pending;
  if (stream && info.keyframe)
    stream->needs_keyframe = false;
  *admission = ACIP_ADMISSION_ACCEPTED;
  if (was_empty)
    cond_broadcast(&out->changed);
  asciichat_error_t result = ASCIICHAT_OK;
  asciichat_error_context_t cause = {0};
  asciichat_error_context_t detail = {0};
  if (info.wait) {
    while (!p->done)
      cond_wait(&out->changed, &out->mutex);
    result = p->result;
    if (result != ASCIICHAT_OK) {
      cause = out->failure;
      detail = out->failure_detail;
    }
    packet_free(out, p);
  }
  mutex_unlock(&out->mutex);
  if (result == ASCIICHAT_OK)
    return result;
  return cause.code == result ? import_failure(&cause, &detail) : SET_ERRNO(result, "Outbound submission failed");
}

asciichat_error_t acip_outbound_snapshot(acip_transport_t *transport, acip_outbound_stats_t *stats) {
  if (!transport || !transport->outbound || !stats)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing outbound statistics");
  acip_outbound_t *out = transport->outbound;
  mutex_lock(&out->mutex);
  *stats = out->stats;
  stats->application_bytes = out->bytes + out->active_bytes;
  stats->backend_bytes = backend_bytes(out);
  stats->backend_reserved_bytes = transport->methods->reserved_bytes;
  uint64_t now = time_get_ns();
  for (outbound_packet_t *p = out->head; p; p = p->next)
    if (p->video && now - p->created > stats->oldest_frame_age_ns)
      stats->oldest_frame_age_ns = now - p->created;
  mutex_unlock(&out->mutex);
  return ASCIICHAT_OK;
}

bool acip_outbound_needs_keyframe(acip_transport_t *transport, uint16_t type, uint32_t id) {
  if (!transport || !transport->outbound)
    return false;
  acip_outbound_t *out = transport->outbound;
  size_t count = out->stream_count;
  for (size_t i = 0; i < count; ++i)
    if (out->streams[i].type == type && out->streams[i].stream == id)
      return out->streams[i].needs_keyframe;
  return true;
}
