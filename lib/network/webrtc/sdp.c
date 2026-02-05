/**
 * @file network/webrtc/sdp.c
 * @brief SDP offer/answer generation and parsing
 *
 * Implements SDP generation for audio (Opus) and video (terminal capabilities).
 * Terminal capabilities are negotiated as custom "codecs" in the video media section.
 *
 * @date January 2026
 */

#include <ascii-chat/network/webrtc/sdp.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/platform/util.h>
#include <ascii-chat/util/pcre2.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <pcre2.h>

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

const char *sdp_codec_name(acip_codec_t codec) {
  switch (codec) {
  case ACIP_CODEC_TRUECOLOR:
    return "ACIP-TC";
  case ACIP_CODEC_256COLOR:
    return "ACIP-256";
  case ACIP_CODEC_16COLOR:
    return "ACIP-16";
  case ACIP_CODEC_MONO:
    return "ACIP-MONO";
  default:
    return "UNKNOWN";
  }
}

const char *sdp_renderer_name(int renderer_type) {
  switch (renderer_type) {
  case RENDERER_BLOCK:
    return "block";
  case RENDERER_HALFBLOCK:
    return "halfblock";
  case RENDERER_BRAILLE:
    return "braille";
  default:
    return "unknown";
  }
}

/* ============================================================================
 * SDP Offer Generation
 * ============================================================================ */

asciichat_error_t sdp_generate_offer(const terminal_capability_t *capabilities, size_t capability_count,
                                     const opus_config_t *audio_config, const terminal_format_params_t *format,
                                     sdp_session_t *offer_out) {
  if (!capabilities || capability_count == 0 || !audio_config || !offer_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid SDP offer parameters");
  }

  memset(offer_out, 0, sizeof(sdp_session_t));

  // Generate session ID and version from current time
  time_t now = time(NULL);
  safe_snprintf(offer_out->session_id, sizeof(offer_out->session_id), "%ld", now);
  safe_snprintf(offer_out->session_version, sizeof(offer_out->session_version), "%ld", now);

  // Allocate and copy video codecs
  offer_out->video_codecs = SAFE_MALLOC(capability_count * sizeof(terminal_capability_t), terminal_capability_t *);
  memcpy(offer_out->video_codecs, capabilities, capability_count * sizeof(terminal_capability_t));
  offer_out->video_codec_count = capability_count;

  offer_out->has_audio = true;
  memcpy(&offer_out->audio_config, audio_config, sizeof(opus_config_t));

  offer_out->has_video = true;
  if (format) {
    memcpy(&offer_out->video_format, format, sizeof(terminal_format_params_t));
  }

  // Build complete SDP offer
  char *sdp = offer_out->sdp_string;
  size_t remaining = sizeof(offer_out->sdp_string);
  int written = 0;

  // Session-level attributes
  written = safe_snprintf(sdp, remaining, "v=0\r\n");
  sdp += written;
  remaining -= written;

  written = safe_snprintf(sdp, remaining, "o=ascii-chat %s %s IN IP4 0.0.0.0\r\n", offer_out->session_id,
                          offer_out->session_version);
  sdp += written;
  remaining -= written;

  written = safe_snprintf(sdp, remaining, "s=-\r\n");
  sdp += written;
  remaining -= written;

  written = safe_snprintf(sdp, remaining, "t=0 0\r\n");
  sdp += written;
  remaining -= written;

  // Audio media section
  written = safe_snprintf(sdp, remaining, "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n");
  sdp += written;
  remaining -= written;

  written = safe_snprintf(sdp, remaining, "a=rtpmap:111 opus/48000/2\r\n");
  sdp += written;
  remaining -= written;

  written = safe_snprintf(sdp, remaining, "a=fmtp:111 minptime=10;useinbandfec=1;usedtx=1\r\n");
  sdp += written;
  remaining -= written;

  // Video media section with terminal capabilities
  char codec_list[128] = "96";
  for (size_t i = 1; i < capability_count; i++) {
    char codec_num[4];
    safe_snprintf(codec_num, sizeof(codec_num), " %d", 96 + (int)i);
    size_t len = strlen(codec_list);
    strncat(codec_list, codec_num, sizeof(codec_list) - len - 1);
  }

  written = safe_snprintf(sdp, remaining, "m=video 9 UDP/TLS/RTP/SAVPF %s\r\n", codec_list);
  sdp += written;
  remaining -= written;

  // Add rtpmap and fmtp for each capability
  for (size_t i = 0; i < capability_count; i++) {
    int pt = 96 + (int)i;
    const char *codec_name = sdp_codec_name(capabilities[i].codec);

    written = safe_snprintf(sdp, remaining, "a=rtpmap:%d %s/90000\r\n", pt, codec_name);
    sdp += written;
    remaining -= written;

    // Format parameters
    const terminal_format_params_t *cap_format = &capabilities[i].format;
    const char *renderer_name = sdp_renderer_name(cap_format->renderer);
    const char *charset_name = (cap_format->charset == CHARSET_UTF8) ? "utf8" : "ascii";
    const char *compression_name = "none";
    if (cap_format->compression == COMPRESSION_RLE) {
      compression_name = "rle";
    } else if (cap_format->compression == COMPRESSION_ZSTD) {
      compression_name = "zstd";
    }

    written = safe_snprintf(sdp, remaining,
                            "a=fmtp:%d width=%u;height=%u;renderer=%s;charset=%s;compression=%s;csi_rep=%d\r\n", pt,
                            cap_format->width, cap_format->height, renderer_name, charset_name, compression_name,
                            cap_format->csi_rep_support ? 1 : 0);
    sdp += written;
    remaining -= written;
  }

  offer_out->sdp_length = strlen(offer_out->sdp_string);

  log_debug("SDP: Generated offer with %zu video codecs and Opus audio", capability_count);

  return ASCIICHAT_OK;
}

