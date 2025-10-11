#include "av.h"
#include "network.h"
#include "packet.h"
#include "common.h"
#include "asciichat_errno.h"
#include "platform/socket.h"
#include "platform/string.h"
#include "buffer_pool.h"
#include "packet_types.h"
#include <stdint.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Audio/Video/ASCII Packet Implementation
 * ============================================================================
 * This module handles audio, video, and ASCII frame packets including
 * compression, message formatting, and specialized packet types.
 */

// Check if we're in a test environment
static int is_test_environment(void) {
  return SAFE_GETENV("CRITERION_TEST") != NULL || SAFE_GETENV("TESTING") != NULL;
}

/**
 * @brief Send ASCII frame packet
 * @param sockfd Socket file descriptor
 * @param frame_data ASCII frame data
 * @param frame_size Size of frame data
 * @return 0 on success, -1 on error
 */
int av_send_ascii_frame(socket_t sockfd, const char *frame_data, size_t frame_size) {
  if (!frame_data || frame_size == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: frame_data=%p, frame_size=%zu", frame_data, frame_size);
    return -1;
  }

  // Create ASCII frame packet
  ascii_frame_packet_t packet;
  packet.width = 0;  // Will be set by receiver
  packet.height = 0; // Will be set by receiver
  packet.original_size = frame_size;
  packet.compressed_size = 0;
  packet.checksum = 0;
  packet.flags = 0;

  // Calculate total packet size
  size_t total_size = sizeof(ascii_frame_packet_t) + frame_size;

  // Allocate buffer for complete packet
  void *packet_data = buffer_pool_alloc(total_size);
  if (!packet_data) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate buffer for ASCII frame packet: %zu bytes", total_size);
    return -1;
  }

  // Copy packet header and frame data
  memcpy(packet_data, &packet, sizeof(ascii_frame_packet_t));
  memcpy((char *)packet_data + sizeof(ascii_frame_packet_t), frame_data, frame_size);

  // Send packet
  int result = packet_send(sockfd, PACKET_TYPE_ASCII_FRAME, packet_data, total_size);

  // Clean up
  buffer_pool_free(packet_data, total_size);

  return result;
}

/**
 * @brief Send image frame packet
 * @param sockfd Socket file descriptor
 * @param image_data Image pixel data
 * @param width Image width
 * @param height Image height
 * @param format Pixel format
 * @return 0 on success, -1 on error
 */
int av_send_image_frame(socket_t sockfd, const void *image_data, uint16_t width, uint16_t height, uint8_t format) {
  if (!image_data || width == 0 || height == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: image_data=%p, width=%u, height=%u", image_data, width, height);
    return -1;
  }

  // Create image frame packet
  image_frame_packet_t packet;
  packet.width = width;
  packet.height = height;
  packet.pixel_format = format;
  packet.compressed_size = 0;
  packet.checksum = 0;
  packet.timestamp = 0; // Will be set by receiver

  // Calculate total packet size
  size_t frame_size = width * height * 3; // Assume RGB format
  size_t total_size = sizeof(image_frame_packet_t) + frame_size;

  // Allocate buffer for complete packet
  void *packet_data = buffer_pool_alloc(total_size);
  if (!packet_data) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate buffer for image frame packet: %zu bytes", total_size);
    return -1;
  }

  // Copy packet header and image data
  memcpy(packet_data, &packet, sizeof(image_frame_packet_t));
  memcpy((char *)packet_data + sizeof(image_frame_packet_t), image_data, frame_size);

  // Send packet
  int result = packet_send(sockfd, PACKET_TYPE_IMAGE_FRAME, packet_data, total_size);

  // Clean up
  buffer_pool_free(packet_data, total_size);

  return result;
}

/**
 * @brief Send audio packet
 * @param sockfd Socket file descriptor
 * @param samples Audio samples
 * @param num_samples Number of samples
 * @return 0 on success, -1 on error
 */
int av_send_audio(socket_t sockfd, const float *samples, int num_samples) {
  if (!samples || num_samples <= 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: samples=%p, num_samples=%d", samples, num_samples);
    return -1;
  }

  // Send packet
  return packet_send(sockfd, PACKET_TYPE_AUDIO, samples, num_samples * sizeof(float));
}

/**
 * @brief Send audio batch packet
 * @param sockfd Socket file descriptor
 * @param samples Audio samples
 * @param num_samples Number of samples
 * @param sample_rate Sample rate
 * @return 0 on success, -1 on error
 */
int av_send_audio_batch(socket_t sockfd, const float *samples, int num_samples, int sample_rate) {
  if (!samples || num_samples <= 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: samples=%p, num_samples=%d", samples, num_samples);
    return -1;
  }

  // Create audio batch packet
  audio_batch_packet_t packet;
  packet.batch_count = 1;
  packet.total_samples = num_samples;
  packet.sample_rate = sample_rate;
  packet.channels = 1; // Assume mono

  // Calculate total packet size
  size_t samples_size = num_samples * sizeof(float);
  size_t total_size = sizeof(audio_batch_packet_t) + samples_size;

  // Allocate buffer for complete packet
  void *packet_data = buffer_pool_alloc(total_size);
  if (!packet_data) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate buffer for audio batch packet: %zu bytes", total_size);
    return -1;
  }

  // Copy packet header and samples
  memcpy(packet_data, &packet, sizeof(audio_batch_packet_t));
  memcpy((char *)packet_data + sizeof(audio_batch_packet_t), samples, samples_size);

  // Send packet
  int result = packet_send(sockfd, PACKET_TYPE_AUDIO_BATCH, packet_data, total_size);

  // Clean up
  buffer_pool_free(packet_data, total_size);

  return result;
}

