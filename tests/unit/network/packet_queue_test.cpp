#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/network/packet/queue.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>
TEST_CASE("packet_queue/copy_is_independent", "[packet_queue]") {
  packet_queue_t *queue = packet_queue_create(4);
  REQUIRE((queue) != nullptr);
  char input[] = "packet";
  REQUIRE((packet_queue_push_copy(queue, PACKET_TYPE_PING, input, sizeof(input), 0)) == (ASCIICHAT_OK));
  input[0] = 'x';
  queued_packet_t *packet = packet_queue_dequeue(queue);
  REQUIRE((packet) != nullptr);
  REQUIRE(std::strcmp((static_cast<const char *>(packet->data)), ("packet")) == 0);
  packet_queue_destroy(queue);
  REQUIRE(packet_queue_validate_packet(packet));
  packet_queue_free_packet(packet);
}

TEST_CASE("packet_queue/failed_take_preserves_owner", "[packet_queue]") {
  packet_queue_t *queue = packet_queue_create(1);
  REQUIRE((queue) != nullptr);
  void *first = buffer_alloc(NULL, 64);
  void *second = buffer_alloc(NULL, 64);
  REQUIRE((first) != nullptr);
  REQUIRE((second) != nullptr);
  memset(first, 1, 64);
  memset(second, 2, 64);
  REQUIRE((packet_queue_push_take(queue, PACKET_TYPE_PING, &first, 64, 0)) == (ASCIICHAT_OK));
  REQUIRE((first) == nullptr);
  REQUIRE((packet_queue_push_take(queue, PACKET_TYPE_PING, &second, 64, 0)) != (ASCIICHAT_OK));
  REQUIRE((second) != nullptr);
  buffer_release(second);
  packet_queue_destroy(queue);
}

TEST_CASE("packet_queue/byte_limit_and_drop_policy", "[packet_queue]") {
  packet_queue_config_t config = {100, 8, PACKET_QUEUE_DROP_OLDEST};
  packet_queue_t *queue = packet_queue_create_config(&config);
  REQUIRE((queue) != nullptr);
  REQUIRE((packet_queue_push_copy(queue, PACKET_TYPE_IMAGE_FRAME, "first", 6, 0)) == (ASCIICHAT_OK));
  REQUIRE((packet_queue_push_copy(queue, PACKET_TYPE_IMAGE_FRAME, "second", 7, 0)) == (ASCIICHAT_OK));
  REQUIRE((packet_queue_size(queue)) == (1));
  queued_packet_t *packet = packet_queue_dequeue(queue);
  REQUIRE(std::strcmp((static_cast<const char *>(packet->data)), ("second")) == 0);
  packet_queue_free_packet(packet);
  REQUIRE((packet_queue_push_copy(queue, PACKET_TYPE_PING, "oversized", 10, 0)) != (ASCIICHAT_OK));
  packet_queue_destroy(queue);
}

TEST_CASE("packet_queue/stopped_queue_drains_on_destroy", "[packet_queue]") {
  packet_queue_t *queue = packet_queue_create(1);
  REQUIRE((queue) != nullptr);
  REQUIRE((packet_queue_push_copy(queue, PACKET_TYPE_PING, "test", 5, 0)) == (ASCIICHAT_OK));
  packet_queue_stop(queue);
  REQUIRE((packet_queue_dequeue(queue)) == nullptr);
  REQUIRE((packet_queue_push_copy(queue, PACKET_TYPE_PING, NULL, 0, 0)) != (ASCIICHAT_OK));
  packet_queue_clear(queue);
  REQUIRE(packet_queue_is_empty(queue));
  packet_queue_destroy(queue);
}

TEST_CASE("packet_queue/malformed_inputs", "[packet_queue]") {
  packet_queue_t *queue = packet_queue_create(1);
  REQUIRE((packet_queue_push_copy(queue, PACKET_TYPE_PING, NULL, 5, 0)) != (ASCIICHAT_OK));
  REQUIRE((packet_queue_push_copy(queue, static_cast<packet_type_t>(0), "test", 5, 0)) != (ASCIICHAT_OK));
  REQUIRE((packet_queue_push_copy(queue, PACKET_TYPE_PING, NULL, 0, 0)) == (ASCIICHAT_OK));
  queued_packet_t *packet = packet_queue_dequeue(queue);
  REQUIRE((packet) != nullptr);
  REQUIRE((packet->data_len) == (0));
  packet_queue_free_packet(packet);
  packet_queue_destroy(queue);
}
