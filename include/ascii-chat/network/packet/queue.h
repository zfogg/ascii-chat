#pragma once
#include <ascii-chat/buffer.h>
#include "packet.h"

/** A dequeued packet owns its immutable payload until packet_queue_free_packet(). */
typedef struct {
  packet_header_t header;
  const void *data;
  size_t data_len;
} queued_packet_t;

typedef struct packet_queue packet_queue_t;
typedef enum { PACKET_QUEUE_REJECT, PACKET_QUEUE_DROP_OLDEST } packet_queue_overflow_t;

typedef struct {
  size_t max_packets;
  size_t max_bytes;
  packet_queue_overflow_t overflow;
} packet_queue_config_t;

/** Defaults: 16 MiB byte limit and rejection on overflow. Zero packet count
 * disables only the item limit, never the byte limit or memory budget. */
packet_queue_t *packet_queue_create(size_t max_packets);
packet_queue_t *packet_queue_create_config(const packet_queue_config_t *config);
/** Join queue users before destruction. Dequeued packets may outlive the queue. */
void packet_queue_destroy(packet_queue_t *queue);
void packet_queue_stop(packet_queue_t *queue);
void packet_queue_clear(packet_queue_t *queue);

/** Copy a borrowed view into queue-owned storage. */
asciichat_error_t packet_queue_push_copy(packet_queue_t *queue, packet_type_t type, const void *data, size_t length,
                                         uint32_t client_id);
/** On success, transfers the owning buffer pointer and clears *data.
 * On any error, the caller retains ownership and *data is unchanged. */
asciichat_error_t packet_queue_push_take(packet_queue_t *queue, packet_type_t type, void **data, size_t length,
                                         uint32_t client_id);
/** Queue a shared immutable buffer, retaining an independent reference. */
asciichat_error_t packet_queue_push_shared(packet_queue_t *queue, packet_type_t type, const void *data, size_t length,
                                           uint32_t client_id);
queued_packet_t *packet_queue_dequeue(packet_queue_t *queue);
queued_packet_t *packet_queue_try_dequeue(packet_queue_t *queue);
void packet_queue_free_packet(queued_packet_t *packet);
size_t packet_queue_size(packet_queue_t *queue);
bool packet_queue_is_empty(packet_queue_t *queue);
bool packet_queue_is_full(packet_queue_t *queue);
void packet_queue_get_stats(packet_queue_t *queue, uint64_t *enqueued, uint64_t *dequeued, uint64_t *dropped);
bool packet_queue_validate_packet(const queued_packet_t *packet);