/* ============================================================================
 * SDP Answer Generation
 * ============================================================================ */

asciichat_error_t sdp_generate_answer(const sdp_session_t *offer, const terminal_capability_t *server_capabilities,
                                      size_t server_capability_count, const opus_config_t *audio_config,
                                      const terminal_format_params_t *server_format, sdp_session_t *answer_out) {
  if (!offer || !server_capabilities || server_capability_count == 0 || !audio_config || !answer_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid SDP answer parameters");
  }

  memset(answer_out, 0, sizeof(sdp_session_t));

  // Copy session ID from offer, increment version
  SAFE_STRNCPY(answer_out->session_id, offer->session_id, sizeof(answer_out->session_id));

  time_t now = time(NULL);
  safe_snprintf(answer_out->session_version, sizeof(answer_out->session_version), "%ld", now);

  // Answer audio section: accept Opus
  answer_out->has_audio = offer->has_audio;
  if (answer_out->has_audio) {
    memcpy(&answer_out->audio_config, audio_config, sizeof(opus_config_t));
  }

  // Answer video section: find best mutually-supported codec
  answer_out->has_video = offer->has_video;

  if (answer_out->has_video && offer->video_codecs && offer->video_codec_count > 0) {
    // Find best codec: iterate through server preferences and find first match in offer
    int selected_index = -1;

    for (size_t s = 0; s < server_capability_count; s++) {
      for (size_t o = 0; o < offer->video_codec_count; o++) {
        if (server_capabilities[s].codec == offer->video_codecs[o].codec) {
          selected_index = (int)s;
          break; // Found match, use server's preference order
        }
      }
      if (selected_index >= 0) {
        break;
      }
    }

    // Allocate and fill selected codec
    answer_out->video_codecs = SAFE_MALLOC(sizeof(terminal_capability_t), terminal_capability_t *);
    answer_out->video_codec_count = 1;

    // Use server's capability with any server_format overrides
    if (selected_index >= 0) {
      memcpy(answer_out->video_codecs, &server_capabilities[selected_index], sizeof(terminal_capability_t));
    } else {
      // Fallback to monochrome
      memcpy(answer_out->video_codecs, &server_capabilities[0], sizeof(terminal_capability_t));
    }

    // Apply server format constraints if provided
    if (server_format) {
      if (server_format->width > 0) {
        answer_out->video_codecs[0].format.width = server_format->width;
      }
      if (server_format->height > 0) {
        answer_out->video_codecs[0].format.height = server_format->height;
      }
      if (server_format->renderer != RENDERER_BLOCK) {
        answer_out->video_codecs[0].format.renderer = server_format->renderer;
      }
      if (server_format->compression != COMPRESSION_NONE) {
        answer_out->video_codecs[0].format.compression = server_format->compression;
      }
    }

    memcpy(&answer_out->video_format, &answer_out->video_codecs[0].format, sizeof(terminal_format_params_t));
  }

  // Build complete SDP answer
  char *sdp = answer_out->sdp_string;
  size_t remaining = sizeof(answer_out->sdp_string);
  int written = 0;

  // Session-level attributes
  written = safe_snprintf(sdp, remaining, "v=0\r\n");
  sdp += written;
  remaining -= written;

  written = safe_snprintf(sdp, remaining, "o=ascii-chat %s %s IN IP4 0.0.0.0\r\n", answer_out->session_id,
                          answer_out->session_version);
  sdp += written;
  remaining -= written;

  written = safe_snprintf(sdp, remaining, "s=-\r\n");
  sdp += written;
  remaining -= written;

  written = safe_snprintf(sdp, remaining, "t=0 0\r\n");
  sdp += written;
  remaining -= written;

  // Audio media section (if present in offer)
  if (answer_out->has_audio) {
    written = safe_snprintf(sdp, remaining, "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n");
    sdp += written;
    remaining -= written;

    written = safe_snprintf(sdp, remaining, "a=rtpmap:111 opus/48000/2\r\n");
    sdp += written;
    remaining -= written;

    written = safe_snprintf(sdp, remaining, "a=fmtp:111 minptime=10;useinbandfec=1;usedtx=1\r\n");
    sdp += written;
    remaining -= written;
  }

  // Video media section (if present in offer)
  if (answer_out->has_video && answer_out->video_codecs && answer_out->video_codec_count > 0) {
    written = safe_snprintf(sdp, remaining, "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n");
    sdp += written;
    remaining -= written;

    const char *codec_name = sdp_codec_name(answer_out->video_codecs[0].codec);
    written = safe_snprintf(sdp, remaining, "a=rtpmap:96 %s/90000\r\n", codec_name);
    sdp += written;
    remaining -= written;

    // Format parameters
    const terminal_format_params_t *cap_format = &answer_out->video_codecs[0].format;
    const char *renderer_name = sdp_renderer_name(cap_format->renderer);
    const char *charset_name = (cap_format->charset == CHARSET_UTF8) ? "utf8" : "ascii";
    const char *compression_name = "none";
    if (cap_format->compression == COMPRESSION_RLE) {
      compression_name = "rle";
    } else if (cap_format->compression == COMPRESSION_ZSTD) {
      compression_name = "zstd";
    }

    written = safe_snprintf(sdp, remaining,
                            "a=fmtp:96 width=%u;height=%u;renderer=%s;charset=%s;compression=%s;csi_rep=%d\r\n",
                            cap_format->width, cap_format->height, renderer_name, charset_name, compression_name,
                            cap_format->csi_rep_support ? 1 : 0);
    sdp += written;
    remaining -= written;
  }

  answer_out->sdp_length = strlen(answer_out->sdp_string);

  log_debug("SDP: Generated answer with codec %s", sdp_codec_name(answer_out->video_codecs[0].codec));

  return ASCIICHAT_OK;
}

