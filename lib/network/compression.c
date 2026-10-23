/**
 * @file compression.c
 * @ingroup compression
 * @brief 🗜️ Fast zstd compression/decompression utilities for network payload optimization
 */

#include <zstd.h>
#include <ascii-chat/otel.h>
#include <ascii-chat/buffer.h>
#include <string.h>
#include <ascii-chat/network/compression.h>
#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h> // For asciichat_errno system

static void compression_measure(size_t input, size_t output, bool compressing) {
  ascii_otel_metric(compressing ? ASCII_OTEL_METRIC_COMPRESS_INPUT : ASCII_OTEL_METRIC_DECOMPRESS_INPUT, (double)input);
  ascii_otel_metric(compressing ? ASCII_OTEL_METRIC_COMPRESS_OUTPUT : ASCII_OTEL_METRIC_DECOMPRESS_OUTPUT,
                    (double)output);
  if (compressing) {
    ascii_otel_metric(ASCII_OTEL_METRIC_COMPRESS_BYTES_IN, (double)input);
    ascii_otel_metric(ASCII_OTEL_METRIC_COMPRESS_BYTES_OUT, (double)output);
    if (input)
      ascii_otel_metric(ASCII_OTEL_METRIC_COMPRESS_RATIO, (double)output / (double)input);
  } else
    ascii_otel_metric(ASCII_OTEL_METRIC_DECOMPRESS_BYTES_OUT, (double)output);
}

// Compress data using zstd with configurable compression level
asciichat_error_t compress_data(const void *input, size_t input_size, void **output, size_t *output_size,
                                int compression_level) {
  ASCII_OTEL_SCOPE(ZSTD_COMPRESS);
  ASCII_OTEL_ATTR_STRING("compression.algorithm", "zstd");
  ASCII_OTEL_ATTR_STRING("compression.library.version", ZSTD_versionString());
  ASCII_OTEL_ATTR_INT("compression.level", compression_level);
  ASCII_OTEL_ATTR_INT("compression.input.size", (int64_t)input_size);
  if (!input || input_size == 0 || !output || !output_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: input=%p, input_size=%zu, output=%p, output_size=%p",
                     input, input_size, output, output_size);
  }

  // Validate compression level (1-9 for real-time streaming)
  if (compression_level < 1 || compression_level > 9) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid compression level %d: must be between 1 and 9", compression_level);
  }

  // Calculate maximum compressed size
  size_t compressed_size = ZSTD_compressBound(input_size);

  // Sanity check ZSTD_compressBound result
  // ZSTD_compressBound returns 0 for errors or very large values for huge inputs
  if (compressed_size == 0 || compressed_size > 256 * 1024 * 1024) { // Max 256MB compressed buffer
    return SET_ERRNO(ERROR_INVALID_PARAM, "ZSTD_compressBound returned unreasonable size: %zu for input %zu",
                     compressed_size, input_size);
  }

  unsigned char *compressed_data = NULL;
  compressed_data = buffer_alloc(NULL, compressed_size);

  if (!compressed_data) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate compressed data buffer");
  }

  // Compress using zstd with user-specified compression level
  size_t ret = ZSTD_compress(compressed_data, compressed_size, input, input_size, compression_level);

  if (ZSTD_isError(ret)) {
    buffer_release(compressed_data);
    return SET_ERRNO(ERROR_GENERAL, "zstd compression failed: %s", ZSTD_getErrorName(ret));
  }

  *output = compressed_data;
  *output_size = ret;
  ASCII_OTEL_ATTR_INT("compression.output.size", (int64_t)ret);
  ASCII_OTEL_ATTR_INT("compression.ratio.permille", (int64_t)(1000.0 * (double)ret / (double)input_size));
  compression_measure(input_size, ret, true);

  return ASCIICHAT_OK;
}

