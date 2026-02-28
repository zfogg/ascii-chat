#pragma once

/**
 * @file video/x265/encoder.h
 * @brief HEVC/H.265 encoder for ASCII art frames using libx265
 *
 * Encodes terminal ASCII grid data using x265 for efficient compression.
 * Handles variable-sized frames by storing dimensions in the encoded packet.
 *
 * FRAME SIZE CHANGES:
 * When terminal resize occurs (width/height change), the encoder must be
 * reconfigured. This is handled transparently - the encoder detects size
 * changes and reinitializes as needed.
 *
 * PACKET FORMAT:
 *   [flags: u8][width: u16][height: u16][encoded_data: x265]
 *   - flags: Encoding flags (keyframe, etc.)
 *   - width: Frame width in characters
 *   - height: Frame height in characters
 *   - encoded_data: x265-encoded frame data
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <ascii-chat/asciichat_errno.h>

typedef struct x265_encoder x265_encoder_t;

/**
 * Encoder flags
 */
#define X265_ENCODER_FLAG_KEYFRAME  0x01
#define X265_ENCODER_FLAG_SIZE_CHANGE 0x02

/**
 * Create a new x265 encoder for ASCII frames
 *
 * @param initial_width Initial frame width (characters)
 * @param initial_height Initial frame height (characters)
 * @return Encoder handle, or NULL on error
 */
x265_encoder_t *x265_encoder_create(uint16_t initial_width, uint16_t initial_height);

/**
 * Destroy an x265 encoder
 *
 * @param encoder Encoder to destroy (may be NULL)
 */
void x265_encoder_destroy(x265_encoder_t *encoder);

/**
 * Encode an ASCII art frame
 *
 * The encoder automatically detects size changes and reinitializes when needed.
 * The output packet includes frame dimensions so decoder can reconfigure.
 *
 * @param encoder Encoder handle
 * @param width Current frame width (characters)
 * @param height Current frame height (characters)
 * @param ascii_data Raw ASCII character grid (width × height bytes)
 * @param output_buf Buffer for encoded output
 * @param output_size Input: buffer size, Output: actual encoded size
 * @return Error code (ASCIICHAT_OK on success)
 *
 * OUTPUT PACKET FORMAT:
 *   [flags: u8][width: u16][height: u16][x265_data...]
 */
asciichat_error_t x265_encoder_encode(
    x265_encoder_t *encoder,
    uint16_t width,
    uint16_t height,
    const uint8_t *ascii_data,
    uint8_t *output_buf,
    size_t *output_size
);

/**
 * Request a keyframe on the next encode
 *
 * Forces the encoder to produce an I-frame (keyframe) on the next call to
 * x265_encoder_encode(). Useful for error recovery or stream synchronization.
 *
 * @param encoder Encoder handle
 */
void x265_encoder_request_keyframe(x265_encoder_t *encoder);

/**
 * Get encoder statistics
 *
 * @param encoder Encoder handle
 * @param total_frames Output: total frames encoded
 * @param keyframes Output: total keyframes encoded
 * @param avg_bitrate Output: average bitrate in bits per second
 */
void x265_encoder_get_stats(
    x265_encoder_t *encoder,
    uint64_t *total_frames,
    uint64_t *keyframes,
    uint32_t *avg_bitrate
);
