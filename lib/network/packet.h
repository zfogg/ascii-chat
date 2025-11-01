#pragma once

/**
 * @file network/packet.h
 * @brief Packet protocol implementation
 *
 * This header provides packet verification, CRC validation, and protocol
 * compliance checking for all network packets.
 */

#include "platform/socket.h"
#include "packet_types.h"
#include "crypto/crypto.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Packet envelope containing received packet data
 */
typedef struct {
  packet_type_t type;
  void *data;
  size_t len;
  bool was_encrypted;
  void *allocated_buffer; // Buffer that needs to be freed
  size_t allocated_size;  // Size of allocated buffer
} packet_envelope_t;

/**
 * @brief Packet reception result codes
 */
typedef enum {
  PACKET_RECV_SUCCESS = 0,            // Packet received successfully
  PACKET_RECV_EOF = -1,               // Connection closed
  PACKET_RECV_ERROR = -2,             // Network error
  PACKET_RECV_SECURITY_VIOLATION = -3 // Encryption policy violated
} packet_recv_result_t;

/**
 * @brief Validate packet header and return parsed information
 * @param header Packet header to validate
 * @param pkt_type Output: packet type
 * @param pkt_len Output: packet length
 * @param expected_crc Output: expected CRC32
 * @return 0 on success, -1 on error
 */
asciichat_error_t packet_validate_header(const packet_header_t *header, uint16_t *pkt_type, uint32_t *pkt_len,
                                         uint32_t *expected_crc);

/**
 * @brief Validate packet CRC32
 * @param data Packet data
 * @param len Data length
 * @param expected_crc Expected CRC32 value
 * @return 0 on success, -1 on error
 */
asciichat_error_t packet_validate_crc32(const void *data, size_t len, uint32_t expected_crc);

/**
 * @brief Send a packet with proper header and CRC32
 * @param sockfd Socket file descriptor
 * @param type Packet type
 * @param data Packet data
 * @param len Data length
 * @return 0 on success, -1 on error
 */
asciichat_error_t packet_send(socket_t sockfd, packet_type_t type, const void *data, size_t len);

/**
 * @brief Receive a packet with proper header validation and CRC32 checking
 * @param sockfd Socket file descriptor
 * @param type Output: packet type
 * @param data Output: packet data (allocated by caller, freed by caller)
 * @param len Output: data length
 * @return 0 on success, -1 on error
 */
asciichat_error_t packet_receive(socket_t sockfd, packet_type_t *type, void **data, size_t *len);

/**
 * @brief Send a packet with encryption and compression support
 * @param sockfd Socket file descriptor
 * @param type Packet type
 * @param data Packet data
 * @param len Data length
 * @param crypto_ctx Crypto context for encryption
 * @return 0 on success, -1 on error
 */
int send_packet_secure(socket_t sockfd, packet_type_t type, const void *data, size_t len, crypto_context_t *crypto_ctx);

/**
 * @brief Receive a packet with decryption and decompression support
 * @param sockfd Socket file descriptor
 * @param crypto_ctx Crypto context for decryption
 * @param enforce_encryption Whether to require encryption
 * @param envelope Output: received packet envelope
 * @return Packet receive result
 */
packet_recv_result_t receive_packet_secure(socket_t sockfd, void *crypto_ctx, bool enforce_encryption,
                                           packet_envelope_t *envelope);

/**
 * @brief Send a basic packet without encryption
 * @param sockfd Socket file descriptor
 * @param type Packet type
 * @param data Packet data
 * @param len Data length
 * @return 0 on success, -1 on error
 */
int send_packet(socket_t sockfd, packet_type_t type, const void *data, size_t len);

/**
 * @brief Receive a basic packet without encryption
 * @param sockfd Socket file descriptor
 * @param type Output: packet type
 * @param data Output: packet data
 * @param len Output: data length
 * @return 0 on success, -1 on error
 */
int receive_packet(socket_t sockfd, packet_type_t *type, void **data, size_t *len);

/**
 * @brief Send a ping packet
 * @param sockfd Socket file descriptor
 * @return 0 on success, -1 on error
 */
int send_ping_packet(socket_t sockfd);

/**
 * @brief Send a pong packet
 * @param sockfd Socket file descriptor
 * @return 0 on success, -1 on error
 */
int send_pong_packet(socket_t sockfd);

/**
 * @brief Send a clear console packet
 * @param sockfd Socket file descriptor
 * @return 0 on success, -1 on error
 */
int send_clear_console_packet(socket_t sockfd);

/**
 * @brief Send protocol version packet
 * @param sockfd Socket file descriptor
 * @param version Protocol version packet
 * @return 0 on success, -1 on error
 */
int send_protocol_version_packet(socket_t sockfd, const protocol_version_packet_t *version);

/**
 * @brief Send crypto capabilities packet
 * @param sockfd Socket file descriptor
 * @param caps Crypto capabilities packet
 * @return 0 on success, -1 on error
 */
int send_crypto_capabilities_packet(socket_t sockfd, const crypto_capabilities_packet_t *caps);

/**
 * @brief Send crypto parameters packet
 * @param sockfd Socket file descriptor
 * @param params Crypto parameters packet
 * @return 0 on success, -1 on error
 */
int send_crypto_parameters_packet(socket_t sockfd, const crypto_parameters_packet_t *params);
