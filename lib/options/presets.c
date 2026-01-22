/**
 * @file presets.c
 * @brief Preset option configurations for ascii-chat modes
 * @ingroup options
 */

#include "builder.h"
#include "options.h"
#include "parsers.h"
#include "actions.h"
#include "config.h"
#include "common.h"
#include "platform/terminal.h"
#include "video/palette.h"
#include "log/logging.h"
#include "registry.h"

// ============================================================================
// Binary-Level Options Helper
// ============================================================================

/**
 * @brief Custom parser for --verbose flag
 *
 * Allows --verbose to work both as a flag (without argument) and with an optional
 * count argument. Increments verbose_level each time called.
 */
static bool parse_verbose_flag(const char *arg, void *dest, char **error_msg) {
  (void)error_msg; // Unused but required by function signature

  // If arg is NULL or starts with a flag, just increment
  // Otherwise try to parse as integer count
  unsigned short int *verbose_level = (unsigned short int *)dest;

  if (!arg || arg[0] == '\0') {
    // No argument provided, just increment
    (*verbose_level)++;
    return true;
  }

  // Try to parse as integer count
  char *endptr;
  long value = strtol(arg, &endptr, 10);
  if (*endptr == '\0' && value >= 0 && value <= 100) {
    *verbose_level = (unsigned short int)value;
    return true;
  }

  // If it didn't parse as int, treat as flag increment
  (*verbose_level)++;
  return true;
}

/**
 * @brief Custom parser for --cookies-from-browser
 *
 * Allows --cookies-from-browser to work as an optional argument.
 * When used without argument, defaults to 'chrome'.
 * When used with argument, uses specified browser name.
 */
static bool parse_cookies_from_browser(const char *arg, void *dest, char **error_msg) {
  (void)error_msg; // Unused but required by function signature

  char *browser_buf = (char *)dest;
  const size_t max_size = 256;

  if (!arg || arg[0] == '\0') {
    // No argument provided, default to chrome
    strncpy(browser_buf, "chrome", max_size - 1);
    browser_buf[max_size - 1] = '\0';
    return true;
  }

  // Copy the provided browser name
  strncpy(browser_buf, arg, max_size - 1);
  browser_buf[max_size - 1] = '\0';
  return true;
}

/**
 * @brief Custom parser for --seek flag
 *
 * Accepts both "hh:mm:ss.ms" format and plain seconds format.
 * Examples:
 * - "30" = 30 seconds
 * - "30.5" = 30.5 seconds
 * - "1:30" = 1 minute 30 seconds (90 seconds)
 * - "1:30.5" = 1 minute 30.5 seconds (90.5 seconds)
 * - "0:1:30.5" = 1 minute 30.5 seconds (90.5 seconds)
 * - "1:2:30.5" = 1 hour 2 minutes 30.5 seconds (3750.5 seconds)
 */
static bool parse_timestamp(const char *arg, void *dest, char **error_msg) {
  if (!arg || arg[0] == '\0') {
    if (error_msg) {
      *error_msg = strdup("--seek requires a timestamp argument");
    }
    return false;
  }

  double *timestamp = (double *)dest;
  char *endptr;
  long strtol_result;

  // Count colons to determine format
  int colon_count = 0;
  for (const char *p = arg; *p; p++) {
    if (*p == ':')
      colon_count++;
  }

  if (colon_count == 0) {
    // Plain seconds format: "30" or "30.5"
    *timestamp = strtod(arg, &endptr);
    if (*endptr != '\0' || *timestamp < 0.0) {
      if (error_msg) {
        *error_msg = strdup("Invalid timestamp: expected non-negative seconds");
      }
      return false;
    }
    return true;
  } else if (colon_count == 1) {
    // MM:SS or MM:SS.ms format
    strtol_result = strtol(arg, &endptr, 10);
    if (*endptr != ':' || strtol_result < 0) {
      if (error_msg) {
        *error_msg = strdup("Invalid timestamp: expected MM:SS or MM:SS.ms format");
      }
      return false;
    }
    long minutes = strtol_result;
    double seconds = strtod(endptr + 1, &endptr);
    if (*endptr != '\0' && *endptr != '.' && *endptr != '\0') {
      if (error_msg) {
        *error_msg = strdup("Invalid timestamp: expected MM:SS or MM:SS.ms format");
      }
      return false;
    }
    *timestamp = minutes * 60.0 + seconds;
    return true;
  } else if (colon_count == 2) {
    // HH:MM:SS or HH:MM:SS.ms format
    strtol_result = strtol(arg, &endptr, 10);
    if (*endptr != ':' || strtol_result < 0) {
      if (error_msg) {
        *error_msg = strdup("Invalid timestamp: expected HH:MM:SS or HH:MM:SS.ms format");
      }
      return false;
    }
    long hours = strtol_result;

    strtol_result = strtol(endptr + 1, &endptr, 10);
    if (*endptr != ':' || strtol_result < 0 || strtol_result >= 60) {
      if (error_msg) {
        *error_msg = strdup("Invalid timestamp: minutes must be 0-59");
      }
      return false;
    }
    long minutes = strtol_result;

    double seconds = strtod(endptr + 1, &endptr);
    if (*endptr != '\0') {
      if (error_msg) {
        *error_msg = strdup("Invalid timestamp: expected HH:MM:SS or HH:MM:SS.ms format");
      }
      return false;
    }
    *timestamp = hours * 3600.0 + minutes * 60.0 + seconds;
    return true;
  } else {
    if (error_msg) {
      *error_msg = strdup("Invalid timestamp format");
    }
    return false;
  }
}

