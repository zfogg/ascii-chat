/**
 * @file network/packet.h
 * @ingroup network
 * @brief Packet protocol implementation with encryption and compression support
 *
 * This module provides comprehensive packet protocol implementation including
 * packet verification, CRC validation, protocol compliance checking, encryption
 * support, and compression integration. It serves as the core network protocol
 * layer for ascii-chat's communication system.
 *
 * CORE RESPONSIBILITIES:
 * ======================
 * 1. Packet header validation and CRC32 checksum verification
 * 2. Secure packet transmission with encryption support
 * 3. Secure packet reception with decryption and validation
 * 4. Protocol compliance checking and error handling
 * 5. Integration with cryptographic and compression subsystems
 *
 * ARCHITECTURAL OVERVIEW:
 * =======================
 *
 * PACKET STRUCTURE:
 * - Header: Magic number, type, length, CRC32, client ID
 * - Payload: Variable-length data (may be encrypted/compressed)
 * - Validation: CRC32 checksum for integrity verification
 *
 * ENCRYPTION INTEGRATION:
 * - Automatic encryption/decryption when crypto context provided
 * - Handshake packets always sent unencrypted (plaintext)
 * - Session packets encrypted after handshake completion
 * - Encryption policy enforcement (require/optional encryption)
 *
 * COMPRESSION INTEGRATION:
 * - Automatic compression for large packets (frames, audio)
 * - Compression threshold-based decision making
 * - Decompression on receive path
 *
 * PROTOCOL VALIDATION:
 * - Magic number validation to detect corruption
 * - CRC32 checksum verification for integrity
 * - Packet size validation to prevent attacks
 * - Type validation for protocol compliance
 *
 * INTEGRATION WITH OTHER MODULES:
 * ===============================
 * - crypto/crypto.h: Encryption/decryption operations
 * - compression.h: Compression/decompression support
 * - network/network.h: Socket I/O operations
 * - network/packet_types.h: Packet type definitions
 *
 * PERFORMANCE CHARACTERISTICS:
 * ============================
 * - Zero-copy operation when possible
 * - Automatic memory management for packet buffers
 * - Efficient CRC32 validation (hardware-accelerated when available)
 * - Compression reduces bandwidth for large frames
 *
 * @note All packet functions handle encryption automatically when crypto
 *       context is provided. Plaintext packets are used for handshake.
 *
 * @note Compression is automatically applied to large packets (frames,
 *       audio batches) based on size thresholds.
 *
 * @warning Packet buffers are allocated by receive functions and must be
 *          freed by caller using the allocated_buffer pointer.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 * @version 2.0 (Post-Modularization)
 */

#pragma once

#include "platform/socket.h"
#include "packet_types.h"
#include "crypto/crypto.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Packet envelope containing received packet data
 *
 * Represents a complete received packet with all metadata. The envelope
 * includes packet type, payload data, encryption status, and buffer
 * ownership information for proper memory management.
 *
 * MEMORY MANAGEMENT:
 * - allocated_buffer points to buffer that needs to be freed
 * - allocated_size indicates size of allocated buffer
 * - Caller must free allocated_buffer when done with packet
 *
 * @note If was_encrypted is true, data has already been decrypted
 *       before being placed in the envelope.
 *
 * @note allocated_buffer may be different from data if packet
 *       was encrypted/compressed (data points into allocated_buffer).
 *
 * @ingroup network
 */
typedef struct {
  /** @brief Packet type (from packet_types.h) */
  packet_type_t type;
  /** @brief Packet payload data (decrypted and decompressed if applicable) */
  void *data;
  /** @brief Length of payload data in bytes */
  size_t len;
  /** @brief True if packet was encrypted (decrypted before envelope creation) */
  bool was_encrypted;
  /** @brief Buffer that needs to be freed by caller (may be NULL if not allocated) */
  void *allocated_buffer;
  /** @brief Size of allocated buffer in bytes */
  size_t allocated_size;
} packet_envelope_t;

/**
 * @brief Packet reception result codes
 *
 * Result codes for packet reception operations. Negative values indicate
 * errors, zero indicates success.
 *
 * @ingroup network
 */
typedef enum {
  /** @brief Packet received successfully */
  PACKET_RECV_SUCCESS = 0,
  /** @brief Connection closed (EOF) */
  PACKET_RECV_EOF = -1,
  /** @brief Network error occurred */
  PACKET_RECV_ERROR = -2,
  /** @brief Encryption policy violation (e.g., unencrypted packet when encryption required) */
  PACKET_RECV_SECURITY_VIOLATION = -3
} packet_recv_result_t;

/**
 * @name Packet Validation Functions
 * @{
 * @ingroup network
 */

