/**
 * @file network/packet.c
 * @ingroup network
 * @brief 📦 Packet protocol handler with CRC validation, encryption, and compression
 */

#include "packet.h"
#include "network.h"
#include "common.h"
#include "asciichat_errno.h"
#include "platform/socket.h"
#include "buffer_pool.h"
#include "crc32.h"
#include "crypto/crypto.h"
#include "compression.h"
#include <stdint.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Network timeout constants are defined in packet_types.h

/* ============================================================================
 * Packet Protocol Implementation
 * ============================================================================
 * This module handles packet verification, CRC validation, and protocol
 * compliance checking for all network packets.
 */

// Check if we're in a test environment
static int is_test_environment(void) {
  return SAFE_GETENV("CRITERION_TEST") != NULL || SAFE_GETENV("TESTING") != NULL;
}

/**
 * Calculate timeout based on packet size
 * Large packets need more time to transmit reliably
 */
static int calculate_packet_timeout(size_t packet_size) {
  int base_timeout = is_test_environment() ? 1 : SEND_TIMEOUT;

  // For large packets, increase timeout proportionally
  if (packet_size > LARGE_PACKET_THRESHOLD) {
    // Add extra timeout per MB above the threshold
    int extra_timeout =
        (int)(((double)packet_size - LARGE_PACKET_THRESHOLD) / 1000000.0 * LARGE_PACKET_EXTRA_TIMEOUT_PER_MB) + 1;
    int total_timeout = base_timeout + extra_timeout;

    // Ensure client timeout is longer than server's RECV_TIMEOUT (30s) to prevent deadlock
    // Add 10 seconds buffer to account for server processing delays
    int min_timeout = MIN_CLIENT_TIMEOUT;
    if (total_timeout < min_timeout) {
      total_timeout = min_timeout;
    }

    // Cap at maximum timeout
    return (total_timeout > MAX_CLIENT_TIMEOUT) ? MAX_CLIENT_TIMEOUT : total_timeout;
  }

  return base_timeout;
}

/**
 * @brief Validate packet header and return parsed information
 * @param header Packet header to validate
 * @param pkt_type Output: packet type
 * @param pkt_len Output: packet length
 * @param expected_crc Output: expected CRC32
 * @return 0 on success, -1 on error
 */