/**
 * @brief Send size message packet
 * @param sockfd Socket file descriptor
 * @param width Terminal width
 * @param height Terminal height
 * @return 0 on success, -1 on error
 */
int av_send_size_message(socket_t sockfd, unsigned short width, unsigned short height) {
  char message[32];
  int len = safe_snprintf(message, sizeof(message), "SIZE:%u,%u", width, height);
  if (len < 0 || len >= (int)sizeof(message)) {
    SET_ERRNO(ERROR_FORMAT, "Failed to format size message");
    return -1;
  }

  return packet_send(sockfd, PACKET_TYPE_SIZE_MESSAGE, message, len);
}

/**
 * @brief Send audio message packet
 * @param sockfd Socket file descriptor
 * @param num_samples Number of audio samples
 * @return 0 on success, -1 on error
 */
int av_send_audio_message(socket_t sockfd, unsigned int num_samples) {
  char message[32];
  int len = safe_snprintf(message, sizeof(message), "AUDIO:%u", num_samples);
  if (len < 0 || len >= (int)sizeof(message)) {
    SET_ERRNO(ERROR_FORMAT, "Failed to format audio message");
    return -1;
  }

  return packet_send(sockfd, PACKET_TYPE_AUDIO_MESSAGE, message, len);
}

/**
 * @brief Send text message packet
 * @param sockfd Socket file descriptor
 * @param text Text message
 * @return 0 on success, -1 on error
 */
int av_send_text_message(socket_t sockfd, const char *text) {
  if (!text) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: text=%p", text);
    return -1;
  }

  size_t len = strlen(text);
  if (len > 1024) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Text message too long: %zu > 1024", len);
    return -1;
  }

  return packet_send(sockfd, PACKET_TYPE_TEXT_MESSAGE, text, len);
}

/**
 * @brief Receive audio message
 * @param sockfd Socket file descriptor
 * @param header Message header
 * @param samples Output buffer for samples
 * @param max_samples Maximum number of samples
 * @return Number of samples received, or -1 on error
 */
int av_receive_audio_message(socket_t sockfd, const char *header, float *samples, size_t max_samples) {
  if (!header || !samples || max_samples == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: header=%p, samples=%p, max_samples=%zu", header, samples,
              max_samples);
    return -1;
  }

  if (strncmp(header, AUDIO_MESSAGE_PREFIX, strlen(AUDIO_MESSAGE_PREFIX)) != 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid audio message header");
    return -1;
  }

  unsigned int num_samples;
  if (safe_sscanf(header + strlen(AUDIO_MESSAGE_PREFIX), "%u", &num_samples) != 1 ||
      num_samples > (unsigned int)max_samples || num_samples > AUDIO_SAMPLES_PER_PACKET) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid audio sample count: %u", num_samples);
    return -1;
  }

  size_t data_size = num_samples * sizeof(float);
  ssize_t received = recv_with_timeout(sockfd, samples, data_size, is_test_environment() ? 1 : RECV_TIMEOUT);
  if (received != (ssize_t)data_size) {
    SET_ERRNO(ERROR_NETWORK, "Failed to receive audio data: %zd/%zu bytes", received, data_size);
    return -1;
  }

  return (int)num_samples;
}

/**
 * @brief Parse size message
 * @param message Size message string
 * @param width Output: terminal width
 * @param height Output: terminal height
 * @return 0 on success, -1 on error
 */
int av_parse_size_message(const char *message, unsigned short *width, unsigned short *height) {
  if (!message || !width || !height) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: message=%p, width=%p, height=%p", message, width, height);
    return -1;
  }

  // Check if message starts with SIZE_MESSAGE_PREFIX
  if (strncmp(message, SIZE_MESSAGE_PREFIX, strlen(SIZE_MESSAGE_PREFIX)) != 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid size message format");
    return -1;
  }

  // Parse the width,height values
  unsigned int w;
  unsigned int h;
  if (safe_sscanf(message + strlen(SIZE_MESSAGE_PREFIX), "%u,%u", &w, &h) != 2) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Failed to parse size message: %s", message);
    return -1;
  }

  *width = (unsigned short)w;
  *height = (unsigned short)h;
  return 0;
}

/**
 * @brief Send audio batch packet
 * @param sockfd Socket file descriptor
 * @param samples Audio samples
 * @param num_samples Number of samples
 * @param batch_count Number of batches
 * @return 0 on success, -1 on error
 */
int send_audio_batch_packet(socket_t sockfd, const float *samples, int num_samples, int batch_count) {
  if (!samples || num_samples <= 0 || batch_count <= 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid audio batch: samples=%p, num_samples=%d, batch_count=%d", samples,
              num_samples, batch_count);
    return -1;
  }

  // Build batch header
  audio_batch_packet_t header;
  header.batch_count = htonl(batch_count);
  header.total_samples = htonl(num_samples);
  header.sample_rate = htonl(44100); // Could make this configurable
  header.channels = htonl(1);        // Mono for now

  // Calculate total payload size
  size_t data_size = num_samples * sizeof(float);
  size_t total_size = sizeof(header) + data_size;

  // Allocate buffer for header + data
  uint8_t *buffer = buffer_pool_alloc(total_size);
  if (!buffer) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate buffer for audio batch packet");
    return -1;
  }

  // Copy header and data
  memcpy(buffer, &header, sizeof(header));
  memcpy(buffer + sizeof(header), samples, data_size);

  // Send packet
  int result = send_packet(sockfd, PACKET_TYPE_AUDIO_BATCH, buffer, total_size);
  buffer_pool_free(buffer, total_size);

  return result;
}
