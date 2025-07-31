// Example: Frame compression using zlib
// Add to network.c (requires: sudo apt-get install zlib1g-dev)

#include "compression.h"
#include <time.h>

// Rate-limit compression debug logs to once every 5 seconds
static time_t g_last_compression_log_time = 0;

// network.c implementation:
uint32_t calculate_crc32(const char *data, size_t length) {
  return crc32(0L, (const Bytef *)data, length);
}

int send_compressed_frame(int sockfd, const char *frame_data, size_t frame_size) {
  // Calculate maximum compressed size
  uLongf compressed_size = compressBound(frame_size);
  char *compressed_data = (char *)malloc(compressed_size);

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
                                        .checksum = calculate_crc32(frame_data, frame_size)};

    // Send header
    if (send_with_timeout(sockfd, &header, sizeof(header), SEND_TIMEOUT) != sizeof(header)) {
      free(compressed_data);
      return -1;
    }

    // Send compressed data
    if (send_with_timeout(sockfd, compressed_data, compressed_size, SEND_TIMEOUT) != (ssize_t)compressed_size) {
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
                                        .checksum = calculate_crc32(frame_data, frame_size)};

    if (send_with_timeout(sockfd, &header, sizeof(header), SEND_TIMEOUT) != sizeof(header)) {
      free(compressed_data);
      return -1;
    }

    if (send_with_timeout(sockfd, frame_data, frame_size, SEND_TIMEOUT) != (ssize_t)frame_size) {
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

static ssize_t recv_all_with_timeout(int sockfd, void *buf, size_t len, int timeout_seconds) {
  size_t total_received = 0;
  char *data = (char *)buf;
  while (total_received < len) {
    ssize_t recvd = recv_with_timeout(sockfd, data + total_received, len - total_received, timeout_seconds);
    if (recvd <= 0) {
      return -1; // Timeout or error
    }
    total_received += recvd;
  }
  return (ssize_t)total_received;
}

ssize_t recv_compressed_frame(int sockfd, char **buf, size_t *output_size) {
  // Receive header (exactly sizeof(header) bytes)
  compressed_frame_header_t header;
  ssize_t header_size;
  if (0 < (header_size = recv_all_with_timeout(sockfd, &header, sizeof(header), RECV_TIMEOUT) != sizeof(header))) {
    return header_size;
  }

  // Validate magic number
  if (header.magic != COMPRESSION_FRAME_MAGIC) {
    log_error("Invalid frame magic: 0x%08x", header.magic);
    return -1;
  }

  char *frame_data;
  SAFE_MALLOC(frame_data, header.original_size + 1, char *); // +1 for null terminator

  ssize_t frame_size;

  if (header.compressed_size == 0) {
    // Uncompressed frame
    frame_size = recv_all_with_timeout(sockfd, frame_data, header.original_size, RECV_TIMEOUT);
    if (frame_size != (ssize_t)header.original_size) {
      log_error("Invalid uncompressed frame size: %zu != %zu", frame_size, header.original_size);
      free(frame_data);
      return -1;
    }
  } else {
    // Compressed frame
    char *compressed_data;
    SAFE_MALLOC(compressed_data, header.compressed_size, char *);

    frame_size = recv_all_with_timeout(sockfd, compressed_data, header.compressed_size, RECV_TIMEOUT);
    if (frame_size != (ssize_t)header.compressed_size) {
      log_error("Invalid compressed frame size: %zu != %zu", frame_size, header.compressed_size);
      free(compressed_data);
      free(frame_data);
      return -1;
    }

    // Decompress
    uLongf decompressed_size = header.original_size;
    int result =
        uncompress((Bytef *)frame_data, &decompressed_size, (const Bytef *)compressed_data, header.compressed_size);

    free(compressed_data);

    if (result != Z_OK || decompressed_size != header.original_size) {
      log_error("Decompression failed: %d", result);
      free(frame_data);
      return -1;
    }
  }

  // Verify checksum
  uint32_t received_checksum = calculate_crc32(frame_data, header.original_size);
  if (received_checksum != header.checksum) {
    log_error("Compressed frame checksum mismatch: expected 0x%08x, got 0x%08x", header.checksum, received_checksum);
    free(frame_data);
    return -1;
  }

  *buf = frame_data;

  frame_data[header.original_size] = '\0'; // Null terminate
  *output_size = header.original_size;
  return frame_size;
}