/* ============================================================================
 * SDP Parsing with PCRE2
 * ============================================================================ */

/**
 * @brief PCRE2 regex validator for SDP fmtp parameter parsing
 *
 * Extracts video terminal capability parameters from SDP fmtp attributes.
 * Uses centralized PCRE2 singleton for atomic extraction with single regex match.
 */

static const char *SDP_FMTP_VIDEO_PATTERN = "width=([0-9]+)"                  // 1: width
                                            ".*?height=([0-9]+)"              // 2: height
                                            ".*?renderer=([a-z]+)"            // 3: renderer (block/halfblock/braille)
                                            "(?:.*?charset=([a-z0-9_]+))?"    // 4: charset (optional)
                                            "(?:.*?compression=([a-z0-9]+))?" // 5: compression (optional)
                                            "(?:.*?csi_rep=([01]))?";         // 6: csi_rep (optional)

static pcre2_singleton_t *g_sdp_fmtp_video_regex = NULL;

/**
 * Get compiled SDP fmtp video regex (lazy initialization)
 * Returns NULL if compilation failed
 */
static pcre2_code *sdp_fmtp_video_regex_get(void) {
  if (g_sdp_fmtp_video_regex == NULL) {
    g_sdp_fmtp_video_regex = asciichat_pcre2_singleton_compile(SDP_FMTP_VIDEO_PATTERN, PCRE2_DOTALL);
  }
  return asciichat_pcre2_singleton_get_code(g_sdp_fmtp_video_regex);
}

