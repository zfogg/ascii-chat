#include <time.h>
#include "compression.h"
#include "common.h"
#include "options.h"
#include "network.h"

// Rate-limit compression debug logs to once every 5 seconds
static time_t g_last_compression_log_time = 0;

// network.c implementation:
uint32_t calculate_crc32(const char *data, size_t length) {
  return crc32(0L, (const Bytef *)data, length);
}

int send_compressed_frame(int sockfd, const char *frame_data, size_t frame_size) {
  // Calculate maximum compressed size
  uLongf compressed_size = compressBound(frame_size);
  char *compressed_data;
  SAFE_MALLOC(compressed_data, compressed_size, char *);

  if (!compressed_data) {
    return -1;
  }

  // Compress the frame
  int result = compress((Bytef *)compressed_data, &compressed_size, (const Bytef *)frame_data, frame_size);

  if (result != Z_OK) {
    free(compressed_data);
    return -1;
  }

  // Check if compression is worthwhile
  float compression_ratio = (float)compressed_size / frame_size;
  bool use_compression = compression_ratio < COMPRESSION_RATIO_THRESHOLD;

  if (use_compression) {
    // Send compressed frame
    compressed_frame_header_t header = {.magic = COMPRESSION_FRAME_MAGIC,
                                        .compressed_size = (uint32_t)compressed_size,
                                        .original_size = (uint32_t)frame_size,
                                        .width = opt_width,
                                        .height = opt_height,
                                        .checksum = calculate_crc32(frame_data, frame_size)};

    if (send_video_header_packet(sockfd, &header, sizeof(header)) < 0) {
      free(compressed_data);
      return -1;
    }

    if (send_video_packet(sockfd, compressed_data, compressed_size) < 0) {
      free(compressed_data);
      return -1;
    }

    time_t now = time(NULL);
    if (now - g_last_compression_log_time >= 5) {
      log_debug("Sent compressed frame: %zu -> %lu bytes (%.1f%%)", frame_size, compressed_size,
                compression_ratio * 100);
      g_last_compression_log_time = now;
    }
  } else {
    // Send uncompressed (add special header to indicate this)
    compressed_frame_header_t header = {.magic = COMPRESSION_FRAME_MAGIC,
                                        .compressed_size = 0, // 0 indicates uncompressed
                                        .original_size = (uint32_t)frame_size,
                                        .width = opt_width,
                                        .height = opt_height,
                                        .checksum = calculate_crc32(frame_data, frame_size)};

    if (send_video_header_packet(sockfd, &header, sizeof(header)) < 0) {
      free(compressed_data);
      return -1;
    }

    if (send_video_packet(sockfd, frame_data, frame_size) < 0) {
      free(compressed_data);
      return -1;
    }

    time_t now = time(NULL);
    if (now - g_last_compression_log_time >= 5) {
      log_debug("Sent uncompressed frame: %zu bytes (compression not beneficial)", frame_size);
      g_last_compression_log_time = now;
    }
  }

  free(compressed_data);
  return use_compression ? compressed_size : frame_size;
}