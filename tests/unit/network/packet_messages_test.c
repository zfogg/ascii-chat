#include <criterion/criterion.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/acip/messages.h>
#include <ascii-chat/util/endian.h>
#include <string.h>

Test(packet_messages, error_packets_use_declared_length_and_terminate_every_output_capacity) {
  struct {
    error_packet_t header;
    char message[6];
  } packet = {
      .header = {.error_code = HOST_TO_NET_U32(ERROR_CRYPTO_VERIFICATION), .message_length = HOST_TO_NET_U32(5)},
      .message = {'h', 'e', 'l', 'l', 'o', '!'}};
  for (size_t capacity = 1; capacity <= 8; ++capacity) {
    char output[10];
    memset(output, 0xa5, sizeof(output));
    size_t length = SIZE_MAX;
    asciichat_error_t code = ASCIICHAT_OK;
    cr_assert_eq(packet_parse_error_message(&packet, sizeof(packet), &code, output, capacity, &length), ASCIICHAT_OK);
    cr_assert_eq(code, ERROR_CRYPTO_VERIFICATION);
    cr_assert_eq(length, 5);
    size_t copied = capacity > 5 ? 5 : capacity - 1;
    cr_assert_arr_eq(output, "hello", copied);
    cr_assert_eq(output[copied], '\0');
    cr_assert_eq((unsigned char)output[capacity], 0xa5);
  }
  char output[8];
  asciichat_error_t code;
  packet.header.message_length = 0;
  cr_assert_eq(packet_parse_error_message(&packet, sizeof(packet.header), &code, output, sizeof(output), NULL),
               ASCIICHAT_OK);
  cr_assert_str_empty(output);
}

Test(packet_messages, error_packet_rejections_preserve_message_storage) {
  struct {
    error_packet_t header;
    char message[4];
  } packet = {.header = {.error_code = HOST_TO_NET_U32(ERROR_GENERAL), .message_length = HOST_TO_NET_U32(4)},
              .message = {'t', 'e', 's', 't'}};
  asciichat_error_t code = ERROR_GENERAL;
  char output[8];
  memset(output, 0xa5, sizeof(output));
  size_t length = SIZE_MAX;
  cr_assert_eq(packet_parse_error_message(NULL, sizeof(packet), &code, output, sizeof(output), &length),
               ERROR_INVALID_PARAM);
  cr_assert_eq(packet_parse_error_message(&packet, sizeof(packet), NULL, output, sizeof(output), &length),
               ERROR_INVALID_PARAM);
  cr_assert_eq(packet_parse_error_message(&packet, sizeof(packet), &code, NULL, sizeof(output), &length),
               ERROR_INVALID_PARAM);
  cr_assert_eq(packet_parse_error_message(&packet, sizeof(packet), &code, output, 0, &length), ERROR_INVALID_PARAM);
  for (size_t size = 0; size < sizeof(packet.header); ++size)
    cr_assert_eq(packet_parse_error_message(&packet, size, &code, output, sizeof(output), &length),
                 ERROR_INVALID_PARAM);
  for (size_t size = sizeof(packet.header); size < sizeof(packet); ++size)
    cr_assert_eq(packet_parse_error_message(&packet, size, &code, output, sizeof(output), &length),
                 ERROR_NETWORK_PROTOCOL);
  packet.header.message_length = HOST_TO_NET_U32(MAX_ERROR_MESSAGE_LENGTH + 1);
  cr_assert_eq(packet_parse_error_message(&packet, sizeof(packet), &code, output, sizeof(output), &length),
               ERROR_NETWORK_PROTOCOL);
  for (size_t i = 0; i < sizeof(output); ++i)
    cr_assert_eq((unsigned char)output[i], 0xa5);
  cr_assert_eq(length, SIZE_MAX);
  cr_assert_eq(code, ERROR_GENERAL);
}

