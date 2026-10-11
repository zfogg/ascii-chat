#include <criterion/criterion.h>
#include <ascii-chat/network/packet/queue.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>

TestSuite(packet_queue);

Test(packet_queue, copy_is_independent) {
  packet_queue_t *queue = packet_queue_create(4);
  cr_assert_not_null(queue);
  char input[] = "packet";
  cr_assert_eq(packet_queue_push_copy(queue, PACKET_TYPE_PING, input, sizeof(input), 0), ASCIICHAT_OK);
  input[0] = 'x';
  queued_packet_t *packet = packet_queue_dequeue(queue);
  cr_assert_not_null(packet);
  cr_assert_str_eq(packet->data, "packet");
  packet_queue_destroy(queue);
  cr_assert(packet_queue_validate_packet(packet));
  packet_queue_free_packet(packet);
}

Test(packet_queue, failed_take_preserves_owner) {
  packet_queue_t *queue = packet_queue_create(1);
  cr_assert_not_null(queue);
  void *first = buffer_alloc(NULL, 64);
  void *second = buffer_alloc(NULL, 64);
  cr_assert_not_null(first);
  cr_assert_not_null(second);
  memset(first, 1, 64);
  memset(second, 2, 64);
  cr_assert_eq(packet_queue_push_take(queue, PACKET_TYPE_PING, &first, 64, 0), ASCIICHAT_OK);
  cr_assert_null(first);
  cr_assert_neq(packet_queue_push_take(queue, PACKET_TYPE_PING, &second, 64, 0), ASCIICHAT_OK);
  cr_assert_not_null(second);
  buffer_release(second);
  packet_queue_destroy(queue);
}

Test(packet_queue, byte_limit_and_drop_policy) {
  packet_queue_config_t config = {100, 8, PACKET_QUEUE_DROP_OLDEST};
  packet_queue_t *queue = packet_queue_create_config(&config);
  cr_assert_not_null(queue);
  cr_assert_eq(packet_queue_push_copy(queue, PACKET_TYPE_IMAGE_FRAME, "first", 6, 0), ASCIICHAT_OK);
  cr_assert_eq(packet_queue_push_copy(queue, PACKET_TYPE_IMAGE_FRAME, "second", 7, 0), ASCIICHAT_OK);
  cr_assert_eq(packet_queue_size(queue), 1);
  queued_packet_t *packet = packet_queue_dequeue(queue);
  cr_assert_str_eq(packet->data, "second");
  packet_queue_free_packet(packet);
  cr_assert_neq(packet_queue_push_copy(queue, PACKET_TYPE_PING, "oversized", 10, 0), ASCIICHAT_OK);
  packet_queue_destroy(queue);
}

Test(packet_queue, stopped_queue_drains_on_destroy) {
  packet_queue_t *queue = packet_queue_create(1);
  cr_assert_not_null(queue);
  cr_assert_eq(packet_queue_push_copy(queue, PACKET_TYPE_PING, "test", 5, 0), ASCIICHAT_OK);
  packet_queue_stop(queue);
  cr_assert_null(packet_queue_dequeue(queue));
  cr_assert_neq(packet_queue_push_copy(queue, PACKET_TYPE_PING, NULL, 0, 0), ASCIICHAT_OK);
  packet_queue_clear(queue);
  cr_assert(packet_queue_is_empty(queue));
  packet_queue_destroy(queue);
}

Test(packet_queue, malformed_inputs) {
  packet_queue_t *queue = packet_queue_create(1);
  cr_assert_neq(packet_queue_push_copy(queue, PACKET_TYPE_PING, NULL, 5, 0), ASCIICHAT_OK);
  cr_assert_neq(packet_queue_push_copy(queue, 0, "test", 5, 0), ASCIICHAT_OK);
  cr_assert_eq(packet_queue_push_copy(queue, PACKET_TYPE_PING, NULL, 0, 0), ASCIICHAT_OK);
  queued_packet_t *packet = packet_queue_dequeue(queue);
  cr_assert_not_null(packet);
  cr_assert_eq(packet->data_len, 0);
  packet_queue_free_packet(packet);
  packet_queue_destroy(queue);
}
