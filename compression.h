#include <zlib.h>
#include <stdlib.h>
#include <stdio.h>
#include "common.h"
#include "network.h"

typedef struct {
  uint32_t magic;           // COMPRESSION_FRAME_MAGIC
  uint32_t compressed_size; // value of 0 means uncompressed
  uint32_t original_size;
  uint32_t checksum; // CRC32 of original data
} compressed_frame_header_t;

#define COMPRESSION_FRAME_MAGIC 0x41534349 // 0x41 0x53 0x43 0x49 (“ASCI” in ASCII)
#define COMPRESSION_RATIO_THRESHOLD 0.8f   // Only send compressed if <80% original size

int send_compressed_frame(int sockfd, const char *frame_data, size_t frame_size);
ssize_t recv_compressed_frame(int sockfd, char **buf, size_t *output_size);