asciichat_error_t packet_validate_header(const packet_header_t *header, uint16_t *pkt_type, uint32_t *pkt_len,
                                         uint32_t *expected_crc) {
  if (!header || !pkt_type || !pkt_len || !expected_crc) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: header=%p, pkt_type=%p, pkt_len=%p, expected_crc=%p",
                     header, pkt_type, pkt_len, expected_crc);
  }

  // First validate packet length BEFORE converting from network byte order
  // This prevents potential integer overflow issues
  uint32_t pkt_len_network = header->length;
  if (pkt_len_network == 0xFFFFFFFF) {
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid packet length in network byte order: 0xFFFFFFFF");
  }

  // Convert from network byte order
  uint32_t magic = ntohl(header->magic);
  uint16_t type = ntohs(header->type);
  uint32_t len = ntohl(pkt_len_network);
  uint32_t crc = ntohl(header->crc32);

  // Validate magic
  if (magic != PACKET_MAGIC) {
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid packet magic: 0x%x (expected 0x%x)", magic, PACKET_MAGIC);
  }

  // Validate packet size with bounds checking
  if (len > MAX_PACKET_SIZE) {
    return SET_ERRNO(ERROR_NETWORK_SIZE, "Packet too large: %u > %d", len, MAX_PACKET_SIZE);
  }

  // Validate packet type and size constraints
  switch (type) {
  case PACKET_TYPE_PROTOCOL_VERSION:
    // Protocol version packet has fixed size
    if (len != sizeof(protocol_version_packet_t)) {
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid protocol version packet size: %u, expected %zu", len,
                       sizeof(protocol_version_packet_t));
    }
    break;
  case PACKET_TYPE_ASCII_FRAME:
    // ASCII frame contains header + frame data
    if (len < sizeof(ascii_frame_packet_t)) {
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid ASCII frame packet size: %u, minimum %zu", len,
                       sizeof(ascii_frame_packet_t));
    }
    break;
  case PACKET_TYPE_IMAGE_FRAME:
    // Image frame contains header + pixel data
    if (len < sizeof(image_frame_packet_t)) {
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid image frame packet size: %u, minimum %zu", len,
                       sizeof(image_frame_packet_t));
    }
    break;
  case PACKET_TYPE_AUDIO:
    if (len == 0 || len > AUDIO_SAMPLES_PER_PACKET * sizeof(float) * 2) { // Max stereo samples
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid audio packet size: %u", len);
    }
    break;
  case PACKET_TYPE_AUDIO_BATCH:
    // Batch must have at least header + some samples
    if (len < sizeof(audio_batch_packet_t) + sizeof(float)) {
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid audio batch packet size: %u", len);
    }
    break;
  case PACKET_TYPE_PING:
  case PACKET_TYPE_PONG:
    if (len != 0) { // Ping/pong are just header packets and no payload.
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid ping/pong packet size: %u", len);
    }
    break;
  case PACKET_TYPE_CLIENT_CAPABILITIES:
    // Client capabilities packet can be empty or contain capability data
    if (len > 1024) { // Reasonable limit for capability data
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid client capabilities packet size: %u", len);
    }
    break;
  case PACKET_TYPE_CLIENT_JOIN:
    if (len != sizeof(client_info_packet_t)) {
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid client join packet size: %u, expected %zu", len,
                       sizeof(client_info_packet_t));
    }
    break;
  case PACKET_TYPE_CLIENT_LEAVE:
    // Client leave packet can be empty or contain reason
    if (len > 256) {
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid client leave packet size: %u", len);
    }
    break;
  case PACKET_TYPE_STREAM_START:
  case PACKET_TYPE_STREAM_STOP:
    if (len != sizeof(uint32_t)) {
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid stream control packet size: %u, expected %zu", len,
                       sizeof(uint32_t));
    }
    break;
  case PACKET_TYPE_SIZE_MESSAGE:
    // Size message format: "SIZE:width,height"
    if (len == 0 || len > 32) { // Reasonable size for "SIZE:1234,5678"
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid size message packet size: %u", len);
    }
    break;
  case PACKET_TYPE_AUDIO_MESSAGE:
    // Audio message format: "AUDIO:num_samples"
    if (len == 0 || len > 32) { // Reasonable size for "AUDIO:1234"
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid audio message packet size: %u", len);
    }
    break;
  case PACKET_TYPE_TEXT_MESSAGE:
    // Text message can be empty or contain message
    if (len > 1024) { // Reasonable limit for text messages
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid text message packet size: %u", len);
    }
    break;
  case PACKET_TYPE_ERROR_MESSAGE:
    if (len < sizeof(error_packet_t)) {
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid error packet size: %u (minimum %zu)", len,
                       sizeof(error_packet_t));
    }
    if (len > sizeof(error_packet_t) + MAX_ERROR_MESSAGE_LENGTH) {
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Error packet message too large: %u (max %zu)", len,
                       sizeof(error_packet_t) + (size_t)MAX_ERROR_MESSAGE_LENGTH);
    }
    break;
  // All crypto handshake packet types - validate using session parameters
  case PACKET_TYPE_CRYPTO_CAPABILITIES:
  case PACKET_TYPE_CRYPTO_PARAMETERS:
  case PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT:
  case PACKET_TYPE_CRYPTO_KEY_EXCHANGE_RESP:
  case PACKET_TYPE_CRYPTO_AUTH_CHALLENGE:
  case PACKET_TYPE_CRYPTO_AUTH_RESPONSE:
  case PACKET_TYPE_CRYPTO_AUTH_FAILED:
  case PACKET_TYPE_CRYPTO_SERVER_AUTH_RESP:
  case PACKET_TYPE_CRYPTO_HANDSHAKE_COMPLETE:
  case PACKET_TYPE_CRYPTO_NO_ENCRYPTION:
  case PACKET_TYPE_ENCRYPTED:
    // Crypto packets are validated by the crypto handshake context
    // This is just a basic sanity check for extremely large packets
    if (len > 65536) { // 64KB should be enough for even large post-quantum crypto packets
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Crypto packet too large: %u bytes (max 65536)", len);
    }
    // Note: Proper size validation is done in crypto_handshake_validate_packet_size()
    // which uses the session's crypto parameters for accurate validation
    break;
  default:
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unknown packet type: %u", type);
  }

  // Return parsed values
  *pkt_type = type;
  *pkt_len = len;
  *expected_crc = crc;

  return ASCIICHAT_OK;
}

/**
 * @brief Validate packet CRC32
 * @param data Packet data
 * @param len Data length
 * @param expected_crc Expected CRC32 value
 * @return 0 on success, -1 on error
 */