/**
 * @brief Parse video fmtp parameters using PCRE2 regex singleton
 *
 * Extracts width, height, renderer, charset, compression, and csi_rep from fmtp string.
 * Falls back to manual parsing if PCRE2 unavailable.
 *
 * @param fmtp_params SDP fmtp parameter string
 * @param cap Output terminal capability structure
 * @return true if successfully parsed, false otherwise
 */
static bool sdp_parse_fmtp_video_pcre2(const char *fmtp_params, terminal_capability_t *cap) {
  if (!fmtp_params || !cap) {
    return false;
  }

  // Get compiled regex (lazy initialization)
  pcre2_code *regex = sdp_fmtp_video_regex_get();

  // If PCRE2 not available, fall through to manual parsing
  if (!regex) {
    return false; // Fall back to manual parser in caller
  }

  pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(regex, NULL);
  if (!match_data) {
    return false;
  }

  int rc = pcre2_jit_match(regex, (PCRE2_SPTR8)fmtp_params, strlen(fmtp_params), 0, 0, match_data, NULL);

  bool success = false;
  if (rc >= 4) { // At least 4 groups (width, height, renderer, and must be present)
    PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);

    // Extract width (group 1)
    size_t start = ovector[2], end = ovector[3];
    if (start < end) {
      char width_str[16];
      memcpy(width_str, fmtp_params + start, end - start);
      width_str[end - start] = '\0';
      cap->format.width = (uint16_t)strtoul(width_str, NULL, 10);
    }

    // Extract height (group 2)
    start = ovector[4], end = ovector[5];
    if (start < end) {
      char height_str[16];
      memcpy(height_str, fmtp_params + start, end - start);
      height_str[end - start] = '\0';
      cap->format.height = (uint16_t)strtoul(height_str, NULL, 10);
    }

    // Extract renderer (group 3)
    start = ovector[6], end = ovector[7];
    if (start < end) {
      char renderer_str[16];
      memcpy(renderer_str, fmtp_params + start, end - start);
      renderer_str[end - start] = '\0';
      if (strcmp(renderer_str, "block") == 0) {
        cap->format.renderer = RENDERER_BLOCK;
      } else if (strcmp(renderer_str, "halfblock") == 0) {
        cap->format.renderer = RENDERER_HALFBLOCK;
      } else if (strcmp(renderer_str, "braille") == 0) {
        cap->format.renderer = RENDERER_BRAILLE;
      }
    }

    // Extract charset (group 4, optional)
    start = ovector[8], end = ovector[9];
    if (start < end && start != (size_t)-1) {
      char charset_str[16];
      memcpy(charset_str, fmtp_params + start, end - start);
      charset_str[end - start] = '\0';
      if (strcmp(charset_str, "utf8_wide") == 0) {
        cap->format.charset = CHARSET_UTF8_WIDE;
      } else if (strcmp(charset_str, "utf8") == 0) {
        cap->format.charset = CHARSET_UTF8;
      } else {
        cap->format.charset = CHARSET_ASCII;
      }
    }

    // Extract compression (group 5, optional)
    start = ovector[10], end = ovector[11];
    if (start < end && start != (size_t)-1) {
      char compression_str[16];
      memcpy(compression_str, fmtp_params + start, end - start);
      compression_str[end - start] = '\0';
      if (strcmp(compression_str, "zstd") == 0) {
        cap->format.compression = COMPRESSION_ZSTD;
      } else if (strcmp(compression_str, "rle") == 0) {
        cap->format.compression = COMPRESSION_RLE;
      } else {
        cap->format.compression = COMPRESSION_NONE;
      }
    }

    // Extract csi_rep (group 6, optional)
    start = ovector[12], end = ovector[13];
    if (start < end && start != (size_t)-1) {
      cap->format.csi_rep_support = (fmtp_params[start] == '1');
    }

    success = true;
  }

  pcre2_match_data_free(match_data);
  return success;
}

