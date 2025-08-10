#ifndef COMPRESSION_H
#define COMPRESSION_H

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

// Compression settings
#define COMPRESSION_RATIO_THRESHOLD 0.8f // Only send compressed if <80% original size

// Function declarations for unified packet system
int send_ascii_frame_packet(int sockfd, const char *frame_data, size_t frame_size, int width, int height);
int send_image_frame_packet(int sockfd, const void *pixel_data, size_t pixel_size, int width, int height,
                            uint32_t pixel_format);

// Legacy functions (deprecated - use unified packet functions above)
int send_compressed_frame(int sockfd, const char *frame_data, size_t frame_size);

// Note: compressed_frame_header_t has been removed in favor of
// ascii_frame_packet_t and image_frame_packet_t in network.h

#endif // COMPRESSION_H