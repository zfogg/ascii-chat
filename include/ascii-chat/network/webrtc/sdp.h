/**
 * @file network/webrtc/sdp.h
 * @brief SDP (Session Description Protocol) for WebRTC audio/video negotiation
 * @ingroup webrtc
 *
 * Handles SDP offer/answer generation and parsing for WebRTC connections.
 * Includes:
 * - Opus audio codec negotiation (48kHz, mono, 24kbps)
 * - Terminal capability negotiation via custom ACIP video "codecs"
 * - Format parameters for resolution, renderer, charset, compression
 *
 * ## SDP Offer/Answer Flow
 *
 * **Client (Joiner) generates OFFER:**
 * - Lists supported audio codecs (Opus)
 * - Lists supported terminal rendering modes in preference order
 * - Server selects best mutually-supported mode
 *
 * **Server (Creator) generates ANSWER:**
 * - Selects single audio codec (Opus)
 * - Selects best terminal rendering mode
 * - Server enforces its rendering constraints
 *
 * ## Terminal Capability "Codecs"
 *
 * These are RTP payload types that represent terminal rendering modes:
 * - PT 96: ACIP-TC (Truecolor, 24-bit RGB)
 * - PT 97: ACIP-256 (256-color xterm)
 * - PT 98: ACIP-16 (16-color ANSI)
 * - PT 99: ACIP-MONO (Monochrome, ASCII only)
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../../common.h"
#include "../../asciichat_errno.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Audio Codec Configuration
 * ============================================================================ */

/**
 * @brief Opus codec parameters for ascii-chat
 *
 * RFC 7587: RTP Payload Format for the Opus Speech and Audio Codec
 */
typedef struct {
  uint32_t sample_rate;    ///< 48000 Hz (Opus native rate)
  uint8_t channels;        ///< 1 (mono for voice chat)
  uint32_t bitrate;        ///< 24000 bps (good quality for speech)
  uint16_t frame_duration; ///< 20 ms (balance latency/efficiency)
  bool dtx_enabled;        ///< Discontinuous Transmission (silence suppression)
  bool fec_enabled;        ///< Forward Error Correction for lossy networks
} opus_config_t;

/* ============================================================================
 * Terminal Rendering Capability "Codecs"
 * ============================================================================ */

/**
 * @brief Terminal rendering capability payload types
 *
 * These are custom "codecs" that represent terminal rendering modes.
 * Used in SDP to negotiate which color mode both peers can support.
 */
typedef enum {
  ACIP_CODEC_TRUECOLOR = 96, ///< 24-bit RGB (truecolor)
  ACIP_CODEC_256COLOR = 97,  ///< 256-color (xterm palette)
  ACIP_CODEC_16COLOR = 98,   ///< 16-color (ANSI standard)
  ACIP_CODEC_MONO = 99,      ///< Monochrome (ASCII only)
} acip_codec_t;

/**
 * @brief Terminal rendering format parameters
 */
typedef struct {
  uint16_t width;  ///< Terminal width in characters
  uint16_t height; ///< Terminal height in characters
  enum { RENDERER_BLOCK, RENDERER_HALFBLOCK, RENDERER_BRAILLE } renderer;
  enum { CHARSET_ASCII, CHARSET_UTF8, CHARSET_UTF8_WIDE } charset;
  enum { COMPRESSION_NONE, COMPRESSION_RLE, COMPRESSION_ZSTD } compression;
  bool csi_rep_support;     ///< CSI REP (repeat) support
  const char *palette_hint; ///< Palette name (informational)
} terminal_format_params_t;

/**
 * @brief Supported terminal capability (for offer/answer)
 */
typedef struct {
  acip_codec_t codec;              ///< Rendering capability type
  terminal_format_params_t format; ///< Format parameters
} terminal_capability_t;

/* ============================================================================
 * SDP Session Description
 * ============================================================================ */

/**
 * @brief SDP media session (simplified for WebRTC)
 *
 * Represents a complete SDP offer or answer.
 * Includes audio (Opus) and video (terminal capabilities) media sections.
 */
