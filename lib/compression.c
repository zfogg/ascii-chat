#include <time.h>
#include <stdint.h>
#include <string.h>
#include <zlib.h>
#include "platform/abstraction.h"
#include "compression.h"
#include "common.h"
#include "options.h"
#include "network.h"

// Rate-limit compression debug logs to once every 5 seconds
static time_t g_last_compression_log_time = 0;

// Send ASCII frame using the new unified packet structure
int send_ascii_frame_packet(int sockfd, const char *frame_data, size_t frame_size, int width, int height) {
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

  // Try compression using deflate
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

  // Check if compression is worthwhile
  float compression_ratio = (float)compressed_size / frame_size;
  bool use_compression = compression_ratio < COMPRESSION_RATIO_THRESHOLD;

  // Build the ASCII frame packet header
  ascii_frame_packet_t frame_header;
  frame_header.width = htonl(width);
  frame_header.height = htonl(height);
  frame_header.original_size = htonl((uint32_t)frame_size);
  frame_header.checksum = htonl(asciichat_crc32(frame_data, frame_size));
  frame_header.flags = 0;

  // Prepare packet data
  void *packet_data;
  size_t packet_size;

  if (use_compression) {
    frame_header.compressed_size = htonl((uint32_t)compressed_size);
    frame_header.flags |= htonl(FRAME_FLAG_IS_COMPRESSED);

    // Allocate buffer for header + compressed data
    packet_size = sizeof(ascii_frame_packet_t) + compressed_size;
    SAFE_MALLOC(packet_data, packet_size, void *);
    if (!packet_data) {
      free(compressed_data);
      return -1;
    }

    // Copy header and compressed data
    memcpy(packet_data, &frame_header, sizeof(ascii_frame_packet_t));
    memcpy((char *)packet_data + sizeof(ascii_frame_packet_t), compressed_data, compressed_size);

    time_t now = time(NULL);
    if (now - g_last_compression_log_time >= 5) {
      log_debug("Sending compressed ASCII frame: %zu -> %lu bytes (%.1f%%)", frame_size, compressed_size,
                compression_ratio * 100);
      g_last_compression_log_time = now;
    }
  } else {
    frame_header.compressed_size = htonl(0);

    // Allocate buffer for header + uncompressed data
    packet_size = sizeof(ascii_frame_packet_t) + frame_size;
    SAFE_MALLOC(packet_data, packet_size, void *);
    if (!packet_data) {
      free(compressed_data);
      return -1;
    }

    // Copy header and uncompressed data
    memcpy(packet_data, &frame_header, sizeof(ascii_frame_packet_t));
    memcpy((char *)packet_data + sizeof(ascii_frame_packet_t), frame_data, frame_size);

    time_t now = time(NULL);
    if (now - g_last_compression_log_time >= 5) {
      log_debug("Sending uncompressed ASCII frame: %zu bytes", frame_size);
      g_last_compression_log_time = now;
    }
  }

  // Send as a single unified packet
  int result = send_packet(sockfd, PACKET_TYPE_ASCII_FRAME, packet_data, packet_size);

  free(packet_data);
  free(compressed_data);

  return result < 0 ? -1 : (use_compression ? (long)compressed_size : (long)frame_size);
}

// Send image frame using the new unified packet structure
int send_image_frame_packet(int sockfd, const void *pixel_data, size_t pixel_size, int width, int height,
                            uint32_t pixel_format) {
  // Validate input
  if (!pixel_data || pixel_size == 0) {
    log_error("Invalid pixel data");
    return -1;
  }

  // Build the image frame packet header
  image_frame_packet_t frame_header;
  frame_header.width = htonl(width);
  frame_header.height = htonl(height);
  frame_header.pixel_format = htonl(pixel_format);
  frame_header.compressed_size = htonl(0); // No compression for now
  frame_header.checksum = htonl(asciichat_crc32(pixel_data, pixel_size));
  frame_header.timestamp = htonl((uint32_t)time(NULL));

  // Allocate buffer for header + pixel data
  size_t packet_size = sizeof(image_frame_packet_t) + pixel_size;
  void *packet_data;
  SAFE_MALLOC(packet_data, packet_size, void *);
  if (!packet_data) {
    return -1;
  }

  // Copy header and pixel data
  memcpy(packet_data, &frame_header, sizeof(image_frame_packet_t));
  memcpy((char *)packet_data + sizeof(image_frame_packet_t), pixel_data, pixel_size);

  // Send as a single unified packet
  int result = send_packet(sockfd, PACKET_TYPE_IMAGE_FRAME, packet_data, packet_size);

  free(packet_data);
  return result;
}

// Legacy function - now uses the new unified packet system
int send_compressed_frame(int sockfd, const char *frame_data, size_t frame_size) {
  // Use the new unified packet function with global width/height
  return send_ascii_frame_packet(sockfd, frame_data, frame_size, opt_width, opt_height);
}