// Decompress data using zstd
asciichat_error_t decompress_data(const void *input, size_t input_size, void *output, size_t output_size) {
  ASCII_OTEL_SCOPE(ZSTD_DECOMPRESS);
  ASCII_OTEL_ATTR_STRING("compression.algorithm", "zstd");
  ASCII_OTEL_ATTR_STRING("compression.library.version", ZSTD_versionString());
  ASCII_OTEL_ATTR_INT("compression.input.size", (int64_t)input_size);
  ASCII_OTEL_ATTR_INT("compression.output.capacity", (int64_t)output_size);
  if (!input || input_size == 0 || !output || output_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: input=%p, input_size=%zu, output=%p, output_size=%zu",
                     input, input_size, output, output_size);
  }

  size_t ret = ZSTD_decompress(output, output_size, input, input_size);

  if (ZSTD_isError(ret)) {
    return SET_ERRNO(ERROR_GENERAL, "zstd decompression failed: %s", ZSTD_getErrorName(ret));
  }

  ASCII_OTEL_ATTR_INT("compression.output.size", (int64_t)ret);
  ASCII_OTEL_ATTR_INT("compression.ratio.permille", (int64_t)(1000.0 * (double)ret / (double)input_size));
  compression_measure(input_size, ret, false);
  return ASCIICHAT_OK;
}

// Rendered frames must fill every advertised byte; a short decode must not
// expose stale workspace bytes or uninitialized allocation tails to CRC/parser.
asciichat_error_t decompress_data_exact(const void *input, size_t input_size, void *output, size_t expected_size) {
  ASCII_OTEL_SCOPE(ZSTD_DECOMPRESS_EXACT);
  ASCII_OTEL_ATTR_STRING("compression.algorithm", "zstd");
  ASCII_OTEL_ATTR_STRING("compression.library.version", ZSTD_versionString());
  ASCII_OTEL_ATTR_INT("compression.input.size", (int64_t)input_size);
  ASCII_OTEL_ATTR_INT("compression.expected_output.size", (int64_t)expected_size);
  if (!input || !input_size || !output || !expected_size)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid exact decompression bounds");
  size_t decoded = ZSTD_decompress(output, expected_size, input, input_size);
  if (ZSTD_isError(decoded) || decoded != expected_size)
    return SET_ERRNO(ERROR_COMPRESSION, "Decompressed payload does not match expected size %zu", expected_size);
  ASCII_OTEL_ATTR_INT("compression.output.size", (int64_t)decoded);
  compression_measure(input_size, decoded, false);
  return ASCIICHAT_OK;
}

// Check if compression is worthwhile based on ratio
bool should_compress(size_t original_size, size_t compressed_size) {
  if (original_size == 0)
    return false;

  float ratio = (float)compressed_size / (float)original_size;
  return ratio < COMPRESSION_RATIO_THRESHOLD;
}

asciichat_error_t compress_workspace(compression_workspace_t *workspace, const void *input, size_t size, int level) {
  ASCII_OTEL_SCOPE(ZSTD_COMPRESS_WORKSPACE);
  ASCII_OTEL_ATTR_STRING("compression.algorithm", "zstd");
  ASCII_OTEL_ATTR_STRING("compression.library.version", ZSTD_versionString());
  ASCII_OTEL_ATTR_INT("compression.level", level);
  ASCII_OTEL_ATTR_INT("compression.input.size", (int64_t)size);
  if (!workspace || !input || !size || level < 1 || level > 9)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid compression workspace request");
  workspace->output.length = 0;
  size_t bound = ZSTD_compressBound(size);
  if (!bound || bound > 256 * 1024 * 1024)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Compression workspace exceeds maximum size");
  asciichat_error_t result = buffer_workspace_reserve(&workspace->output, NULL, bound);
  if (result != ASCIICHAT_OK)
    return result;
  if (!workspace->codec)
    workspace->codec = ZSTD_createCCtx();
  if (!workspace->codec)
    return SET_ERRNO(ERROR_MEMORY, "Cannot create compression context");
  size_t written = ZSTD_compressCCtx(workspace->codec, workspace->output.data, buffer_capacity(workspace->output.data),
                                     input, size, level);
  if (ZSTD_isError(written))
    return SET_ERRNO(ERROR_GENERAL, "Compression failed: %s", ZSTD_getErrorName(written));
  workspace->output.length = written;
  result = buffer_set_size(workspace->output.data, written);
  if (result == ASCIICHAT_OK) {
    ASCII_OTEL_ATTR_INT("compression.output.size", (int64_t)written);
    ASCII_OTEL_ATTR_INT("compression.ratio.permille", (int64_t)(1000.0 * (double)written / (double)size));
    compression_measure(size, written, true);
  }
  return result;
}

void compression_workspace_destroy(compression_workspace_t *workspace) {
  if (!workspace)
    return;
  ZSTD_freeCCtx(workspace->codec);
  workspace->codec = NULL;
  buffer_workspace_destroy(&workspace->output);
}