/**
 * @brief Add binary-level logging options to a builder
 *
 * This ensures consistent option definitions across all modes.
 * These options can be used before OR after the mode name.
 */
void options_builder_add_logging_group(options_builder_t *b) {
  // Use static const to ensure default persists for the lifetime of the program
  static const log_level_t default_log_level = DEFAULT_LOG_LEVEL;

  options_builder_add_string(b, "log-file", 'L', offsetof(options_t, log_file), "", "Redirect logs to FILE", "LOGGING",
                             false, "ASCII_CHAT_LOG_FILE", NULL);

  options_builder_add_callback(b, "log-level", '\0', offsetof(options_t, log_level),
                               &default_log_level, // Default level based on build type
                               sizeof(log_level_t), parse_log_level,
                               "Set log level: dev, debug, info, warn, error, fatal", "LOGGING", false, NULL);

  options_builder_add_callback_optional(b, "verbose", 'V', offsetof(options_t, verbose_level),
                                        &(unsigned short int){0}, // Default: 0 (no extra verbosity)
                                        sizeof(unsigned short int), parse_verbose_flag,
                                        "Increase log verbosity (stackable: -VV, -VVV, or --verbose)", "LOGGING", false,
                                        NULL,
                                        true); // optional_arg = true

  options_builder_add_bool(b, "quiet", 'q', offsetof(options_t, quiet), OPT_QUIET_DEFAULT,
                           "Disable console logging (log to file only)", "LOGGING", false, NULL);
}

// ============================================================================
// Webcam & Display Options Helper (Client + Mirror)
// ============================================================================

/**
 * @brief Add terminal dimension options (width, height)
 * Used by: client, mirror modes
 */
void options_builder_add_terminal_group(options_builder_t *b) {
  options_builder_add_int(b, "width", 'x', offsetof(options_t, width), OPT_WIDTH_DEFAULT,
                          "Terminal width in characters", "TERMINAL", false, "ASCII_CHAT_WIDTH", NULL);

  options_builder_add_int(b, "height", 'y', offsetof(options_t, height), OPT_HEIGHT_DEFAULT,
                          "Terminal height in characters", "TERMINAL", false, "ASCII_CHAT_HEIGHT", NULL);
}

/**
 * @brief Add webcam options (device selection, flipping, test pattern)
 * Used by: client, mirror modes
 */
void options_builder_add_webcam_group(options_builder_t *b) {
  options_builder_add_int(b, "webcam-index", 'c', offsetof(options_t, webcam_index), OPT_WEBCAM_INDEX_DEFAULT,
                          "Webcam device index", "WEBCAM", false, "ASCII_CHAT_WEBCAM_INDEX", NULL);

  options_builder_add_bool(b, "webcam-flip", 'g', offsetof(options_t, webcam_flip), OPT_WEBCAM_FLIP_DEFAULT,
                           "Flip webcam horizontally", "WEBCAM", false, NULL);

  options_builder_add_bool(b, "test-pattern", '\0', offsetof(options_t, test_pattern), OPT_TEST_PATTERN_DEFAULT,
                           "Use test pattern instead of webcam", "WEBCAM", false, "WEBCAM_DISABLED");
}

