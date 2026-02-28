#pragma once

/**
 * @file video/x265/decoder.h
 * @brief HEVC/H.265 decoder for ASCII art frames using libde265
 *
 * Decodes x265-encoded ASCII art frames back into terminal grid data.
 * Automatically reconfigures when frame size changes.
 *
 * FRAME SIZE CHANGES:
 * The decoder reads width/height from each packet and reconfigures as needed.
 * No explicit API call required - handled transparently during decode.
 *
 * PACKET FORMAT:
 *   [flags: u8][width: u16][height: u16][encoded_data: x265]
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <ascii-chat/asciichat_errno.h>

typedef struct x265_decoder x265_decoder_t;

/**
 * Decoder flags (from packet)
 */
#define X265_DECODER_FLAG_KEYFRAME  0x01
#define X265_DECODER_FLAG_SIZE_CHANGE 0x02

/**
 * Create a new x265 decoder for ASCII frames
 *
 * @return Decoder handle, or NULL on error
 */
x265_decoder_t *x265_decoder_create(void);

/**
 * Destroy an x265 decoder
 *
 * @param decoder Decoder to destroy (may be NULL)
 */
void x265_decoder_destroy(x265_decoder_t *decoder);

/**
 * Decode an x265-encoded ASCII frame
 *
 * The decoder reads width/height from the packet and automatically
 * reconfigures when frame dimensions change.
 *
 * @param decoder Decoder handle
 * @param encoded_packet Encoded packet (flags+width+height+x265_data)
 * @param packet_size Size of encoded packet
 * @param output_width Output: decoded frame width
 * @param output_height Output: decoded frame height
 * @param output_buf Buffer for decoded ASCII data (should be at least width×height)
 * @param output_size Input: buffer size, Output: actual decoded size
 * @return Error code (ASCIICHAT_OK on success)
 *
 * INPUT PACKET FORMAT:
 *   [flags: u8][width: u16][height: u16][x265_data...]
 *
 * Call this as:
 *   uint16_t width, height;
 *   uint8_t ascii_grid[256*64];
 *   size_t decoded_size = sizeof(ascii_grid);
 *   x265_decoder_decode(decoder, packet, packet_size, &width, &height,
 *                       ascii_grid, &decoded_size);
 */
asciichat_error_t x265_decoder_decode(
    x265_decoder_t *decoder,
    const uint8_t *encoded_packet,
    size_t packet_size,
    uint16_t *output_width,
    uint16_t *output_height,
    uint8_t *output_buf,
    size_t *output_size
);

/**
 * Get decoder statistics
 *
 * @param decoder Decoder handle
 * @param total_frames Output: total frames decoded
 * @param keyframes Output: total keyframes decoded
 * @param last_width Output: width of last decoded frame
 * @param last_height Output: height of last decoded frame
 */
void x265_decoder_get_stats(
    x265_decoder_t *decoder,
    uint64_t *total_frames,
    uint64_t *keyframes,
    uint16_t *last_width,
    uint16_t *last_height
);
