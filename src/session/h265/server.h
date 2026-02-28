#pragma once

/**
 * @file session/h265/server.h
 * @brief Server-side H.265 decoding pipeline for multi-client streams
 *
 * The server maintains a separate H.265 decoder for each connected client.
 * When a client sends an H.265-encoded frame, the server:
 * 1. Identifies the decoder for that client
 * 2. Decodes the H.265 frame to ASCII art
 * 3. Converts ASCII art to RGBA pixels for compositing
 * 4. Adds the RGBA frame to the compositing pipeline
 */

#include <ascii-chat/asciichat_errno.h>
#include <stdint.h>
#include <stddef.h>

typedef struct h265_server_context h265_server_context_t;
typedef struct h265_server_client h265_server_client_t;

/**
 * Create a server-side H.265 decoding context
 * Manages decoders for all connected clients
 *
 * @return Context handle, or NULL on error
 */
h265_server_context_t *h265_server_context_create(void);

/**
 * Destroy the server context and all client decoders
 *
 * @param ctx Context to destroy (may be NULL)
 */
void h265_server_context_destroy(h265_server_context_t *ctx);

/**
 * Get or create a client decoder within the server context
 * Each client gets its own H.265 decoder instance
 *
 * @param ctx Server context
 * @param client_id Unique client identifier
 * @return Client decoder handle, or NULL on error
 */
h265_server_client_t *h265_server_get_client_decoder(
    h265_server_context_t *ctx,
    uint32_t client_id
);

/**
 * Decode an H.265 frame from a client and convert to RGBA
 *
 * @param client Client decoder handle
 * @param h265_packet H.265 encoded packet data (with header)
 * @param packet_size Size of packet
 * @param output_rgba Output buffer for RGBA pixel data
 * @param output_width Output: frame width in pixels
 * @param output_height Output: frame height in pixels
 * @param output_size Input: buffer size, Output: actual RGBA size (width*height*4)
 * @return Error code (ASCIICHAT_OK on success)
 *
 * RGBA conversion: Each ASCII character (0-255) becomes a grayscale pixel
 * where pixel = (ascii_val, ascii_val, ascii_val, 255)
 */
asciichat_error_t h265_server_decode_and_convert(
    h265_server_client_t *client,
    const uint8_t *h265_packet,
    size_t packet_size,
    uint8_t *output_rgba,
    uint16_t *output_width,
    uint16_t *output_height,
    size_t *output_size
);

/**
 * Remove a client decoder (when client disconnects)
 *
 * @param ctx Server context
 * @param client_id Client identifier
 */
void h265_server_remove_client(h265_server_context_t *ctx, uint32_t client_id);

/**
 * Get statistics for a client's decoder
 *
 * @param client Client decoder
 * @param total_frames Output: total frames decoded
 * @param keyframes Output: total keyframes decoded
 * @param last_width Output: width of last decoded frame
 * @param last_height Output: height of last decoded frame
 */
void h265_server_client_get_stats(
    h265_server_client_t *client,
    uint64_t *total_frames,
    uint64_t *keyframes,
    uint16_t *last_width,
    uint16_t *last_height
);