asciichat_error_t packet_validate_crc32(const void *data, size_t len, uint32_t expected_crc) {
  if (!data && len > 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: data=%p but len=%zu", data, len);
  }

  if (len == 0) {
    // Empty packets should have CRC32 of 0
    if (expected_crc != 0) {
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid CRC32 for empty packet: 0x%x (expected 0)", expected_crc);
    }
    return 0;
  }

  uint32_t calculated_crc = asciichat_crc32(data, len);
  if (calculated_crc != expected_crc) {
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "CRC32 mismatch: calculated 0x%x, expected 0x%x", calculated_crc,
                     expected_crc);
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Send a packet with proper header and CRC32
 * @param sockfd Socket file descriptor
 * @param type Packet type
 * @param data Packet data
 * @param len Data length
 * @return 0 on success, -1 on error
 */
asciichat_error_t packet_send(socket_t sockfd, packet_type_t type, const void *data, size_t len) {
  if (sockfd == INVALID_SOCKET_VALUE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid socket descriptor");
  }

  if (len > MAX_PACKET_SIZE) {
    return SET_ERRNO(ERROR_NETWORK_SIZE, "Packet too large: %zu > %d", len, MAX_PACKET_SIZE);
  }

  packet_header_t header = {.magic = htonl(PACKET_MAGIC),
                            .type = htons((uint16_t)type),
                            .length = htonl((uint32_t)len),
                            .crc32 = htonl(len > 0 ? asciichat_crc32(data, len) : 0),
                            .client_id = htonl(0)}; // Always initialize client_id to 0 in network byte order

  // Calculate timeout based on packet size
  int timeout = calculate_packet_timeout(len);

  // Send header first
  ssize_t sent = send_with_timeout(sockfd, &header, sizeof(header), timeout);
  if (sent < 0) {
    // Error context is already set by send_with_timeout
    return ERROR_NETWORK;
  }
  if ((size_t)sent != sizeof(header)) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to fully send packet header. Sent %zd/%zu bytes", sent, sizeof(header));
  }

  // Send payload if present
  if (len > 0 && data) {
    // Check socket validity before sending payload to avoid race conditions
    if (!socket_is_valid(sockfd)) {
      return SET_ERRNO(ERROR_NETWORK, "Socket became invalid between header and payload send");
    }
    sent = send_with_timeout(sockfd, data, len, timeout);
    // Check for error first to avoid signed/unsigned comparison issues
    if (sent < 0) {
      // Error context is already set by send_with_timeout
      return ERROR_NETWORK;
    }
    if ((size_t)sent != len) {
      return SET_ERRNO(ERROR_NETWORK, "Failed to fully send packet payload. Sent %zd/%zu bytes", sent, len);
    }
  }

#ifdef DEBUG_NETWORK
  log_debug("Sent packet type=%d, len=%zu, errno=%d (%s)", type, len, errno, SAFE_STRERROR(errno));
#endif

  return 0;
}

/**
 * @brief Receive a packet with proper header validation and CRC32 checking
 * @param sockfd Socket file descriptor
 * @param type Output: packet type
 * @param data Output: packet data (allocated by caller, freed by caller)
 * @param len Output: data length
 * @return 0 on success, -1 on error
 */