/**
 * @brief Add display/rendering options (color mode, palette, etc.)
 * Used by: client, mirror modes
 */
void options_builder_add_display_group(options_builder_t *b) {
  options_builder_add_callback(b, "color-mode", '\0', offsetof(options_t, color_mode),
                               &(terminal_color_mode_t){TERM_COLOR_AUTO}, // Auto-detect by default
                               sizeof(terminal_color_mode_t), parse_color_mode,
                               "Terminal color level (auto, none, 16, 256, truecolor)", "DISPLAY", false,
                               "ASCII_CHAT_COLOR_MODE");

  options_builder_add_callback(b, "render-mode", 'M', offsetof(options_t, render_mode),
                               &(render_mode_t){RENDER_MODE_FOREGROUND}, // Default: foreground
                               sizeof(render_mode_t), parse_render_mode,
                               "Render mode (foreground, background, half-block)", "DISPLAY", false,
                               "ASCII_CHAT_RENDER_MODE");

  options_builder_add_callback(
      b, "palette", 'P', offsetof(options_t, palette_type), &(palette_type_t){PALETTE_STANDARD}, // Default: standard
      sizeof(palette_type_t), parse_palette_type,
      "ASCII palette type (standard, blocks, digital, minimal, cool, custom)", "DISPLAY", false, "ASCII_CHAT_PALETTE");

  options_builder_add_callback(b, "palette-chars", 'C', offsetof(options_t, palette_custom), "",
                               sizeof(((options_t *)0)->palette_custom), parse_palette_chars,
                               "Custom palette characters (implies --palette=custom)", "DISPLAY", false, NULL);

  options_builder_add_bool(b, "show-capabilities", '\0', offsetof(options_t, show_capabilities),
                           OPT_SHOW_CAPABILITIES_DEFAULT, "Show terminal capabilities and exit", "DISPLAY", false,
                           NULL);

  options_builder_add_bool(b, "utf8", '\0', offsetof(options_t, force_utf8), OPT_FORCE_UTF8_DEFAULT,
                           "Force UTF-8 support", "DISPLAY", false, NULL);

  options_builder_add_bool(b, "stretch", 's', offsetof(options_t, stretch), OPT_STRETCH_DEFAULT,
                           "Allow aspect ratio distortion", "DISPLAY", false, NULL);

  options_builder_add_bool(b, "strip-ansi", '\0', offsetof(options_t, strip_ansi), OPT_STRIP_ANSI_DEFAULT,
                           "Strip ANSI escape sequences", "DISPLAY", false, NULL);

  options_builder_add_int(b, "fps", '\0', offsetof(options_t, fps), 0, "Target framerate (1-144, default: 60)",
                          "DISPLAY", false, "ASCII_CHAT_FPS", NULL);
}

/**
 * @brief Add snapshot mode options (snapshot, snapshot-delay)
 * Used by: client, mirror modes
 */
void options_builder_add_snapshot_group(options_builder_t *b) {
  options_builder_add_bool(b, "snapshot", 'S', offsetof(options_t, snapshot_mode), OPT_SNAPSHOT_MODE_DEFAULT,
                           "Snapshot mode (one frame and exit)", "SNAPSHOT", false, "ASCII_CHAT_SNAPSHOT");

  options_builder_add_double(b, "snapshot-delay", 'D', offsetof(options_t, snapshot_delay), SNAPSHOT_DELAY_DEFAULT,
                             "Snapshot delay in seconds", "SNAPSHOT", false, "ASCII_CHAT_SNAPSHOT_DELAY", NULL);
}

// ============================================================================
// Compression Options Helper (Client + Server)
// ============================================================================

/**
 * @brief Add compression options
 * Used by: client, server modes
 */
void options_builder_add_compression_group(options_builder_t *b) {
  options_builder_add_int(b, "compression-level", '\0', offsetof(options_t, compression_level),
                          OPT_COMPRESSION_LEVEL_DEFAULT, "zstd compression level (1-9)", "PERFORMANCE", false,
                          "ASCII_CHAT_COMPRESSION_LEVEL", NULL);

  options_builder_add_bool(b, "no-compress", '\0', offsetof(options_t, no_compress), OPT_NO_COMPRESS_DEFAULT,
                           "Disable compression", "PERFORMANCE", false, "ASCII_CHAT_NO_COMPRESS");
}

