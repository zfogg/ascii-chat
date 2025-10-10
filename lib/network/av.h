#pragma once

/**
 * @file network/av.h
 * @brief Audio/Video/ASCII packet implementation
 *
 * This header provides audio, video, and ASCII frame packet functionality
 * including compression, message formatting, and specialized packet types.
 */

#include "platform/socket.h"
#include <stdint.h>
#include <stddef.h>

/* Size communication protocol */
#define SIZE_MESSAGE_PREFIX "SIZE:"
#define SIZE_MESSAGE_FORMAT "SIZE:%u,%u\n"
#define SIZE_MESSAGE_MAX_LEN 32

/* Audio communication protocol */
#define AUDIO_MESSAGE_PREFIX "AUDIO:"
#define AUDIO_MESSAGE_FORMAT "AUDIO:%u\n"
#define AUDIO_MESSAGE_MAX_LEN 32

/**
 * @brief Send ASCII frame packet
 * @param sockfd Socket file descriptor
 * @param frame_data ASCII frame data
 * @param frame_size Size of frame data
 * @return 0 on success, -1 on error
 */
int av_send_ascii_frame(socket_t sockfd, const char *frame_data, size_t frame_size);

/**
 * @brief Send image frame packet
 * @param sockfd Socket file descriptor
 * @param image_data Image pixel data
 * @param width Image width
 * @param height Image height
 * @param format Pixel format
 * @return 0 on success, -1 on error
 */
int av_send_image_frame(socket_t sockfd, const void *image_data, uint16_t width, uint16_t height, uint8_t format);

/**
 * @brief Send audio packet
 * @param sockfd Socket file descriptor
 * @param samples Audio samples
 * @param num_samples Number of samples
 * @return 0 on success, -1 on error
 */
int av_send_audio(socket_t sockfd, const float *samples, int num_samples);

/**
 * @brief Send audio batch packet
 * @param sockfd Socket file descriptor
 * @param samples Audio samples
 * @param num_samples Number of samples
 * @param batch_count Number of batches
 * @return 0 on success, -1 on error
 */
int send_audio_batch_packet(socket_t sockfd, const float *samples, int num_samples, int batch_count);

/**
 * @brief Send audio batch packet
 * @param sockfd Socket file descriptor
 * @param samples Audio samples
 * @param num_samples Number of samples
 * @param sample_rate Sample rate
 * @return 0 on success, -1 on error
 */
int av_send_audio_batch(socket_t sockfd, const float *samples, int num_samples, int sample_rate);

/**
 * @brief Send size message packet
 * @param sockfd Socket file descriptor
 * @param width Terminal width
 * @param height Terminal height
 * @return 0 on success, -1 on error
 */
int av_send_size_message(socket_t sockfd, unsigned short width, unsigned short height);

/**
 * @brief Send audio message packet
 * @param sockfd Socket file descriptor
 * @param num_samples Number of audio samples
 * @return 0 on success, -1 on error
 */
int av_send_audio_message(socket_t sockfd, unsigned int num_samples);

/**
 * @brief Send text message packet
 * @param sockfd Socket file descriptor
 * @param text Text message
 * @return 0 on success, -1 on error
 */
int av_send_text_message(socket_t sockfd, const char *text);

/**
 * @brief Receive audio message
 * @param sockfd Socket file descriptor
 * @param header Message header
 * @param samples Output buffer for samples
 * @param max_samples Maximum number of samples
 * @return Number of samples received, or -1 on error
 */
int av_receive_audio_message(socket_t sockfd, const char *header, float *samples, size_t max_samples);

/**
 * @brief Parse size message
 * @param message Size message string
 * @param width Output: terminal width
 * @param height Output: terminal height
 * @return 0 on success, -1 on error
 */
int av_parse_size_message(const char *message, unsigned short *width, unsigned short *height);