asciichat_error_t sdp_parse(const char *sdp_string, sdp_session_t *session) {
  if (!sdp_string || !session) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid SDP parse parameters");
  }

  memset(session, 0, sizeof(sdp_session_t));

  // Copy original SDP string for reference
  SAFE_STRNCPY(session->sdp_string, sdp_string, sizeof(session->sdp_string));
  session->sdp_length = strlen(sdp_string);

  // Parse line by line
  char line_buffer[512];
  SAFE_STRNCPY(line_buffer, sdp_string, sizeof(line_buffer));

  char *saveptr = NULL;
  char *line = platform_strtok_r(line_buffer, "\r\n", &saveptr);

  int in_audio_section = 0;
  int in_video_section = 0;
  size_t video_codec_index = 0;

  while (line) {
    // Parse line format: "key=value"
    char *equals = strchr(line, '=');
    if (!equals) {
      line = platform_strtok_r(NULL, "\r\n", &saveptr);
      continue;
    }

    char key = line[0];
    const char *value = equals + 1;

    switch (key) {
    case 'v':
      // v=0 (version)
      break;

    case 'o':
      // o=username session_id session_version ... session_id ...
      // Parse session ID (second field)
      {
        char o_copy[256];
        SAFE_STRNCPY(o_copy, value, sizeof(o_copy));
        char *o_saveptr = NULL;
        platform_strtok_r(o_copy, " ", &o_saveptr); // username
        char *session_id_str = platform_strtok_r(NULL, " ", &o_saveptr);
        if (session_id_str) {
          SAFE_STRNCPY(session->session_id, session_id_str, sizeof(session->session_id));
        }
      }
      break;

    case 's':
      // s=session_name (typically "-" or empty)
      break;

    case 'm':
      // m=audio 9 UDP/TLS/RTP/SAVPF ...
      // m=video 9 UDP/TLS/RTP/SAVPF ...
      in_audio_section = 0;
      in_video_section = 0;

      if (strstr(value, "audio")) {
        in_audio_section = 1;
        session->has_audio = true;
      } else if (strstr(value, "video")) {
        in_video_section = 1;
        session->has_video = true;
        video_codec_index = 0;
      }
      break;

    case 'a':
      // a=rtpmap:111 opus/48000/2
      // a=fmtp:111 minptime=10;useinbandfec=1;usedtx=1
      // a=rtpmap:96 ACIP-TC/90000
      // a=fmtp:96 ...

      if (strstr(value, "rtpmap:")) {
        const char *rtpmap = value + 7; // Skip "rtpmap:"

        // Parse: PT codec/rate[/channels]
        int pt = 0;
        char codec_name[32] = {0};
        sscanf(rtpmap, "%d %s", &pt, codec_name);

        if (in_audio_section && pt == 111 && strstr(codec_name, "opus")) {
          // Opus audio
          session->audio_config.sample_rate = 48000;
          session->audio_config.channels = 2;
        } else if (in_video_section && video_codec_index < 4) {
          // Terminal capability codec
          if (!session->video_codecs) {
            session->video_codecs = SAFE_MALLOC(4 * sizeof(terminal_capability_t), terminal_capability_t *);
          }

          terminal_capability_t *cap = &session->video_codecs[video_codec_index];
          memset(cap, 0, sizeof(terminal_capability_t));

          if (pt == 96 && strstr(codec_name, "ACIP-TC")) {
            cap->codec = ACIP_CODEC_TRUECOLOR;
            video_codec_index++;
            session->video_codec_count++;
          } else if (pt == 97 && strstr(codec_name, "ACIP-256")) {
            cap->codec = ACIP_CODEC_256COLOR;
            video_codec_index++;
            session->video_codec_count++;
          } else if (pt == 98 && strstr(codec_name, "ACIP-16")) {
            cap->codec = ACIP_CODEC_16COLOR;
            video_codec_index++;
            session->video_codec_count++;
          } else if (pt == 99 && strstr(codec_name, "ACIP-MONO")) {
            cap->codec = ACIP_CODEC_MONO;
            video_codec_index++;
            session->video_codec_count++;
          }
        }
      } else if (strstr(value, "fmtp:")) {
        // Parse format parameters
        // a=fmtp:111 minptime=10;useinbandfec=1;usedtx=1
        // a=fmtp:96 width=80;height=24;renderer=block;charset=utf8;compression=rle;csi_rep=1

        const char *fmtp = value + 5; // Skip "fmtp:"

        if (in_audio_section) {
          // Parse Opus parameters
          if (strstr(fmtp, "useinbandfec=1")) {
            session->audio_config.fec_enabled = true;
          }
          if (strstr(fmtp, "usedtx=1")) {
            session->audio_config.dtx_enabled = true;
          }
          // Default values for Opus
          session->audio_config.bitrate = 24000;
          session->audio_config.frame_duration = 20;
        } else if (in_video_section && video_codec_index > 0) {
          // Parse terminal format parameters
          terminal_capability_t *cap = &session->video_codecs[video_codec_index - 1];

          // Skip PT number in fmtp string (format: "96 width=...")
          const char *params = fmtp;
          while (*params && *params != ' ') {
            params++;
          }
          if (*params == ' ') {
            params++; // Skip space after PT
          }

          // Parse with PCRE2 regex (atomic extraction)
          sdp_parse_fmtp_video_pcre2(params, cap);
        }
      }
      break;
    }

    line = platform_strtok_r(NULL, "\r\n", &saveptr);
  }

  return ASCIICHAT_OK;
}

