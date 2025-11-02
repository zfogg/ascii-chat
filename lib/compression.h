#pragma once

/**
 * @file compression.h
 * @ingroup network
 * @brief Network Packet Compression Utilities
 *
 * This header provides compression and decompression utilities for network
 * packets in ASCII-Chat. The system uses zlib deflate compression to reduce
 * bandwidth usage for large packets like video frames.
 *
 * CORE FEATURES:
 * ==============
 * - Automatic compression ratio checking (only use if beneficial)
 * - Minimum size threshold to avoid compressing small packets
 * - zlib deflate compression algorithm
 * - Memory-efficient compression/decompression
 * - Pure utility functions (no state management)
 *
 * COMPRESSION STRATEGY:
 * =====================
 * Compression is only applied when:
 * - Packet size exceeds minimum threshold (1KB)
 * - Compression achieves at least 20% size reduction (<80% of original)
 *
 * This prevents:
 * - Overhead of compressing already-compressed data
 * - CPU waste on small packets that don't benefit
 * - Compression expansion (when compressed data is larger)
 *
 * ALGORITHM:
 * ==========
 * - Uses zlib deflate for compression
 * - Compatible with standard zlib/gzip decompression
 * - Provides good compression ratio for text/ASCII data
 * - Reasonable CPU overhead for real-time streaming
 *
 * @note Compression is optional and only used when beneficial.
 * @note Frame sending functions have been moved to network.h/network.c.
 * @note The compression ratio threshold ensures compression is only
 *       used when it provides meaningful bandwidth savings.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date August 2025
 */

#include <stdlib.h>
#include <stdbool.h>

/**
 * @name Compression Settings
 * @{
 */

/** @brief Compression ratio threshold - only use if <80% original size */
#define COMPRESSION_RATIO_THRESHOLD 0.8f

/** @brief Minimum packet size to attempt compression (1KB) */
#define COMPRESSION_MIN_SIZE 1024

/** @} */

/**
 * @brief Compress data using zlib deflate
 * @param input Input data to compress (must not be NULL)
 * @param input_size Size of input data in bytes
 * @param output Output buffer pointer (allocated by function, must not be NULL)
 * @param output_size Size of compressed data in bytes (output parameter, must not be NULL)
 * @return 0 on success, non-zero on error
 *
 * Compresses input data using zlib deflate algorithm. The output buffer is
 * automatically allocated by the function and must be freed by the caller
 * using free() or the appropriate memory management function.
 *
 * @note The output buffer is allocated using malloc(). Caller must free it
 *       when done using the compressed data.
 *
 * @note Compression may fail if input data is already compressed or if
 *       compression would expand the data significantly.
 *
 * @note For best performance, use should_compress() first to determine
 *       if compression is beneficial before calling this function.
 *
 * @warning Caller must free the output buffer to avoid memory leaks.
 */
int compress_data(const void *input, size_t input_size, void **output, size_t *output_size);

/**
 * @brief Decompress data using zlib inflate
 * @param input Compressed input data (must not be NULL)
 * @param input_size Size of compressed data in bytes
 * @param output Pre-allocated output buffer (must not be NULL)
 * @param output_size Size of output buffer in bytes (must be >= decompressed size)
 * @return 0 on success, non-zero on error
 *
 * Decompresses zlib deflate-compressed data into a pre-allocated output buffer.
 * The output buffer must be large enough to hold the decompressed data.
 *
 * @note The output buffer size must be known in advance (typically from packet
 *       header or protocol specification). This function does not dynamically
 *       allocate the output buffer.
 *
 * @note This function uses zlib inflate algorithm, compatible with standard
 *       zlib/gzip compression.
 *
 * @warning Output buffer must be large enough for decompressed data or buffer
 *          overflow will occur. Ensure output_size is correct before calling.
 */
int decompress_data(const void *input, size_t input_size, void *output, size_t output_size);

/**
 * @brief Determine if compression should be used for given data sizes
 * @param original_size Original (uncompressed) data size in bytes
 * @param compressed_size Compressed data size in bytes
 * @return true if compression should be used, false otherwise
 *
 * Determines whether compression is beneficial based on size thresholds and
 * compression ratio. Returns true if:
 * - Original size >= COMPRESSION_MIN_SIZE (1KB minimum)
 * - Compressed size < COMPRESSION_RATIO_THRESHOLD * original_size (<80% of original)
 *
 * This prevents:
 * - Compressing small packets that don't benefit
 * - Using compression when it doesn't reduce size meaningfully
 * - Wasting CPU on compression that expands data
 *
 * @note Call this function before compress_data() to determine if compression
 *       is worth the CPU overhead.
 *
 * @note The function compares sizes only. Actual compression must be performed
 *       to get compressed_size for comparison.
 */
bool should_compress(size_t original_size, size_t compressed_size);

// Note: Frame sending functions have been moved to network.h/network.c