asciichat_error_t packet_receive(socket_t sockfd, packet_type_t *type, void **data, size_t *len) {
  if (sockfd == INVALID_SOCKET_VALUE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid socket descriptor");
  }
  if (!type || !data || !len) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: type=%p, data=%p, len=%p", type, data, len);
  }

  // Read packet header into memory from network socket
  packet_header_t header;
  ssize_t received = recv_with_timeout(sockfd, &header, sizeof(header), is_test_environment() ? 1 : RECV_TIMEOUT);
  if (received < 0) {
    // Error context is already set by recv_with_timeout
    return ERROR_NETWORK;
  }
  if ((size_t)received != sizeof(header)) {
    if (received == 0) {
      log_warn("Connection closed while reading packet header");
      // Set output parameters to indicate connection closed
      *type = 0;
      *data = NULL;
      *len = 0;
      log_debug("Connection closed while reading packet header, setting type=0, data=NULL, len=0");
      return ASCIICHAT_OK;
    }

    return SET_ERRNO(ERROR_NETWORK, "Partial packet header received: %zd/%zu bytes", received, sizeof(header));
  }

  // Validate packet header
  uint16_t pkt_type;
  uint32_t pkt_len;
  uint32_t expected_crc;
  if (packet_validate_header(&header, &pkt_type, &pkt_len, &expected_crc) != ASCIICHAT_OK) {
    // Error context is already set by packet_validate_header
    return ERROR_NETWORK_PROTOCOL;
  }

  // Allocate buffer for payload
  void *payload = NULL;
  if (pkt_len > 0) {
    payload = buffer_pool_alloc(pkt_len);

    // Use adaptive timeout for large packets
    int recv_timeout = is_test_environment() ? 1 : calculate_packet_timeout(pkt_len);
    received = recv_with_timeout(sockfd, payload, pkt_len, recv_timeout);
    if (received < 0) {
      buffer_pool_free(payload, pkt_len);
      return SET_ERRNO_SYS(ERROR_NETWORK, "Failed to receive packet payload");
    }
    if (received != (ssize_t)pkt_len) {
      buffer_pool_free(payload, pkt_len);
      return SET_ERRNO(ERROR_NETWORK, "Partial packet payload received: %zd/%u bytes", received, pkt_len);
    }

    // Validate CRC32
    if (packet_validate_crc32(payload, pkt_len, expected_crc) != ASCIICHAT_OK) {
      buffer_pool_free(payload, pkt_len);
      // Error context is already set by packet_validate_crc32
      return ERROR_NETWORK_PROTOCOL;
    }
  }

  // Return results
  *type = (packet_type_t)pkt_type;
  *data = payload;
  *len = pkt_len;

  return ASCIICHAT_OK;
}

/* ============================================================================
 * High-Level Secure Packet Functions
 * ============================================================================
 * These functions handle encryption and compression for secure communication.
 */

/**
 * @brief Send a packet with encryption and compression support
 * @param sockfd Socket file descriptor
 * @param type Packet type
 * @param data Packet data
 * @param len Data length
 * @param crypto_ctx Crypto context for encryption
 * @return 0 on success, -1 on error
 */
int send_packet_secure(socket_t sockfd, packet_type_t type, const void *data, size_t len,
                       crypto_context_t *crypto_ctx) {
  if (len > MAX_PACKET_SIZE) {
    SET_ERRNO(ERROR_NETWORK_SIZE, "Packet too large: %zu > %d", len, MAX_PACKET_SIZE);
    return -1;
  }

  // Handshake packets are ALWAYS sent unencrypted
  if (packet_is_handshake_type(type)) {
    return packet_send(sockfd, type, data, len);
  }

  // Apply compression if beneficial for large packets
  const void *final_data = data;
  size_t final_len = len;
  void *compressed_data = NULL;

  if (len > COMPRESSION_MIN_SIZE && should_compress(len, len)) {
    void *temp_compressed = NULL;
    size_t compressed_size = 0;

    if (compress_data(data, len, &temp_compressed, &compressed_size) == 0) {
      double ratio = (double)compressed_size / (double)len;
      if (ratio < COMPRESSION_RATIO_THRESHOLD) {
        final_data = temp_compressed;
        final_len = compressed_size;
        compressed_data = temp_compressed;
        log_debug("Compressed packet: %zu -> %zu bytes (%.1f%%)", len, compressed_size, ratio * 100.0);
      } else {
        SAFE_FREE(temp_compressed);
      }
    }
  }

  // If no crypto context or crypto not ready, send unencrypted
  bool ready = crypto_ctx ? crypto_is_ready(crypto_ctx) : false;
  if (!crypto_ctx || !ready) {
    log_warn_every(1000000, "CRYPTO_DEBUG: Sending packet type %d UNENCRYPTED (crypto_ctx=%p, ready=%d)", type,
                   (void *)crypto_ctx, ready);
    int result = packet_send(sockfd, type, final_data, final_len);
    if (compressed_data) {
      SAFE_FREE(compressed_data);
    }
    if (result != ASCIICHAT_OK) {
      SET_ERRNO(ERROR_NETWORK, "Failed to send packet: %d", result);
    }
    return result;
  }

  // Encrypt the packet: create header + payload, encrypt everything, wrap in PACKET_TYPE_ENCRYPTED
  packet_header_t header = {.magic = htonl(PACKET_MAGIC),
                            .type = htons((uint16_t)type),
                            .length = htonl((uint32_t)final_len),
                            .crc32 = htonl(final_len > 0 ? asciichat_crc32(final_data, final_len) : 0),
                            .client_id = htonl(0)}; // Always 0 - client_id is not used in practice

  // Combine header + payload for encryption
  size_t plaintext_len = sizeof(header) + final_len;
  uint8_t *plaintext = buffer_pool_alloc(plaintext_len);
  if (!plaintext) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate buffer for plaintext packet");
    if (compressed_data) {
      SAFE_FREE(compressed_data);
    }
    return -1;
  }

  memcpy(plaintext, &header, sizeof(header));
  if (final_len > 0 && final_data) {
    memcpy(plaintext + sizeof(header), final_data, final_len);
  }

  // Encrypt
  size_t ciphertext_size = plaintext_len + CRYPTO_NONCE_SIZE + CRYPTO_MAC_SIZE;
  uint8_t *ciphertext = buffer_pool_alloc(ciphertext_size);
  if (!ciphertext) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate buffer for ciphertext");
    buffer_pool_free(plaintext, plaintext_len);
    if (compressed_data) {
      SAFE_FREE(compressed_data);
    }
    return -1;
  }

  size_t ciphertext_len;
  crypto_result_t result =
      crypto_encrypt(crypto_ctx, plaintext, plaintext_len, ciphertext, ciphertext_size, &ciphertext_len);
  buffer_pool_free(plaintext, plaintext_len);

  if (result != CRYPTO_OK) {
    SET_ERRNO(ERROR_CRYPTO, "Failed to encrypt packet: %s", crypto_result_to_string(result));
    buffer_pool_free(ciphertext, ciphertext_size);
    if (compressed_data) {
      SAFE_FREE(compressed_data);
    }
    return -1;
  }

  // Send as PACKET_TYPE_ENCRYPTED
  log_debug_every(10000000, "CRYPTO_DEBUG: Sending encrypted packet (original type %d as PACKET_TYPE_ENCRYPTED)", type);
  int send_result = packet_send(sockfd, PACKET_TYPE_ENCRYPTED, ciphertext, ciphertext_len);
  buffer_pool_free(ciphertext, ciphertext_size);

  if (compressed_data) {
    SAFE_FREE(compressed_data);
  }

  return send_result;
}

