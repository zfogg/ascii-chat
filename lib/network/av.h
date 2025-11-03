#pragma once

/**
 * @defgroup av Audio/Video Networking
 * @ingroup module_network
 * @brief Audio/Video/ASCII Packet Network Protocol Implementation
 *
 * @file network/av.h
 * @ingroup av
 * @brief Audio/Video/ASCII Packet Network Protocol Implementation
 *
 * This header provides the network protocol implementation for audio, video,
 * and ASCII frame packets in ascii-chat. The system handles packet serialization,
 * compression, encryption integration, and message formatting for real-time
 * media streaming over TCP.
 *
 * CORE FEATURES:
 * ==============
 * - ASCII frame packet transmission (text-based video frames)
 * - Image frame packet transmission (raw pixel data)
 * - Audio packet transmission (single and batched)
 * - Protocol message handling (size, audio, text)
 * - Compression integration for large packets
 * - Cryptographic encryption support
 * - Error handling and validation
 *
 * PACKET TYPES:
 * =============
 * The system handles multiple packet types:
 * - ASCII frames: Text-based video frames for terminal display
 * - Image frames: Raw pixel data (RGB, RGBA, etc.)
 * - Audio packets: Single audio sample packets (legacy)
 * - Audio batch packets: Batched audio samples (efficient)
 * - Protocol messages: Size, audio, text messages
 *
 * PROTOCOL INTEGRATION:
 * =====================
 * The system integrates with:
 * - Packet header system (packet.h) for framing
 * - Compression system for bandwidth reduction
 * - Cryptographic system for encryption (optional)
 * - Network abstraction layer for cross-platform support
 *
 * MESSAGE FORMATS:
 * ================
 * Protocol messages use text-based formats:
 * - Size messages: "SIZE:width,height\n"
 * - Audio messages: "AUDIO:num_samples\n"
 * - Text messages: Plain text with headers
 *
 * @note All packet functions handle encryption when crypto context is provided.
 * @note Large packets (frames, audio batches) support automatic compression.
 * @note Message parsing includes validation to prevent protocol errors.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stdint.h>
#include <stddef.h>
#include "platform/socket.h"
#include "crypto/crypto.h"

/* Size communication protocol */
#define SIZE_MESSAGE_PREFIX "SIZE:"
#define SIZE_MESSAGE_FORMAT "SIZE:%u,%u\n"
#define SIZE_MESSAGE_MAX_LEN 32

/* Audio communication protocol */
#define AUDIO_MESSAGE_PREFIX "AUDIO:"
#define AUDIO_MESSAGE_FORMAT "AUDIO:%u\n"
#define AUDIO_MESSAGE_MAX_LEN 32

/**
 * @name Frame Packet Functions
 * @{
 * @ingroup av
 * @ingroup network
 */

/**
 * @brief Send ASCII frame packet with compression support
 * @param sockfd Socket file descriptor
 * @param frame_data ASCII frame data (null-terminated string)
 * @param frame_size Size of frame data in bytes (excluding null terminator)
 * @return 0 on success, -1 on error
 *
 * Sends a PACKET_TYPE_ASCII_FRAME packet containing ASCII frame data
 * with metadata (width, height, compression status, checksum).
 *
 * @note Frame data may be automatically compressed if size exceeds threshold.
 *
 * @note Compression is applied only if it reduces size by threshold percentage.
 *
 * @ingroup av
 * @ingroup network
 */
int av_send_ascii_frame(socket_t sockfd, const char *frame_data, size_t frame_size);

/**
 * @brief Send image frame packet with compression support
 * @param sockfd Socket file descriptor
 * @param image_data Image pixel data (RGB, RGBA, BGR, etc.)
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param format Pixel format (0=RGB, 1=RGBA, 2=BGR, etc.)
 * @return 0 on success, -1 on error
 *
 * Sends a PACKET_TYPE_IMAGE_FRAME packet containing raw image pixel data
 * with metadata (dimensions, format, compression status, checksum, timestamp).
 *
 * @note Image data may be automatically compressed if size exceeds threshold.
 *
 * @note Compression reduces bandwidth for large image frames.
 *
 * @ingroup av
 * @ingroup network
 */
int av_send_image_frame(socket_t sockfd, const void *image_data, uint16_t width, uint16_t height, uint8_t format);

/** @} */

/**
 * @name Audio Packet Functions
 * @{
 * @ingroup av
 * @ingroup network
 */

/**
 * @brief Send single audio packet (legacy)
 * @param sockfd Socket file descriptor
 * @param samples Audio samples (float array)
 * @param num_samples Number of samples in array
 * @return 0 on success, -1 on error
 *
 * Sends a PACKET_TYPE_AUDIO packet containing single audio chunk.
 * Legacy function - use av_send_audio_batch() for better efficiency.
 *
 * @note Use av_send_audio_batch() for better bandwidth efficiency.
 *
 * @ingroup av
 * @ingroup network
 */