/**
 * @brief Validate packet header and extract information
 * @param header Packet header to validate
 * @param pkt_type Output: Packet type
 * @param pkt_len Output: Packet payload length
 * @param expected_crc Output: Expected CRC32 checksum
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Validates packet header structure including magic number, type,
 * and length fields. Extracts packet information for further processing.
 *
 * @note Validates magic number to detect corrupted packets.
 *
 * @note All output parameters must be non-NULL.
 *
 * @ingroup network
 */
asciichat_error_t packet_validate_header(const packet_header_t *header, uint16_t *pkt_type, uint32_t *pkt_len,
                                         uint32_t *expected_crc);

/**
 * @brief Validate packet CRC32 checksum
 * @param data Packet payload data
 * @param len Data length in bytes
 * @param expected_crc Expected CRC32 checksum value
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Validates packet integrity by computing CRC32 checksum of payload
 * and comparing with expected value. Uses hardware acceleration when
 * available for optimal performance.
 *
 * @note CRC32 validation prevents corrupted packets from being processed.
 *
 * @note Uses hardware-accelerated CRC32 when available (automatic fallback
 *       to software implementation).
 *
 * @ingroup network
 */
asciichat_error_t packet_validate_crc32(const void *data, size_t len, uint32_t expected_crc);

/** @} */

/**
 * @name Basic Packet I/O Functions
 * @{
 * @ingroup network
 */

/**
 * @brief Send a packet with header and CRC32 checksum
 * @param sockfd Socket file descriptor
 * @param type Packet type (from packet_types.h)
 * @param data Packet payload data (can be NULL for header-only packets)
 * @param len Payload data length in bytes
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Sends a complete packet with header (magic, type, length, CRC32) and
 * optional payload. CRC32 is computed automatically and included in header.
 *
 * @note Packet header is constructed with all required fields including
 *       magic number, type, length, CRC32, and client ID.
 *
 * @note CRC32 is computed over payload data (or zero if data is NULL).
 *
 * @ingroup network
 */
asciichat_error_t packet_send(socket_t sockfd, packet_type_t type, const void *data, size_t len);

/**
 * @brief Receive a packet with header validation and CRC32 checking
 * @param sockfd Socket file descriptor
 * @param type Output: Packet type
 * @param data Output: Packet payload data (allocated by function, freed by caller)
 * @param len Output: Payload data length in bytes
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Receives a complete packet, validates header, and verifies CRC32 checksum.
 * Allocates buffer for payload data which must be freed by caller.
 *
 * @note Allocates buffer for payload data - caller must free when done.
 *
 * @note Validates magic number and CRC32 checksum before returning data.
 *
 * @warning Allocated data buffer must be freed by caller to prevent memory leaks.
 *
 * @ingroup network
 */
asciichat_error_t packet_receive(socket_t sockfd, packet_type_t *type, void **data, size_t *len);

/** @} */

/**
 * @name Secure Packet I/O Functions
 * @{
 * @ingroup network
 */

/**
 * @brief Send a packet with encryption and compression support
 * @param sockfd Socket file descriptor
 * @param type Packet type (from packet_types.h)
 * @param data Packet payload data
 * @param len Payload data length in bytes
 * @param crypto_ctx Cryptographic context for encryption (NULL for plaintext)
 * @return 0 on success, negative on error
 *
 * Sends a packet with automatic encryption (if crypto context provided) and
 * compression (if packet is large enough). Handshake packets are never encrypted.
 *
 * @note Encryption is applied automatically when crypto_ctx is provided and
 *       packet type is not a handshake packet.
 *
 * @note Compression is applied automatically to large packets based on
 *       size thresholds and compression ratios.
 *
 * @note Handshake packets (packet_is_handshake_type(type) == true) are
 *       always sent unencrypted, even when crypto_ctx is provided.
 *
 * @ingroup network
 */
int send_packet_secure(socket_t sockfd, packet_type_t type, const void *data, size_t len, crypto_context_t *crypto_ctx);

/**
 * @brief Receive a packet with decryption and decompression support
 * @param sockfd Socket file descriptor
 * @param crypto_ctx Cryptographic context for decryption (NULL for plaintext)
 * @param enforce_encryption If true, reject unencrypted packets (except handshake)
 * @param envelope Output: Received packet envelope with all metadata
 * @return PACKET_RECV_SUCCESS on success, error code on failure
 *
 * Receives a packet with automatic decryption (if encrypted) and decompression
 * (if compressed). Validates encryption policy and packet integrity.
 *
 * @note Decryption is applied automatically when packet is encrypted and
 *       crypto_ctx is provided.
 *
 * @note Decompression is applied automatically when packet is compressed.
 *
 * @note If enforce_encryption is true, unencrypted packets (except handshake
 *       packets) cause PACKET_RECV_SECURITY_VIOLATION.
 *
 * @warning Envelope's allocated_buffer must be freed by caller to prevent
 *          memory leaks.
 *
 * @ingroup network
 */