/**
 * @brief Receive a packet with decryption and decompression support
 * @param sockfd Socket file descriptor
 * @param crypto_ctx Crypto context for decryption
 * @param enforce_encryption Whether to require encryption
 * @param envelope Output: received packet envelope
 * @return Packet receive result
 */
packet_recv_result_t receive_packet_secure(socket_t sockfd, void *crypto_ctx, bool enforce_encryption,
                                           packet_envelope_t *envelope) {

  if (!envelope) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: envelope=%p", envelope);
    return PACKET_RECV_ERROR;
  }

  // Initialize envelope
  memset(envelope, 0, sizeof(*envelope));

  // Receive packet header
  packet_header_t header;
  ssize_t received = recv_with_timeout(sockfd, &header, sizeof(header), is_test_environment() ? 1 : RECV_TIMEOUT);

  // Check for errors first (before comparing signed with unsigned)
  if (received < 0) {
    SET_ERRNO(ERROR_NETWORK, "Failed to receive packet header: %zd/%zu bytes", received, sizeof(header));
    return PACKET_RECV_ERROR;
  }

  if (received == 0) {
    return PACKET_RECV_EOF;
  }

  if ((size_t)received != sizeof(header)) {
    SET_ERRNO(ERROR_NETWORK, "Failed to receive packet header: %zd/%zu bytes", received, sizeof(header));
    return PACKET_RECV_ERROR;
  }

  // Convert from network byte order
  uint32_t magic = ntohl(header.magic);
  uint16_t pkt_type = ntohs(header.type);
  uint32_t pkt_len = ntohl(header.length);
  uint32_t expected_crc = ntohl(header.crc32);

  // Validate magic number
  if (magic != PACKET_MAGIC) {
    SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid packet magic: 0x%x (expected 0x%x)", magic, PACKET_MAGIC);
    return PACKET_RECV_ERROR;
  }

  // Validate packet size
  if (pkt_len > MAX_PACKET_SIZE) {
    SET_ERRNO(ERROR_NETWORK_SIZE, "Packet too large: %u > %d", pkt_len, MAX_PACKET_SIZE);
    return PACKET_RECV_ERROR;
  }

  // Handle encrypted packets
  if (pkt_type == PACKET_TYPE_ENCRYPTED) {
    if (!crypto_ctx) {
      SET_ERRNO(ERROR_CRYPTO, "Received encrypted packet but no crypto context");
      return PACKET_RECV_ERROR;
    }

    // Read encrypted payload
    uint8_t *ciphertext = buffer_pool_alloc(pkt_len);
    if (!ciphertext) {
      SET_ERRNO(ERROR_MEMORY, "Failed to allocate buffer for ciphertext");
      return PACKET_RECV_ERROR;
    }

    int recv_timeout = is_test_environment() ? 1 : calculate_packet_timeout(pkt_len);
    received = recv_with_timeout(sockfd, ciphertext, pkt_len, recv_timeout);
    if (received != (ssize_t)pkt_len) {
      SET_ERRNO(ERROR_NETWORK, "Failed to receive encrypted payload: %zd/%u bytes", received, pkt_len);
      buffer_pool_free(ciphertext, pkt_len);
      return PACKET_RECV_ERROR;
    }

    // Decrypt
    size_t plaintext_size = pkt_len + 1024; // Extra space for decryption
    uint8_t *plaintext = buffer_pool_alloc(plaintext_size);
    if (!plaintext) {
      SET_ERRNO(ERROR_MEMORY, "Failed to allocate buffer for plaintext");
      buffer_pool_free(ciphertext, pkt_len);
      return PACKET_RECV_ERROR;
    }

    size_t plaintext_len;
    crypto_result_t result = crypto_decrypt(crypto_ctx, ciphertext, pkt_len, plaintext, plaintext_size, &plaintext_len);
    buffer_pool_free(ciphertext, pkt_len);

    if (result != CRYPTO_OK) {
      SET_ERRNO(ERROR_CRYPTO, "Failed to decrypt packet: %s", crypto_result_to_string(result));
      buffer_pool_free(plaintext, plaintext_size);
      return PACKET_RECV_ERROR;
    }

    if (plaintext_len < sizeof(packet_header_t)) {
      SET_ERRNO(ERROR_CRYPTO, "Decrypted packet too small: %zu < %zu", plaintext_len, sizeof(packet_header_t));
      buffer_pool_free(plaintext, plaintext_size);
      return PACKET_RECV_ERROR;
    }

    // Parse decrypted header
    packet_header_t *decrypted_header = (packet_header_t *)plaintext;
    pkt_type = ntohs(decrypted_header->type);
    pkt_len = ntohl(decrypted_header->length);
    expected_crc = ntohl(decrypted_header->crc32);

    // Extract payload
    size_t payload_len = plaintext_len - sizeof(packet_header_t);
    if (payload_len != pkt_len) {
      SET_ERRNO(ERROR_CRYPTO, "Decrypted payload size mismatch: %zu != %u", payload_len, pkt_len);
      buffer_pool_free(plaintext, plaintext_size);
      return PACKET_RECV_ERROR;
    }

    // Verify CRC
    if (pkt_len > 0) {
      uint32_t actual_crc = asciichat_crc32(plaintext + sizeof(packet_header_t), pkt_len);
      if (actual_crc != expected_crc) {
        SET_ERRNO(ERROR_CRYPTO, "Decrypted packet CRC mismatch: 0x%x != 0x%x", actual_crc, expected_crc);
        buffer_pool_free(plaintext, plaintext_size);
        return PACKET_RECV_ERROR;
      }
    }

    // Set envelope
    envelope->type = (packet_type_t)pkt_type;
    envelope->data = plaintext + sizeof(packet_header_t);
    envelope->len = pkt_len;
    envelope->allocated_buffer = plaintext;
    envelope->allocated_size = plaintext_size;

    return PACKET_RECV_SUCCESS;
  }

  // Handle unencrypted packets
  if (enforce_encryption && !packet_is_handshake_type(pkt_type)) {
    SET_ERRNO(ERROR_CRYPTO, "Received unencrypted packet but encryption is required");
    return PACKET_RECV_ERROR;
  }

  // Read payload
  if (pkt_len > 0) {
    uint8_t *payload = buffer_pool_alloc(pkt_len);
    if (!payload) {
      SET_ERRNO(ERROR_MEMORY, "Failed to allocate buffer for payload");
      return PACKET_RECV_ERROR;
    }

    int recv_timeout = is_test_environment() ? 1 : calculate_packet_timeout(pkt_len);
    received = recv_with_timeout(sockfd, payload, pkt_len, recv_timeout);
    if (received != (ssize_t)pkt_len) {
      SET_ERRNO(ERROR_NETWORK, "Failed to receive payload: %zd/%u bytes", received, pkt_len);
      buffer_pool_free(payload, pkt_len);
      return PACKET_RECV_ERROR;
    }

    // Verify CRC
    uint32_t actual_crc = asciichat_crc32(payload, pkt_len);
    if (actual_crc != expected_crc) {
      SET_ERRNO(ERROR_NETWORK, "Packet CRC mismatch: 0x%x != 0x%x", actual_crc, expected_crc);
      buffer_pool_free(payload, pkt_len);
      return PACKET_RECV_ERROR;
    }

    envelope->data = payload;
    envelope->allocated_buffer = payload;
    envelope->allocated_size = pkt_len;
  }

  envelope->type = (packet_type_t)pkt_type;
  envelope->len = pkt_len;

  return PACKET_RECV_SUCCESS;
}