int av_send_audio(socket_t sockfd, const float *samples, int num_samples);

/**
 * @brief Send audio batch packet with encryption support
 * @param sockfd Socket file descriptor
 * @param samples Audio samples (float array)
 * @param num_samples Total number of samples across all batches
 * @param batch_count Number of audio chunks in batch (usually AUDIO_BATCH_COUNT)
 * @param crypto_ctx Cryptographic context for encryption (NULL for plaintext)
 * @return 0 on success, -1 on error
 *
 * Sends a PACKET_TYPE_AUDIO_BATCH packet containing multiple audio chunks
 * aggregated into a single packet for efficiency. Supports encryption when
 * crypto context is provided.
 *
 * @note Batching reduces packet overhead and improves bandwidth efficiency.
 *
 * @note Encryption is applied automatically when crypto_ctx is provided.
 *
 * @ingroup av
 */
int send_audio_batch_packet(socket_t sockfd, const float *samples, int num_samples, int batch_count,
                            crypto_context_t *crypto_ctx);

/**
 * @brief Send audio batch packet (convenience function)
 * @param sockfd Socket file descriptor
 * @param samples Audio samples (float array)
 * @param num_samples Total number of samples across all batches
 * @param sample_rate Audio sample rate (e.g., 44100)
 * @return 0 on success, -1 on error
 *
 * Convenience function for sending audio batch packets. Constructs
 * audio_batch_packet_t structure and sends PACKET_TYPE_AUDIO_BATCH packet.
 *
 * @note This function does not support encryption. Use send_audio_batch_packet()
 *       with crypto_ctx parameter for encryption support.
 *
 * @ingroup av
 */
int av_send_audio_batch(socket_t sockfd, const float *samples, int num_samples, int sample_rate);

/** @} */

/**
 * @name Message Packet Functions
 * @{
 * @ingroup av
 *
 * Protocol message functions for text-based communication.
 * Messages use simple string formats for compatibility.
 */

/**
 * @brief Send terminal size message packet
 * @param sockfd Socket file descriptor
 * @param width Terminal width in characters
 * @param height Terminal height in characters
 * @return 0 on success, -1 on error
 *
 * Sends a PACKET_TYPE_SIZE_MESSAGE packet with terminal dimensions.
 * Message format: "SIZE:width,height\n"
 *
 * @note Message format is text-based for compatibility.
 *
 * @ingroup av
 */
int av_send_size_message(socket_t sockfd, unsigned short width, unsigned short height);

/**
 * @brief Send audio message packet
 * @param sockfd Socket file descriptor
 * @param num_samples Number of audio samples in subsequent audio packet
 * @return 0 on success, -1 on error
 *
 * Sends a PACKET_TYPE_AUDIO_MESSAGE packet announcing upcoming audio data.
 * Message format: "AUDIO:num_samples\n"
 *
 * @note Message format is text-based for compatibility.
 *
 * @ingroup av
 */
int av_send_audio_message(socket_t sockfd, unsigned int num_samples);

/**
 * @brief Send text message packet
 * @param sockfd Socket file descriptor
 * @param text Text message to send
 * @return 0 on success, -1 on error
 *
 * Sends a PACKET_TYPE_TEXT_MESSAGE packet containing plain text message.
 *
 * @note Text message is sent as-is without modification.
 *
 * @ingroup av
 */
int av_send_text_message(socket_t sockfd, const char *text);

/**
 * @brief Receive audio message packet
 * @param sockfd Socket file descriptor
 * @param header Message header string (must match AUDIO_MESSAGE_PREFIX)
 * @param samples Output buffer for audio samples (float array)
 * @param max_samples Maximum number of samples that fit in buffer
 * @return Number of samples received on success, -1 on error
 *
 * Receives audio message packet and extracts audio samples into buffer.
 * Parses message header to determine number of samples.
 *
 * @warning Buffer must be large enough for num_samples float values.
 *
 * @ingroup av
 */
int av_receive_audio_message(socket_t sockfd, const char *header, float *samples, size_t max_samples);

/** @} */

/**
 * @name Message Parsing Functions
 * @{
 * @ingroup av
 */

/**
 * @brief Parse size message string
 * @param message Size message string (format: "SIZE:width,height\n")
 * @param width Output: Terminal width in characters
 * @param height Output: Terminal height in characters
 * @return 0 on success, -1 on error
 *
 * Parses size message string and extracts width and height values.
 * Validates message format and converts string values to integers.
 *
 * @note Message format must match SIZE_MESSAGE_FORMAT exactly.
 *
 * @note All output parameters must be non-NULL.
 *
 * @ingroup av
 */
int av_parse_size_message(const char *message, unsigned short *width, unsigned short *height);

/** @} */

