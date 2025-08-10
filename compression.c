#include <time.h>
#include <stdint.h>
#include <string.h>
#include <zlib.h>
#include "compression.h"
#include "common.h"
#include "options.h"
#include "network.h"

// Rate-limit compression debug logs to once every 5 seconds
static time_t g_last_compression_log_time = 0;

// Using asciichat_crc32 from network.c for all CRC calculations

int send_compressed_frame(int sockfd, const char *frame_data, size_t frame_size) {
  // Validate frame size
  if (frame_size == 0 || !frame_data) {
    log_error("Invalid frame data: frame_data=%p, frame_size=%zu", frame_data, frame_size);
    return -1;
  }

  // Check for suspicious frame size
  if (frame_size > 10 * 1024 * 1024) { // 10MB seems unreasonable for ASCII art
    log_error("Suspicious frame_size=%zu, might be corrupted", frame_size);
    return -1;
  }

  // Calculate maximum compressed size
  uLongf compressed_size = compressBound(frame_size);

  char *compressed_data;
  SAFE_MALLOC(compressed_data, compressed_size, char *);

  if (!compressed_data) {
    return -1;
  }

  // Try using deflate instead of compress2 for more control
  z_stream strm;
  memset(&strm, 0, sizeof(strm));

  int ret = deflateInit(&strm, Z_DEFAULT_COMPRESSION);
  if (ret != Z_OK) {
    log_error("deflateInit failed: %d", ret);
    free(compressed_data);
    return -1;
  }

  strm.avail_in = frame_size;
  strm.next_in = (Bytef *)frame_data;
  strm.avail_out = compressed_size;
  strm.next_out = (Bytef *)compressed_data;

  ret = deflate(&strm, Z_FINISH);

  if (ret != Z_STREAM_END) {
    log_error("deflate failed: %d", ret);
    deflateEnd(&strm);
    free(compressed_data);
    return -1;
  }

  compressed_size = strm.total_out;
  deflateEnd(&strm);

  int result = Z_OK; // For compatibility with the rest of the code

  if (result != Z_OK) {
    free(compressed_data);
    return -1;
  }

  // Check if compression is worthwhile
  float compression_ratio = (float)compressed_size / frame_size;
  bool use_compression = compression_ratio < COMPRESSION_RATIO_THRESHOLD;

  if (use_compression) {
    // Send compressed frame
    uint32_t checksum = asciichat_crc32(frame_data, frame_size);

    compressed_frame_header_t header = {.magic = htonl(COMPRESSION_FRAME_MAGIC),
                                        .compressed_size = htonl((uint32_t)compressed_size),
                                        .original_size = htonl((uint32_t)frame_size),
                                        .width = htonl(opt_width),
                                        .height = htonl(opt_height),
                                        .checksum = htonl(checksum)};

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
    compressed_frame_header_t header = {.magic = htonl(COMPRESSION_FRAME_MAGIC),
                                        .compressed_size = htonl(0), // 0 indicates uncompressed
                                        .original_size = htonl((uint32_t)frame_size),
                                        .width = htonl(opt_width),
                                        .height = htonl(opt_height),
                                        .checksum = htonl(asciichat_crc32(frame_data, frame_size))};

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