/* ============================================================================
 * Basic Packet Functions (Non-Secure)
 * ============================================================================
 * These are the basic packet send/receive functions without encryption.
 */

/**
 * @brief Send a basic packet without encryption
 * @param sockfd Socket file descriptor
 * @param type Packet type
 * @param data Packet data
 * @param len Data length
 * @return 0 on success, -1 on error
 */
int send_packet(socket_t sockfd, packet_type_t type, const void *data, size_t len) {
  asciichat_error_t result = packet_send(sockfd, type, data, len);
  return result == ASCIICHAT_OK ? 0 : -1;
}

/**
 * @brief Receive a basic packet without encryption
 * @param sockfd Socket file descriptor
 * @param type Output: packet type
 * @param data Output: packet data
 * @param len Output: data length
 * @return 0 on success, -1 on error
 */
int receive_packet(socket_t sockfd, packet_type_t *type, void **data, size_t *len) {
  asciichat_error_t result = packet_receive(sockfd, type, data, len);
  return result == ASCIICHAT_OK ? 0 : -1;
}

/* ============================================================================
 * Protocol Message Functions
 * ============================================================================
 * These functions send specific protocol messages.
 */

/**
 * @brief Send a ping packet
 * @param sockfd Socket file descriptor
 * @return 0 on success, -1 on error
 */