// ============================================================================
// Crypto/Security Options Helper (Client + Server)
// ============================================================================

/**
 * @brief Add encryption and authentication options
 * Common options used by: client, server modes
 * Note: Server has client-keys, client has server-key
 */
void options_builder_add_crypto_group(options_builder_t *b) {
  options_builder_add_bool(b, "encrypt", 'E', offsetof(options_t, encrypt_enabled), OPT_ENCRYPT_ENABLED_DEFAULT,
                           "Enable encryption", "SECURITY", false, NULL);

  options_builder_add_string(b, "key", 'K', offsetof(options_t, encrypt_key), "", "SSH/GPG key file path", "SECURITY",
                             false, "ASCII_CHAT_KEY", NULL);

  options_builder_add_string(b, "password", '\0', offsetof(options_t, password), "",
                             "Shared password for authentication", "SECURITY", false, "ASCII_CHAT_PASSWORD", NULL);

  options_builder_add_bool(b, "no-encrypt", '\0', offsetof(options_t, no_encrypt), OPT_NO_ENCRYPT_DEFAULT,
                           "Disable encryption", "SECURITY", false, NULL);
}

// ============================================================================
// Port Option Helper (Server + Client + ACDS)
// ============================================================================

/**
 * @brief Add port option to a builder
 * Used by: server, client, acds modes
 * @param b Options builder
 * @param default_port Default port value (as string)
 * @param env_var Environment variable name for port override
 */
void options_builder_add_port_option(options_builder_t *b, const char *default_port, const char *env_var) {
  options_builder_add_string(b, "port", 'p', offsetof(options_t, port), default_port, "Server port", "NETWORK", false,
                             env_var, NULL);
}

// ============================================================================
// ACDS Discovery Options Helper (Client + Server)
// ============================================================================

/**
 * @brief Add ACDS options to DISCOVERY section (server/discovery modes only)
 * Note: Client mode uses options_builder_add_acds_network_group instead
 * Used by: server, discovery modes
 */
void options_builder_add_acds_group(options_builder_t *b) {
  options_builder_add_string(
      b, "discovery-server", '\0', offsetof(options_t, discovery_server), OPT_ENDPOINT_DISCOVERY_SERVICE,
      "Discovery service address (default: " OPT_ENDPOINT_DISCOVERY_SERVICE ")", "NETWORK", false, NULL, NULL);

  options_builder_add_int(b, "discovery-port", '\0', offsetof(options_t, discovery_port), OPT_ACDS_PORT_INT_DEFAULT,
                          "Discovery service port", "NETWORK", false, NULL, NULL);

  options_builder_add_string(
      b, "discovery-service-key", '\0', offsetof(options_t, discovery_service_key), "",
      "Discovery server public key for trust verification (SSH/GPG file, HTTPS URL, or github:user/gitlab:user)",
      "SECURITY", false, NULL, NULL);

  options_builder_add_bool(b, "webrtc", '\0', offsetof(options_t, webrtc), OPT_WEBRTC_DEFAULT,
                           "Use WebRTC P2P mode (default: Direct TCP)", "NETWORK", false, NULL);
}

/**
 * @brief Add ACDS connectivity options to NETWORK section (client mode)
 * Adds ACDS server discovery and WebRTC P2P options to NETWORK.
 * The security verification key (--acds-key) should be added separately to SECURITY.
 * Used by: client mode
 */
void options_builder_add_acds_network_group(options_builder_t *b) {
  options_builder_add_string(
      b, "discovery-server", '\0', offsetof(options_t, discovery_server), OPT_ENDPOINT_DISCOVERY_SERVICE,
      "Discovery service address (default: " OPT_ENDPOINT_DISCOVERY_SERVICE ")", "NETWORK", false, NULL, NULL);

  options_builder_add_int(b, "discovery-port", '\0', offsetof(options_t, discovery_port), OPT_ACDS_PORT_INT_DEFAULT,
                          "Discovery service port", "NETWORK", false, NULL, NULL);

  options_builder_add_bool(b, "webrtc", '\0', offsetof(options_t, webrtc), OPT_WEBRTC_DEFAULT,
                           "Use WebRTC P2P mode (default: Direct TCP)", "NETWORK", false, NULL);
}

