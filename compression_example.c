// Example: Frame compression using zlib
// Add to network.c (requires: sudo apt-get install zlib1g-dev)

#include <zlib.h>
#include <stdlib.h>
#include <stdio.h>
#include "common.h"
#include "network.h"

// network.h additions:
typedef struct {
    uint32_t magic;           // 0x41534349 ("ASCI")
    uint32_t compressed_size;
    uint32_t original_size;
    uint32_t checksum;        // CRC32 of original data
} compressed_frame_header_t;

#define FRAME_MAGIC 0x41534349
#define COMPRESSION_RATIO_THRESHOLD 0.8f  // Only send compressed if <80% original size

int send_compressed_frame(int sockfd, const char* frame_data, size_t frame_size);
char* recv_compressed_frame(int sockfd, size_t* output_size);

// network.c implementation:
uint32_t calculate_crc32(const char* data, size_t length) {
    return crc32(0L, (const Bytef*)data, length);
}

int send_compressed_frame(int sockfd, const char* frame_data, size_t frame_size) {
    // Calculate maximum compressed size
    uLongf compressed_size = compressBound(frame_size);
    char* compressed_data = (char*)malloc(compressed_size);

    if (!compressed_data) {
        return -1;
    }

    // Compress the frame
    int result = compress((Bytef*)compressed_data, &compressed_size,
                         (const Bytef*)frame_data, frame_size);

    if (result != Z_OK) {
        free(compressed_data);
        return -1;
    }

    // Check if compression is worthwhile
    float compression_ratio = (float)compressed_size / frame_size;
    bool use_compression = compression_ratio < COMPRESSION_RATIO_THRESHOLD;

    if (use_compression) {
        // Send compressed frame
        compressed_frame_header_t header = {
            .magic = FRAME_MAGIC,
            .compressed_size = (uint32_t)compressed_size,
            .original_size = (uint32_t)frame_size,
            .checksum = calculate_crc32(frame_data, frame_size)
        };

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

        log_debug("Sent compressed frame: %zu -> %lu bytes (%.1f%%)",
                 frame_size, compressed_size, compression_ratio * 100);
    } else {
        // Send uncompressed (add special header to indicate this)
        compressed_frame_header_t header = {
            .magic = FRAME_MAGIC,
            .compressed_size = 0,  // 0 indicates uncompressed
            .original_size = (uint32_t)frame_size,
            .checksum = calculate_crc32(frame_data, frame_size)
        };

        if (send_with_timeout(sockfd, &header, sizeof(header), SEND_TIMEOUT) != sizeof(header)) {
            free(compressed_data);
            return -1;
        }

        if (send_with_timeout(sockfd, frame_data, frame_size, SEND_TIMEOUT) != (ssize_t)frame_size) {
            free(compressed_data);
            return -1;
        }

        log_debug("Sent uncompressed frame: %zu bytes (compression not beneficial)", frame_size);
    }

    free(compressed_data);
    return use_compression ? compressed_size : frame_size;
}

char* recv_compressed_frame(int sockfd, size_t* output_size) {
    // Receive header
    compressed_frame_header_t header;
    if (recv_with_timeout(sockfd, &header, sizeof(header), RECV_TIMEOUT) != sizeof(header)) {
        return NULL;
    }

    // Validate magic number
    if (header.magic != FRAME_MAGIC) {
        log_error("Invalid frame magic: 0x%08x", header.magic);
        return NULL;
    }

    char* frame_data = malloc(header.original_size + 1); // +1 for null terminator
    if (!frame_data) {
        return NULL;
    }

    if (header.compressed_size == 0) {
        // Uncompressed frame
        if (recv_with_timeout(sockfd, frame_data, header.original_size, RECV_TIMEOUT) != (ssize_t)header.original_size) {
            free(frame_data);
            return NULL;
        }
    } else {
        // Compressed frame
        char* compressed_data = malloc(header.compressed_size);
        if (!compressed_data) {
            free(frame_data);
            return NULL;
        }

        if (recv_with_timeout(sockfd, compressed_data, header.compressed_size, RECV_TIMEOUT) != (ssize_t)header.compressed_size) {
            free(compressed_data);
            free(frame_data);
            return NULL;
        }

        // Decompress
        uLongf decompressed_size = header.original_size;
        int result = uncompress((Bytef*)frame_data, &decompressed_size,
                               (const Bytef*)compressed_data, header.compressed_size);

        free(compressed_data);

        if (result != Z_OK || decompressed_size != header.original_size) {
            log_error("Decompression failed: %d", result);
            free(frame_data);
            return NULL;
        }
    }

    // Verify checksum
    uint32_t received_checksum = calculate_crc32(frame_data, header.original_size);
    if (received_checksum != header.checksum) {
        log_error("Frame checksum mismatch: expected 0x%08x, got 0x%08x",
                 header.checksum, received_checksum);
        free(frame_data);
        return NULL;
    }

    frame_data[header.original_size] = '\0'; // Null terminate
    *output_size = header.original_size;
    return frame_data;
}

#if defined(ENABLE_COMPRESSION)
// Usage in server.c:
// Replace: send_with_timeout(client->socket, frame_buffer, frame_len, SEND_TIMEOUT);
// With:    send_compressed_frame(client->socket, frame_buffer, frame_len);

// Usage in client.c:
// Replace the recv loop with:
while (!g_should_exit && !connection_broken) {
    size_t frame_size;
    char* frame = recv_compressed_frame(sockfd, &frame_size);

    if (!frame) {
        log_warn("Failed to receive frame: %s", network_error_string(errno));
        connection_broken = true;
        break;
    }

    if (strcmp(frame, "Webcam capture failed\n") == 0) {
        log_error("Server reported webcam failure");
        free(frame);
        connection_broken = true;
        break;
    }

    ascii_write(frame);
    free(frame);
}
#endif
