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