/**
 * @brief Add WebRTC connection strategy options to a builder
 *
 * Adds connection fallback and optimization flags for WebRTC/TCP selection.
 * Used by: client, discovery modes
 *
 * @param b Builder to add options to
 */
void options_builder_add_webrtc_strategy_group(options_builder_t *b) {
  options_builder_add_bool(b, "prefer-webrtc", '\0', offsetof(options_t, prefer_webrtc), false,
                           "Try WebRTC before Direct TCP (useful when Direct TCP fails)", "NETWORK", false, NULL);

  options_builder_add_bool(b, "no-webrtc", '\0', offsetof(options_t, no_webrtc), false,
                           "Disable WebRTC, use Direct TCP only", "NETWORK", false, NULL);

  options_builder_add_bool(b, "webrtc-skip-stun", '\0', offsetof(options_t, webrtc_skip_stun), false,
                           "Skip WebRTC+STUN stage, go straight to TURN relay", "NETWORK", false, NULL);

  options_builder_add_bool(b, "webrtc-disable-turn", '\0', offsetof(options_t, webrtc_disable_turn), false,
                           "Disable WebRTC+TURN relay, use STUN only", "NETWORK", false, NULL);
}

/**
 * @brief Add ACDS security verification options to a builder
 *
 * Adds --acds-insecure and --acds-key for ACDS server verification.
 * Used by: client, discovery modes
 *
 * @param b Builder to add options to
 */
void options_builder_add_acds_security_group(options_builder_t *b) {
  options_builder_add_bool(b, "discovery-insecure", '\0', offsetof(options_t, discovery_insecure), false,
                           "Skip server key verification (MITM-vulnerable, requires explicit opt-in)", "SECURITY",
                           false, NULL);

  options_builder_add_string(
      b, "discovery-service-key", '\0', offsetof(options_t, discovery_service_key), "",
      "Discovery server public key for trust verification (SSH/GPG file, HTTPS URL, or github:user/gitlab:user)",
      "SECURITY", false, NULL, NULL);
}

// ============================================================================
// Media File Streaming Options Helper (Client + Mirror)
// ============================================================================

/**
 * @brief Add media file streaming options to a builder
 *
 * Adds --file, --url, and --loop options for media file and URL playback.
 * Used by: client, mirror, discovery modes
 */
void options_builder_add_media_group(options_builder_t *b) {
  options_builder_add_string(b, "file", 'f', offsetof(options_t, media_file), "",
                             "Stream from media file or stdin (use '-' for stdin)", "MEDIA", false, NULL, NULL);

  options_builder_add_string(b, "url", 'u', offsetof(options_t, media_url), "",
                             "Stream from network URL (HTTP/HTTPS/YouTube/RTSP) - takes priority over --file", "MEDIA",
                             false, NULL, NULL);

  options_builder_add_bool(b, "loop", 'l', offsetof(options_t, media_loop), false,
                           "Loop media file playback (not supported for network URLs)", "MEDIA", false, NULL);

  options_builder_add_callback(
      b, "seek", 's', offsetof(options_t, media_seek_timestamp), &(double){0.0}, // Default: no seeking
      sizeof(double), parse_timestamp, "Seek to timestamp before playback (format: seconds, MM:SS, or HH:MM:SS.ms)",
      "MEDIA", false, NULL);

  options_builder_add_callback_optional(
      b, "cookies-from-browser", '\0', offsetof(options_t, cookies_from_browser),
      NULL, // Default: empty (will use chrome as fallback)
      256,  // Value size: char[256]
      parse_cookies_from_browser,
      "Browser for reading cookies from (chrome, firefox, edge, safari, brave, opera, vivaldi, whale). "
      "Use without argument to default to chrome.",
      "MEDIA", false, NULL, true); // optional_arg=true

  options_builder_add_bool(b, "no-cookies-from-browser", '\0', offsetof(options_t, no_cookies_from_browser), false,
                           "Explicitly disable reading cookies from browser", "MEDIA", false, NULL);

  // Make --cookies-from-browser and --no-cookies-from-browser mutually exclusive
  options_builder_add_dependency_conflicts(
      b, "cookies-from-browser", "no-cookies-from-browser",
      "--cookies-from-browser and --no-cookies-from-browser are mutually exclusive");
  options_builder_add_dependency_conflicts(
      b, "no-cookies-from-browser", "cookies-from-browser",
      "--no-cookies-from-browser and --cookies-from-browser are mutually exclusive");
}