int send_ping_packet(socket_t sockfd) {
  asciichat_error_t result = send_packet(sockfd, PACKET_TYPE_PING, NULL, 0);
  return result == ASCIICHAT_OK ? 0 : -1;
}

/**
 * @brief Send a pong packet
 * @param sockfd Socket file descriptor
 * @return 0 on success, -1 on error
 */
int send_pong_packet(socket_t sockfd) {
  asciichat_error_t result = send_packet(sockfd, PACKET_TYPE_PONG, NULL, 0);
  return result == ASCIICHAT_OK ? 0 : -1;
}

/**
 * @brief Send a clear console packet
 * @param sockfd Socket file descriptor
 * @return 0 on success, -1 on error
 */
int send_clear_console_packet(socket_t sockfd) {
  return send_packet(sockfd, PACKET_TYPE_CLEAR_CONSOLE, NULL, 0);
}

asciichat_error_t packet_send_error(socket_t sockfd, const crypto_context_t *crypto_ctx, asciichat_error_t error_code,
                                    const char *message) {
  if (sockfd == INVALID_SOCKET_VALUE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid socket descriptor");
  }

  if (!message) {
    message = "";
  }

  size_t message_len = strnlen(message, MAX_ERROR_MESSAGE_LENGTH);
  if (message_len == MAX_ERROR_MESSAGE_LENGTH) {
    log_warn("Error message truncated to %zu bytes", (size_t)MAX_ERROR_MESSAGE_LENGTH);
  }

  size_t payload_len = sizeof(error_packet_t) + message_len;
  uint8_t *payload = SAFE_MALLOC(payload_len, uint8_t *);
  if (!payload) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate %zu bytes for error packet", payload_len);
  }

  error_packet_t *packet = (error_packet_t *)payload;
  packet->error_code = htonl((uint32_t)error_code);
  packet->message_length = htonl((uint32_t)message_len);

  if (message_len > 0) {
    memcpy(payload + sizeof(error_packet_t), message, message_len);
  }

  bool encryption_ready = crypto_ctx && crypto_is_ready(crypto_ctx);
  asciichat_error_t send_result;

  if (encryption_ready) {
    int secure_result =
        send_packet_secure(sockfd, PACKET_TYPE_ERROR_MESSAGE, payload, payload_len, (crypto_context_t *)crypto_ctx);
    send_result = secure_result == ASCIICHAT_OK ? ASCIICHAT_OK : ERROR_NETWORK;
  } else {
    send_result = packet_send(sockfd, PACKET_TYPE_ERROR_MESSAGE, payload, payload_len);
  }
  SAFE_FREE(payload);

  if (send_result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to send error packet: %d", send_result);
  }

  return ASCIICHAT_OK;
}