packet_recv_result_t receive_packet_secure(socket_t sockfd, void *crypto_ctx, bool enforce_encryption,
                                           packet_envelope_t *envelope);

/** @} */

/**
 * @name Legacy Packet I/O Functions
 * @{
 * @ingroup network
 *
 * These functions provide basic packet I/O without encryption support.
 * Use send_packet_secure() and receive_packet_secure() for encryption support.
 */

/**
 * @brief Send a basic packet without encryption
 * @param sockfd Socket file descriptor
 * @param type Packet type (from packet_types.h)
 * @param data Packet payload data
 * @param len Payload data length in bytes
 * @return 0 on success, -1 on error
 *
 * Sends a plaintext packet without encryption. Equivalent to packet_send()
 * but returns int instead of asciichat_error_t for compatibility.
 *
 * @note Use send_packet_secure() if encryption support is needed.
 *
 * @ingroup network
 */
int send_packet(socket_t sockfd, packet_type_t type, const void *data, size_t len);

/**
 * @brief Receive a basic packet without encryption
 * @param sockfd Socket file descriptor
 * @param type Output: Packet type
 * @param data Output: Packet payload data (allocated by function)
 * @param len Output: Payload data length in bytes
 * @return 0 on success, -1 on error
 *
 * Receives a plaintext packet without decryption. Allocates buffer for
 * payload data which must be freed by caller.
 *
 * @note Use receive_packet_secure() if decryption support is needed.
 *
 * @warning Allocated data buffer must be freed by caller.
 *
 * @ingroup network
 */
int receive_packet(socket_t sockfd, packet_type_t *type, void **data, size_t *len);

/** @} */

/**
 * @name Protocol Packet Functions
 * @{
 * @ingroup network
 *
 * Convenience functions for sending specific protocol packets.
 * These functions construct the appropriate packet structure and
 * send it over the socket.
 */

/**
 * @brief Send a ping packet (keepalive)
 * @param sockfd Socket file descriptor
 * @return 0 on success, -1 on error
 *
 * Sends a PACKET_TYPE_PING packet to keep connection alive.
 * Server/client responds with PACKET_TYPE_PONG.
 *
 * @note Ping packets are always sent unencrypted (handshake packets).
 *
 * @ingroup network
 */
int send_ping_packet(socket_t sockfd);

/**
 * @brief Send a pong packet (keepalive response)
 * @param sockfd Socket file descriptor
 * @return 0 on success, -1 on error
 *
 * Sends a PACKET_TYPE_PONG packet in response to PACKET_TYPE_PING.
 * Indicates that connection is alive and responsive.
 *
 * @note Pong packets are always sent unencrypted (handshake packets).
 *
 * @ingroup network
 */
int send_pong_packet(socket_t sockfd);

/**
 * @brief Send a clear console packet
 * @param sockfd Socket file descriptor
 * @return 0 on success, -1 on error
 *
 * Sends a PACKET_TYPE_CLEAR_CONSOLE packet to request client
 * to clear terminal display.
 *
 * @ingroup network
 */
int send_clear_console_packet(socket_t sockfd);

/**
 * @brief Send protocol version negotiation packet
 * @param sockfd Socket file descriptor
 * @param version Protocol version packet structure
 * @return 0 on success, -1 on error
 *
 * Sends a PACKET_TYPE_PROTOCOL_VERSION packet for protocol capability
 * negotiation. Both client and server send this during handshake.
 *
 * @note Protocol version packets are always sent unencrypted (handshake packets).
 *
 * @ingroup network
 */
int send_protocol_version_packet(socket_t sockfd, const protocol_version_packet_t *version);

/**
 * @brief Send crypto capabilities packet
 * @param sockfd Socket file descriptor
 * @param caps Crypto capabilities packet structure
 * @return 0 on success, -1 on error
 *
 * Sends a PACKET_TYPE_CRYPTO_CAPABILITIES packet to advertise supported
 * cryptographic algorithms (key exchange, authentication, cipher).
 *
 * @note Crypto capabilities packets are always sent unencrypted (handshake packets).
 *
 * @ingroup network
 */
int send_crypto_capabilities_packet(socket_t sockfd, const crypto_capabilities_packet_t *caps);

/**
 * @brief Send crypto parameters packet
 * @param sockfd Socket file descriptor
 * @param params Crypto parameters packet structure
 * @return 0 on success, -1 on error
 *
 * Sends a PACKET_TYPE_CRYPTO_PARAMETERS packet containing chosen cryptographic
 * algorithms and data sizes for handshake continuation.
 *
 * @note Crypto parameters packets are always sent unencrypted (handshake packets).
 *
 * @ingroup network
 */
int send_crypto_parameters_packet(socket_t sockfd, const crypto_parameters_packet_t *params);

/** @} */
