/**
 * @file media/codecs.h
 * @brief Video and audio codec definitions and capability bitmasks
 * @ingroup media
 *
 * Defines codec types and capability bitmasks for video and audio streams.
 * Used in CLIENT_CAPABILITIES packet to negotiate supported codecs between
 * client and server.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date March 2026
 * @version 1.0
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name Video Codec Types
 * @brief Enumeration of supported video codecs
 * @ingroup media
 */
typedef enum {
  VIDEO_CODEC_RGBA = 0,   ///< Raw RGBA (4 bytes per pixel, uncompressed)
  VIDEO_CODEC_H265 = 1,   ///< H.265 (HEVC) video codec
  VIDEO_CODEC_JPEG = 2,   ///< JPEG still frame codec
} video_codec_t;

/**
 * @name Video Codec Bitmask Constants
 * @brief Bitmasks for video codec capabilities
 * @ingroup media
 *
 * Each codec is represented by a single bit, allowing multiple codecs
 * to be advertised simultaneously in the codec_capabilities_video field.
 */
#define VIDEO_CODEC_CAP_RGBA ((uint32_t)(1 << VIDEO_CODEC_RGBA))   ///< Bit 0: RGBA support
#define VIDEO_CODEC_CAP_H265 ((uint32_t)(1 << VIDEO_CODEC_H265))   ///< Bit 1: H.265 support
#define VIDEO_CODEC_CAP_JPEG ((uint32_t)(1 << VIDEO_CODEC_JPEG))   ///< Bit 2: JPEG support

/**
 * @brief Default video codec capabilities (all codecs supported)
 * @ingroup media
 */
#define VIDEO_CODEC_CAP_ALL (VIDEO_CODEC_CAP_RGBA | VIDEO_CODEC_CAP_H265 | VIDEO_CODEC_CAP_JPEG)

/**
 * @name Audio Codec Types
 * @brief Enumeration of supported audio codecs
 * @ingroup media
 */
typedef enum {
  AUDIO_CODEC_RAW = 0,   ///< Raw PCM audio (uncompressed)
  AUDIO_CODEC_OPUS = 1,  ///< Opus audio codec
} audio_codec_t;

/**
 * @name Audio Codec Bitmask Constants
 * @brief Bitmasks for audio codec capabilities
 * @ingroup media
 *
 * Each codec is represented by a single bit, allowing multiple codecs
 * to be advertised simultaneously in the codec_capabilities_audio field.
 */
#define AUDIO_CODEC_CAP_RAW ((uint32_t)(1 << AUDIO_CODEC_RAW))     ///< Bit 0: Raw PCM support
#define AUDIO_CODEC_CAP_OPUS ((uint32_t)(1 << AUDIO_CODEC_OPUS))   ///< Bit 1: Opus support

/**
 * @brief Default audio codec capabilities (all codecs supported)
 * @ingroup media
 */
#define AUDIO_CODEC_CAP_ALL (AUDIO_CODEC_CAP_RAW | AUDIO_CODEC_CAP_OPUS)

/**
 * @brief Check if a video codec is supported in capabilities bitmask
 * @param cap Codec capabilities bitmask (VIDEO_CODEC_CAP_*)
 * @param codec Codec type to check (video_codec_t)
 * @return true if codec is supported, false otherwise
 * @ingroup media
 */
#define VIDEO_CODEC_SUPPORTED(cap, codec) (((cap) & (1 << (codec))) != 0)

/**
 * @brief Check if an audio codec is supported in capabilities bitmask
 * @param cap Codec capabilities bitmask (AUDIO_CODEC_CAP_*)
 * @param codec Codec type to check (audio_codec_t)
 * @return true if codec is supported, false otherwise
 * @ingroup media
 */
#define AUDIO_CODEC_SUPPORTED(cap, codec) (((cap) & (1 << (codec))) != 0)

#ifdef __cplusplus
}
#endif

/** @} */
