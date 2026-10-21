#pragma once
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/platform/socket.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct acip_transport acip_transport_t;
typedef struct acip_outbound acip_outbound_t;

/** All limits include ACIP/encryption overhead. Backend bytes exclude OS buffers. */
#define ACIP_OUTBOUND_BUDGET (8U * 1024U * 1024U)
#define ACIP_OUTBOUND_CONTROL_RESERVE (512U * 1024U)
#define ACIP_OUTBOUND_AUDIO_RESERVE (512U * 1024U)
#define ACIP_OUTBOUND_SCTP_RESERVE (64U * 1024U)
#define ACIP_OUTBOUND_VIDEO_AGE_NS (100ULL * 1000000ULL)

typedef enum {
  ACIP_ADMISSION_ACCEPTED,
  ACIP_ADMISSION_CONGESTED,
  ACIP_ADMISSION_DROPPED_STALE,
  ACIP_ADMISSION_NEEDS_KEYFRAME
} acip_admission_t;

typedef struct {
  size_t application_bytes, backend_bytes, backend_reserved_bytes, peak_bytes;
  uint64_t accepted, submitted, dropped, congestion, recovery_frames;
  uint64_t oldest_frame_age_ns;
} acip_outbound_stats_t;

asciichat_error_t acip_outbound_create(acip_transport_t *transport);
/** Stop admission, interrupt writes, join the worker, and release every queued owner. */
void acip_outbound_destroy(acip_transport_t *transport);
/** Copy plaintext before returning ACCEPTED; congestion never changes encryption state.
 * Handshake/control submissions except ping/pong wait for backend submission so crypto/setup barriers hold.
 * Success describes local admission/submission, never receiver delivery.
 * A terminal backend failure retains its status and errno context for waiting
 * and subsequent producers. Congestion remains a nonfatal admission outcome.
 */
asciichat_error_t acip_outbound_submitv(acip_transport_t *transport, const socket_buffer_t *buffers, size_t count,
                                        acip_admission_t *admission);
asciichat_error_t acip_outbound_snapshot(acip_transport_t *transport, acip_outbound_stats_t *stats);
bool acip_outbound_needs_keyframe(acip_transport_t *transport, uint16_t packet_type, uint32_t stream_id);