// ============================================================================
// Audio Streaming Options Helper (Client only)
// ============================================================================

/**
 * @brief Add audio streaming options to a builder
 *
 * Adds --audio, --microphone-index, --speakers-index, and volume options.
 * Used by: client, discovery modes
 */
void options_builder_add_audio_group(options_builder_t *b) {
  options_builder_add_bool(b, "audio", 'A', offsetof(options_t, audio_enabled), OPT_AUDIO_ENABLED_DEFAULT,
                           "Enable audio streaming", "AUDIO", false, "ASCII_CHAT_AUDIO");

  options_builder_add_int(b, "microphone-index", '\0', offsetof(options_t, microphone_index),
                          OPT_MICROPHONE_INDEX_DEFAULT, "Microphone device index (-1=default)", "AUDIO", false,
                          "ASCII_CHAT_MICROPHONE_INDEX", NULL);

  options_builder_add_int(b, "speakers-index", '\0', offsetof(options_t, speakers_index), OPT_SPEAKERS_INDEX_DEFAULT,
                          "Speakers device index (-1=default)", "AUDIO", false, "ASCII_CHAT_SPEAKERS_INDEX", NULL);

  options_builder_add_double(b, "microphone-sensitivity", '\0', offsetof(options_t, microphone_sensitivity),
                             OPT_MICROPHONE_SENSITIVITY_DEFAULT, "Microphone volume multiplier (0.0-1.0)", "AUDIO",
                             false, "ASCII_CHAT_MICROPHONE_SENSITIVITY", NULL);

  options_builder_add_double(b, "speakers-volume", '\0', offsetof(options_t, speakers_volume),
                             OPT_SPEAKERS_VOLUME_DEFAULT, "Speaker volume multiplier (0.0-1.0)", "AUDIO", false,
                             "ASCII_CHAT_SPEAKERS_VOLUME", NULL);

  options_builder_add_bool(b, "audio-analysis", '\0', offsetof(options_t, audio_analysis_enabled),
                           OPT_AUDIO_ANALYSIS_ENABLED_DEFAULT, "Enable audio analysis (debug)", "AUDIO", false,
                           "ASCII_CHAT_AUDIO_ANALYSIS");

  options_builder_add_bool(b, "no-audio-playback", '\0', offsetof(options_t, audio_no_playback),
                           OPT_AUDIO_NO_PLAYBACK_DEFAULT, "Disable speaker playback (debug)", "AUDIO", false,
                           "ASCII_CHAT_NO_AUDIO_PLAYBACK");

  options_builder_add_bool(b, "encode-audio", '\0', offsetof(options_t, encode_audio), OPT_ENCODE_AUDIO_DEFAULT,
                           "Enable Opus audio encoding", "AUDIO", false, "ASCII_CHAT_ENCODE_AUDIO");

  options_builder_add_bool(b, "no-encode-audio", '\0', offsetof(options_t, encode_audio), !OPT_ENCODE_AUDIO_DEFAULT,
                           "Disable Opus audio encoding", "AUDIO", false, "ASCII_CHAT_NO_ENCODE_AUDIO");
}

// ============================================================================
// Positional Argument Examples (Programmatic Help)
// ============================================================================

/**
 * @brief Server bind address format examples
 *
 * Shows how to specify bind addresses for server mode (0-2 addresses).
 */
static const char *g_server_bind_address_examples[] = {
    "(none)                     bind to 127.0.0.1 and ::1 (localhost)",
    "192.168.1.100              bind to IPv4 address only",
    "::                         bind to all IPv6 addresses",
    "0.0.0.0                    bind to all IPv4 addresses",
    "2001:db8::1                bind to specific IPv6 address only",
    "192.168.1.100 ::           bind to IPv4 and IPv6 (dual-stack)"};

/**
 * @brief Client address format examples
 *
 * Shows how to specify server addresses for client mode.
 */
static const char *g_client_address_examples[] = {
    "(none)                     connect to localhost:27224",
    "hostname                   connect to hostname:27224",
    "hostname:port              connect to hostname:port",
    "192.168.1.1                connect to IPv4:27224",
    "192.168.1.1:8080           connect to IPv4:port",
    "::1                        connect to IPv6:27224",
    "[::1]:8080                 connect to IPv6:port (brackets required with port)"};