asciichat_error_t packet_parse_error_message(const void *data, size_t len, asciichat_error_t *out_error_code,
                                             char *message_buffer, size_t message_buffer_size,
                                             size_t *out_message_length) {
  if (!data || len < sizeof(error_packet_t) || !out_error_code || !message_buffer || message_buffer_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM,
                     "Invalid parameters: data=%p len=%zu out_error_code=%p message_buffer=%p buffer_size=%zu", data,
                     len, out_error_code, message_buffer, message_buffer_size);
  }

  const error_packet_t *packet = (const error_packet_t *)data;
  uint32_t raw_error_code = ntohl(packet->error_code);
  uint32_t raw_message_length = ntohl(packet->message_length);

  if (raw_message_length > MAX_ERROR_MESSAGE_LENGTH) {
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Error message length too large: %u", raw_message_length);
  }

  size_t total_required = sizeof(error_packet_t) + (size_t)raw_message_length;
  if (total_required > len) {
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Error packet truncated: expected %zu bytes, have %zu", total_required,
                     len);
  }

  const uint8_t *message_bytes = (const uint8_t *)data + sizeof(error_packet_t);
  size_t copy_len = raw_message_length;
  if (copy_len >= message_buffer_size) {
    copy_len = message_buffer_size - 1;
  }

  if (copy_len > 0) {
    memcpy(message_buffer, message_bytes, copy_len);
  }
  message_buffer[copy_len] = '\0';

  if (out_message_length) {
    *out_message_length = raw_message_length;
  }

  *out_error_code = (asciichat_error_t)raw_error_code;
  return ASCIICHAT_OK;
}

/**
 * @brief Send protocol version packet
 * @param sockfd Socket file descriptor
 * @param version Protocol version packet
 * @return 0 on success, -1 on error
 */
int send_protocol_version_packet(socket_t sockfd, const protocol_version_packet_t *version) {
  if (!version) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: version=%p", version);
    return -1;
  }
  return send_packet(sockfd, PACKET_TYPE_PROTOCOL_VERSION, version, sizeof(*version));
}

/**
 * @brief Send crypto capabilities packet
 * @param sockfd Socket file descriptor
 * @param caps Crypto capabilities packet
 * @return 0 on success, -1 on error
 */
int send_crypto_capabilities_packet(socket_t sockfd, const crypto_capabilities_packet_t *caps) {
  if (!caps) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: caps=%p", caps);
    return -1;
  }
  return send_packet(sockfd, PACKET_TYPE_CRYPTO_CAPABILITIES, caps, sizeof(*caps));
}

/**
 * @brief Send crypto parameters packet
 * @param sockfd Socket file descriptor
 * @param params Crypto parameters packet
 * @return 0 on success, -1 on error
 */
int send_crypto_parameters_packet(socket_t sockfd, const crypto_parameters_packet_t *params) {
  if (!params) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: params=%p", params);
    return -1;
  }

  // Create a copy and convert uint16_t fields to network byte order
  crypto_parameters_packet_t net_params = *params;
  log_debug("NETWORK_DEBUG: Before htons: kex=%u, auth=%u, sig=%u, secret=%u", params->kex_public_key_size,
            params->auth_public_key_size, params->signature_size, params->shared_secret_size);
  net_params.kex_public_key_size = htons(params->kex_public_key_size);
  net_params.auth_public_key_size = htons(params->auth_public_key_size);
  net_params.signature_size = htons(params->signature_size);
  net_params.shared_secret_size = htons(params->shared_secret_size);
  log_debug("NETWORK_DEBUG: After htons: kex=%u, auth=%u, sig=%u, secret=%u", net_params.kex_public_key_size,
            net_params.auth_public_key_size, net_params.signature_size, net_params.shared_secret_size);

  return send_packet(sockfd, PACKET_TYPE_CRYPTO_PARAMETERS, &net_params, sizeof(net_params));
}