typedef struct {
  /* Session-level attributes */
  char session_id[32];      ///< Session identifier
  char session_version[16]; ///< Session version (timestamp)

  /* Audio media section */
  bool has_audio;             ///< Audio media included
  opus_config_t audio_config; ///< Opus codec configuration

  /* Video media section (terminal capabilities) */
  bool has_video;                        ///< Video media included
  terminal_capability_t *video_codecs;   ///< Array of supported capabilities
  size_t video_codec_count;              ///< Number of supported capabilities
  terminal_format_params_t video_format; ///< Default format parameters

  /* Raw SDP string (generated from above) */
  char sdp_string[4096]; ///< Complete SDP as null-terminated string
  size_t sdp_length;     ///< Length of SDP string (excluding null)
} sdp_session_t;

/* ============================================================================
 * SDP Generation (Offer/Answer)
 * ============================================================================ */

/**
 * @brief Generate SDP offer (client side)
 *
 * Creates an SDP offer with:
 * - Opus audio codec
 * - Terminal capabilities in client preference order
 *
 * @param capabilities Array of supported terminal capabilities
 * @param capability_count Number of capabilities
 * @param audio_config Opus codec configuration
 * @param format Format parameters for video section
 * @param offer_out Generated SDP offer (output)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note Caller must free offer_out using sdp_session_free()
 */
asciichat_error_t sdp_generate_offer(const terminal_capability_t *capabilities, size_t capability_count,
                                     const opus_config_t *audio_config, const terminal_format_params_t *format,
                                     sdp_session_t *offer_out);

/**
 * @brief Generate SDP answer (server side)
 *
 * Creates an SDP answer by selecting best mutually-supported mode from offer.
 * Server enforces its rendering constraints.
 *
 * @param offer Received SDP offer from client
 * @param server_capabilities Array of server-supported capabilities
 * @param server_capability_count Number of server capabilities
 * @param audio_config Opus codec configuration
 * @param server_format Server's rendering constraints
 * @param answer_out Generated SDP answer (output)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note Caller must free answer_out using sdp_session_free()
 */
asciichat_error_t sdp_generate_answer(const sdp_session_t *offer, const terminal_capability_t *server_capabilities,
                                      size_t server_capability_count, const opus_config_t *audio_config,
                                      const terminal_format_params_t *server_format, sdp_session_t *answer_out);

/* ============================================================================
 * SDP Parsing (Offer/Answer)
 * ============================================================================ */

/**
 * @brief Parse SDP offer or answer
 *
 * Parses an SDP string and extracts audio/video configurations.
 *
 * @param sdp_string SDP string (null-terminated)
 * @param session Parsed session (output)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t sdp_parse(const char *sdp_string, sdp_session_t *session);

/**
 * @brief Extract selected video codec from SDP answer
 *
 * Determines which terminal rendering capability the peer selected.
 *
 * @param answer SDP answer from peer
 * @param selected_codec Selected codec (output)
 * @param selected_format Format parameters (output)
 * @return ASCIICHAT_OK if valid selection found, error otherwise
 */
asciichat_error_t sdp_get_selected_video_codec(const sdp_session_t *answer, acip_codec_t *selected_codec,
                                               terminal_format_params_t *selected_format);

/* ============================================================================
 * Capability Detection
 * ============================================================================ */

/**
 * @brief Detect client terminal capabilities at startup
 *
 * Auto-detects from environment and terminal:
 * 1. COLORTERM env var (truecolor/24bit)
 * 2. Terminal color query (terminfo/XTGETTCAP)
 * 3. UTF-8 support (LANG, test character)
 * 4. CSI REP support (escape sequence test)
 * 5. Terminal size (TIOCGWINSZ)
 *
 * @param capabilities Detected capabilities (output)
 * @param capability_count Maximum capabilities to detect
 * @param detected_count Actual number detected (output)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t sdp_detect_terminal_capabilities(terminal_capability_t *capabilities, size_t capability_count,
                                                   size_t *detected_count);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Free SDP session resources
 * @param session Session to free
 */
void sdp_session_free(sdp_session_t *session);

/**
 * @brief Get human-readable codec name
 * @param codec Codec type
 * @return Static string (e.g., "ACIP-TC", "ACIP-256")
 */
const char *sdp_codec_name(acip_codec_t codec);

/**
 * @brief Get human-readable renderer name
 * @return Static string (e.g., "block", "halfblock")
 */
const char *sdp_renderer_name(int renderer_type);

#ifdef __cplusplus
}
#endif