Test(packet_messages, remote_log_levels_directions_flags_and_truncation) {
  struct {
    remote_log_packet_t header;
    char message[7];
  } packet = {.header = {.flags = HOST_TO_NET_U16(0xa502), .message_length = HOST_TO_NET_U32(6)},
              .message = {'r', 'e', 'm', 'o', 't', 'e', '!'}};
  const uint8_t directions[] = {REMOTE_LOG_DIRECTION_UNKNOWN, REMOTE_LOG_DIRECTION_SERVER_TO_CLIENT,
                                REMOTE_LOG_DIRECTION_CLIENT_TO_SERVER, 0xff};
  for (unsigned level = LOG_DEV; level <= LOG_FATAL; ++level) {
    packet.header.log_level = (uint8_t)level;
    for (size_t d = 0; d < sizeof(directions); ++d) {
      packet.header.direction = directions[d];
      for (size_t capacity = 1; capacity <= 9; ++capacity) {
        char output[10];
        memset(output, 0xa5, sizeof(output));
        log_level_t parsed_level;
        remote_log_direction_t direction;
        uint16_t flags;
        size_t length = SIZE_MAX;
        cr_assert_eq(packet_parse_remote_log(&packet, sizeof(packet), &parsed_level, &direction, &flags, output,
                                             capacity, &length),
                     ASCIICHAT_OK);
        cr_assert_eq(parsed_level, level);
        cr_assert_eq(direction, directions[d] == 0xff ? REMOTE_LOG_DIRECTION_UNKNOWN : directions[d]);
        cr_assert_eq(flags, 0xa502);
        cr_assert_eq(length, 6);
        size_t copied = capacity > 6 ? 6 : capacity - 1;
        cr_assert_arr_eq(output, "remote", copied);
        cr_assert_eq(output[copied], '\0');
        cr_assert_eq((unsigned char)output[capacity], 0xa5);
      }
    }
  }
  packet.header.message_length = 0;
  char output[8];
  log_level_t level;
  remote_log_direction_t direction;
  uint16_t flags;
  cr_assert_eq(
      packet_parse_remote_log(&packet, sizeof(packet.header), &level, &direction, &flags, output, sizeof(output), NULL),
      ASCIICHAT_OK);
  cr_assert_str_empty(output);
}

Test(packet_messages, malformed_remote_log_packets_are_rejected_without_writing_message) {
  remote_log_packet_t packet = {.log_level = LOG_INFO, .message_length = HOST_TO_NET_U32(1)};
  char output[8];
  memset(output, 0xa5, sizeof(output));
  log_level_t level;
  remote_log_direction_t direction;
  uint16_t flags;
  size_t length = SIZE_MAX;
  cr_assert_eq(
      packet_parse_remote_log(NULL, sizeof(packet), &level, &direction, &flags, output, sizeof(output), &length),
      ERROR_INVALID_PARAM);
  cr_assert_eq(
      packet_parse_remote_log(&packet, sizeof(packet), NULL, &direction, &flags, output, sizeof(output), &length),
      ERROR_INVALID_PARAM);
  cr_assert_eq(packet_parse_remote_log(&packet, sizeof(packet), &level, NULL, &flags, output, sizeof(output), &length),
               ERROR_INVALID_PARAM);
  cr_assert_eq(
      packet_parse_remote_log(&packet, sizeof(packet), &level, &direction, NULL, output, sizeof(output), &length),
      ERROR_INVALID_PARAM);
  cr_assert_eq(
      packet_parse_remote_log(&packet, sizeof(packet), &level, &direction, &flags, NULL, sizeof(output), &length),
      ERROR_INVALID_PARAM);
  cr_assert_eq(packet_parse_remote_log(&packet, sizeof(packet), &level, &direction, &flags, output, 0, &length),
               ERROR_INVALID_PARAM);
  for (size_t size = 0; size < sizeof(packet); ++size)
    cr_assert_eq(packet_parse_remote_log(&packet, size, &level, &direction, &flags, output, sizeof(output), &length),
                 ERROR_INVALID_PARAM);
  cr_assert_eq(
      packet_parse_remote_log(&packet, sizeof(packet), &level, &direction, &flags, output, sizeof(output), &length),
      ERROR_NETWORK_PROTOCOL);
  packet.message_length = HOST_TO_NET_U32(MAX_REMOTE_LOG_MESSAGE_LENGTH + 1);
  cr_assert_eq(
      packet_parse_remote_log(&packet, sizeof(packet), &level, &direction, &flags, output, sizeof(output), &length),
      ERROR_NETWORK_PROTOCOL);
  packet.message_length = 0;
  packet.log_level = 0xff;
  cr_assert_eq(
      packet_parse_remote_log(&packet, sizeof(packet), &level, &direction, &flags, output, sizeof(output), &length),
      ERROR_NETWORK_PROTOCOL);
  cr_assert_eq(length, SIZE_MAX);
  for (size_t i = 0; i < sizeof(output); ++i)
    cr_assert_eq((unsigned char)output[i], 0xa5);
}
