/**
 * @file client_state.h
 * @brief Client state initialization utilities
 *
 * This module consolidates repeated client state initialization patterns
 * from the server protocol handlers. It provides helper functions to:
 * - Enable/disable video and audio streams
 * - Initialize capability negotiation
 * - Set stream state flags
 *
 * Instead of scattered field assignments like:
 * @code
 *   client->is_sending_video = true;
 *   client->is_sending_audio = (type & STREAM_TYPE_AUDIO) != 0;
 *   client->terminal_caps.capabilities = ntohl(caps->capabilities);
 * @endcode
 *
 * Use consolidated helpers:
 * @code
 *   client_state_enable_video(client);
 *   client_state_enable_audio(client);
 *   client_state_init_capabilities(client, ntohl(caps->capabilities), ...);
 * @endcode
 *
 * @ingroup network
 */

#ifndef LIB_NETWORK_CLIENT_STATE_H
#define LIB_NETWORK_CLIENT_STATE_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Forward declaration of client info structure
 *
 * The actual definition comes from the server's client management code.
 * This header only needs the type name for function parameters.
 */
typedef struct client_info client_info_t;

/**
 * @brief Enable video streaming for a client
 *
 * Sets the client's video streaming flag to indicate it's actively
 * sending video frames to the server.
 *
 * @param client Client info structure
 *
 * @note Thread-safe: caller is responsible for synchronization
 *
 * @ingroup network
 */
void client_state_enable_video(client_info_t *client);

/**
 * @brief Enable audio streaming for a client
 *
 * Sets the client's audio streaming flag to indicate it's actively
 * sending audio samples to the server.
 *
 * @param client Client info structure
 *
 * @note Thread-safe: caller is responsible for synchronization
 *
 * @ingroup network
 */
void client_state_enable_audio(client_info_t *client);

/**
 * @brief Disable video streaming for a client
 *
 * Clears the client's video streaming flag to indicate it has stopped
 * sending video frames.
 *
 * @param client Client info structure
 *
 * @note Thread-safe: caller is responsible for synchronization
 *
 * @ingroup network
 */
void client_state_disable_video(client_info_t *client);

/**
 * @brief Disable audio streaming for a client
 *
 * Clears the client's audio streaming flag to indicate it has stopped
 * sending audio samples.
 *
 * @param client Client info structure
 *
 * @note Thread-safe: caller is responsible for synchronization
 *
 * @ingroup network
 */
void client_state_disable_audio(client_info_t *client);

/**
 * @brief Set both video and audio state based on stream type flags
 *
 * Convenience function to enable/disable video and audio based on
 * STREAM_TYPE_* flag bits.
 *
 * @param client Client info structure
 * @param stream_types Bitfield of stream types (STREAM_TYPE_VIDEO | STREAM_TYPE_AUDIO)
 * @param enable True to enable streams, false to disable
 *
 * @note This is useful for STREAM_START/STREAM_STOP handlers where
 *       the packet specifies which streams are starting/stopping
 *
 * @ingroup network
 */
void client_state_set_streams(client_info_t *client, uint32_t stream_types, bool enable);

/**
 * @brief Initialize client terminal capabilities
 *
 * Sets the client's terminal capability flags from the capabilities
 * field extracted from a CLIENT_CAPABILITIES packet.
 *
 * @param client Client info structure
 * @param capabilities Capability bitfield (already converted from network byte order)
 * @param width Terminal width in characters
 * @param height Terminal height in characters
 *
 * @note Consolidates the pattern of setting multiple capability fields
 *
 * @ingroup network
 */
void client_state_init_capabilities(client_info_t *client, uint32_t capabilities, uint32_t width, uint32_t height);

#endif // LIB_NETWORK_CLIENT_STATE_H