// ============================================================================
// Unified Options Preset (NEW - Phase 3)
// ============================================================================

/**
 * @brief Build unified options config with ALL options (binary + all modes)
 *
 * This is the single source of truth for all options. Each option has a
 * mode_bitmask indicating which modes it applies to. The config includes
 * all options, and validation happens after parsing based on detected mode.
 */
const options_config_t *options_preset_unified(const char *program_name, const char *description) {
  options_builder_t *b = options_builder_create(sizeof(options_t));
  if (!b)
    return NULL;

  b->program_name = program_name ? program_name : "ascii-chat";
  b->description = description ? description : "Video chat in your terminal";

  // Add ALL options from registry (binary + all modes)
  asciichat_error_t err = options_registry_add_all_to_builder(b);
  if (err != ASCIICHAT_OK) {
    options_builder_destroy(b);
    return NULL;
  }

  // Add usage lines for all modes
  options_builder_add_usage(b, NULL, NULL, true, "Start a new session (share the session string)");
  options_builder_add_usage(b, NULL, "<session-string>", true, "Join an existing session");
  options_builder_add_usage(b, "<mode>", NULL, true, "Run in a specific mode");
  options_builder_add_usage(b, "server", "[bind-address] [bind-address]", true,
                            "Start server (can specify 0-2 bind addresses for IPv4/IPv6)");
  options_builder_add_usage(b, "client", "[address]", true, "Connect to server (defaults to localhost:27224)");
  options_builder_add_usage(b, "mirror", NULL, true, "View local webcam or media file as ASCII art");
  options_builder_add_usage(b, "discovery-service", "[bind-address] [bind-address]", true,
                            "Start discovery service (can specify 0-2 bind addresses for IPv4/IPv6)");
  options_builder_add_usage(b, "discovery", "<session-string>", true,
                            "Join a discovery session (use session string from initiator)");

  // Add examples for binary-level help
  options_builder_add_example(b, NULL, NULL, "Start new session (share the session string)");
  options_builder_add_example(b, NULL, "swift-river-mountain", "Join session with session string");
  options_builder_add_example(b, "server", NULL, "Run as dedicated server");
  options_builder_add_example(b, "client", "example.com", "Connect to specific server");
  options_builder_add_example(b, "mirror", NULL, "Preview local webcam as ASCII");

  // Add examples for server mode
  options_builder_add_example(b, "server", NULL, "Start on localhost (127.0.0.1 and ::1)");
  options_builder_add_example(b, "server", "0.0.0.0", "Start on all IPv4 interfaces");
  options_builder_add_example(b, "server", "0.0.0.0 ::", "Start on all IPv4 and IPv6 interfaces (dual-stack)");
  options_builder_add_example(b, "server", "--port 8080", "Start on custom port");
  options_builder_add_example(b, "server", "--key ~/.ssh/id_ed25519 --discovery",
                              "Start with identity key and discovery registration");

  // Add examples for client mode
  options_builder_add_example(b, "client", NULL, "Connect to localhost");
  options_builder_add_example(b, "client", "example.com", "Connect to remote server");
  options_builder_add_example(b, "client", "example.com:8080", "Connect to remote server on custom port");
  options_builder_add_example(b, "client", "--url 'https://www.youtube.com/watch?v=tQSbms5MDvY'",
                              "Stream from YouTube URL");
  options_builder_add_example(b, "client", "-f video.mp4", "Stream from local video file");
  options_builder_add_example(b, "client", "--color-mode mono --render-mode half-block --width 120",
                              "Connect with custom display options");
  options_builder_add_example(b, "client", "--palette-chars '@%#*+=-:. '", "Use custom ASCII palette characters");
  options_builder_add_example(b, "client", "--snapshot", "Capture single frame and exit");

  // Add examples for mirror mode
  options_builder_add_example(b, "mirror", NULL, "View local webcam");
  options_builder_add_example(b, "mirror", "--color-mode mono", "View webcam in black and white");
  options_builder_add_example(b, "mirror", "--url 'https://www.youtube.com/watch?v=tQSbms5MDvY'",
                              "Stream from YouTube URL");
  options_builder_add_example(b, "mirror", "-f video.mp4",
                              "Stream from local video file (supports mp4, mkv, webm, mov, etc)");
  options_builder_add_example(b, "mirror", "-f '-'",
                              "Stream media from stdin (cat file.gif | ascii-chat mirror -f '-')");
  options_builder_add_example(b, "mirror", "--palette-chars '@%#*+=-:. '", "View with custom ASCII palette characters");
  options_builder_add_example(b, "mirror", "--snapshot", "Capture single frame and exit");

  // Add examples for discovery-service mode
  options_builder_add_example(b, "discovery-service", NULL, "Start on localhost");
  options_builder_add_example(b, "discovery-service", "0.0.0.0", "Start on all IPv4 interfaces");
  options_builder_add_example(b, "discovery-service", "--port 5000", "Start on custom port");
  options_builder_add_example(b, "discovery-service", "--require-server-identity --require-client-identity",
                              "Enforce identity verification for all parties");

  // Add examples for discovery mode
  options_builder_add_example(b, "discovery", "swift-river-mountain", "Join a session using the session string");
  options_builder_add_example(b, "discovery", "swift-river-mountain --color", "Join session with color support");
  options_builder_add_example(b, "discovery", "swift-river-mountain --discovery-server discovery.example.com",
                              "Join session via custom discovery server");
  options_builder_add_example(b, "discovery", "swift-river-mountain -f video.mp4",
                              "Join session and stream from local video file");
  options_builder_add_example(b, "discovery",
                              "swift-river-mountain --url 'https://www.youtube.com/watch?v=tQSbms5MDvY'",
                              "Join session and stream from YouTube video");
  options_builder_add_example(
      b, "discovery", "swift-river-mountain -f '-'",
      "Join session and stream media from stdin (cat file.mov | ascii-chat discovery ... -f '-')");
  options_builder_add_example(b, "discovery", "swift-river-mountain --palette-chars '@%#*+=-:. '",
                              "Join session with custom ASCII palette characters");

  // Add mode descriptions
  options_builder_add_mode(b, "server", "Run as multi-client video chat server");
  options_builder_add_mode(b, "client", "Run as video chat client (connect to server)");
  options_builder_add_mode(b, "mirror", "View local webcam as ASCII art (no server)");
  options_builder_add_mode(b, "discovery-service", "Secure P2P session signalling");
  options_builder_add_mode(b, "discovery", "Join a discovery session using a session string");

  // Add common dependencies (these will be validated after parsing)
  // Note: Dependencies are validated at runtime, so we add them here for documentation
  // URL conflicts: --url cannot be used with --file or --loop
  options_builder_add_dependency_conflicts(b, "url", "file",
                                           "Option --url cannot be used with --file (--url takes priority)");
  options_builder_add_dependency_conflicts(b, "url", "loop",
                                           "Option --url cannot be used with --loop (network streams cannot be looped)");
  // Encryption conflicts
  options_builder_add_dependency_conflicts(b, "no-encrypt", "encrypt", "Cannot use --no-encrypt with --encrypt");
  options_builder_add_dependency_conflicts(b, "no-encrypt", "key", "Cannot use --no-encrypt with --key");
  options_builder_add_dependency_conflicts(b, "no-encrypt", "password", "Cannot use --no-encrypt with --password");
  // Compression conflicts
  options_builder_add_dependency_conflicts(b, "no-compress", "compression-level",
                                           "Cannot use --no-compress with --compression-level");
  // Audio encoding conflicts
  options_builder_add_dependency_conflicts(b, "encode-audio", "no-encode-audio",
                                           "Cannot use both --encode-audio and --no-encode-audio");
  // Cookies conflicts
  options_builder_add_dependency_conflicts(b, "cookies-from-browser", "no-cookies-from-browser",
                                           "--cookies-from-browser and --no-cookies-from-browser are mutually exclusive");
  options_builder_add_dependency_conflicts(b, "no-cookies-from-browser", "cookies-from-browser",
                                           "--no-cookies-from-browser and --cookies-from-browser are mutually exclusive");
  // Requirements
  options_builder_add_dependency_requires(b, "snapshot-delay", "snapshot",
                                          "Option --snapshot-delay requires --snapshot");
  options_builder_add_dependency_requires(b, "loop", "file", "Option --loop requires --file");

  const options_config_t *config = options_builder_build(b);
  options_builder_destroy(b);
  return config;
}