/* ============================================================================
 * Video Codec Selection
 * ============================================================================ */

asciichat_error_t sdp_get_selected_video_codec(const sdp_session_t *answer, acip_codec_t *selected_codec,
                                               terminal_format_params_t *selected_format) {
  if (!answer || !selected_codec || !selected_format) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for codec selection");
  }

  if (!answer->has_video || !answer->video_codecs || answer->video_codec_count == 0) {
    return SET_ERRNO(ERROR_NOT_FOUND, "No video codec in answer");
  }

  // In SDP answer, server selects only ONE codec (server's preference)
  // The first codec in the answer is the selected one
  terminal_capability_t *selected_cap = &answer->video_codecs[0];

  *selected_codec = selected_cap->codec;
  memcpy(selected_format, &selected_cap->format, sizeof(terminal_format_params_t));

  log_debug("SDP: Selected video codec %s with resolution %ux%u", sdp_codec_name(selected_cap->codec),
            selected_cap->format.width, selected_cap->format.height);

  return ASCIICHAT_OK;
}

/* ============================================================================
 * Terminal Capability Detection
 * ============================================================================ */

asciichat_error_t sdp_detect_terminal_capabilities(terminal_capability_t *capabilities, size_t capability_count,
                                                   size_t *detected_count) {
  if (!capabilities || capability_count == 0 || !detected_count) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid capability detection parameters");
  }

  *detected_count = 0;
  memset(capabilities, 0, capability_count * sizeof(terminal_capability_t));

  // Detect terminal color level using platform terminal module
  const char *colorterm = SAFE_GETENV("COLORTERM");
  const char *term = SAFE_GETENV("TERM");
  terminal_color_mode_t color_level = TERM_COLOR_NONE;

  if (colorterm && (strcmp(colorterm, "truecolor") == 0 || strcmp(colorterm, "24bit") == 0)) {
    color_level = TERM_COLOR_TRUECOLOR;
  } else if (term) {
    if (strstr(term, "256color") || strstr(term, "256")) {
      color_level = TERM_COLOR_256;
    } else if (strstr(term, "color") || strcmp(term, "xterm") == 0) {
      color_level = TERM_COLOR_16;
    }
  }

  // Detect UTF-8 support from LANG environment variable
  const char *lang = SAFE_GETENV("LANG");
  bool has_utf8 = (lang && (strstr(lang, "UTF-8") || strstr(lang, "utf8")));

  // CSI REP support correlates with UTF-8
  bool has_csi_rep = has_utf8;

  // Get terminal size from platform module
  uint16_t term_width = 80;  // default
  uint16_t term_height = 24; // default

  terminal_size_t term_size;
  asciichat_error_t term_err = terminal_get_size(&term_size);
  if (term_err == ASCIICHAT_OK) {
    term_width = (uint16_t)term_size.cols;
    term_height = (uint16_t)term_size.rows;
  } else {
    log_debug("SDP: Using default terminal size %ux%u", term_width, term_height);
  }

  // Fill capabilities array based on detected color level (preference order)
  size_t idx = 0;

  if (color_level >= TERM_COLOR_TRUECOLOR && idx < capability_count) {
    capabilities[idx].codec = ACIP_CODEC_TRUECOLOR;
    capabilities[idx].format.width = term_width;
    capabilities[idx].format.height = term_height;
    capabilities[idx].format.renderer = RENDERER_BLOCK;
    capabilities[idx].format.charset = has_utf8 ? CHARSET_UTF8 : CHARSET_ASCII;
    capabilities[idx].format.compression = COMPRESSION_RLE;
    capabilities[idx].format.csi_rep_support = has_csi_rep;
    idx++;
  }

  if (color_level >= TERM_COLOR_256 && idx < capability_count) {
    capabilities[idx].codec = ACIP_CODEC_256COLOR;
    capabilities[idx].format.width = term_width;
    capabilities[idx].format.height = term_height;
    capabilities[idx].format.renderer = RENDERER_BLOCK;
    capabilities[idx].format.charset = has_utf8 ? CHARSET_UTF8 : CHARSET_ASCII;
    capabilities[idx].format.compression = COMPRESSION_RLE;
    capabilities[idx].format.csi_rep_support = has_csi_rep;
    idx++;
  }

  if (color_level >= TERM_COLOR_16 && idx < capability_count) {
    capabilities[idx].codec = ACIP_CODEC_16COLOR;
    capabilities[idx].format.width = term_width;
    capabilities[idx].format.height = term_height;
    capabilities[idx].format.renderer = RENDERER_BLOCK;
    capabilities[idx].format.charset = has_utf8 ? CHARSET_UTF8 : CHARSET_ASCII;
    capabilities[idx].format.compression = COMPRESSION_NONE;
    capabilities[idx].format.csi_rep_support = has_csi_rep;
    idx++;
  }

  // Monochrome always supported
  if (idx < capability_count) {
    capabilities[idx].codec = ACIP_CODEC_MONO;
    capabilities[idx].format.width = term_width;
    capabilities[idx].format.height = term_height;
    capabilities[idx].format.renderer = RENDERER_BLOCK;
    capabilities[idx].format.charset = has_utf8 ? CHARSET_UTF8 : CHARSET_ASCII;
    capabilities[idx].format.compression = COMPRESSION_NONE;
    capabilities[idx].format.csi_rep_support = has_csi_rep;
    idx++;
  }

  *detected_count = idx;

  log_debug("SDP: Detected %zu terminal capabilities (colors=%d, utf8=%s, csi_rep=%s, size=%ux%u)", *detected_count,
            color_level, has_utf8 ? "yes" : "no", has_csi_rep ? "yes" : "no", term_width, term_height);

  return ASCIICHAT_OK;
}

/* ============================================================================
 * Resource Cleanup
 * ============================================================================ */

void sdp_session_free(sdp_session_t *session) {
  if (!session) {
    return;
  }

  // Free allocated video codec array
  if (session->video_codecs) {
    SAFE_FREE(session->video_codecs);
    session->video_codec_count = 0;
  }

  // Zero out the session structure
  memset(session, 0, sizeof(sdp_session_t));
}
