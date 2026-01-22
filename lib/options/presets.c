/**
 * @file presets.c
 * @brief Preset option configurations for ascii-chat modes
 * @ingroup options
 */

#include "builder.h"
#include "options.h"
#include "parsers.h"
#include "actions.h"
#include "common.h"
#include "platform/terminal.h"
#include "video/palette.h"
#include "log/logging.h"

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
 * @brief Add binary-level logging options to a builder
 *
 * This ensures consistent option definitions across all modes.
 * These options can be used before OR after the mode name.
 */
void options_builder_add_logging_group(options_builder_t *b) {
  options_builder_add_string(b, "log-file", 'L', offsetof(options_t, log_file), "", "Redirect logs to FILE", "LOGGING",
                             false, "ASCII_CHAT_LOG_FILE", NULL);

  options_builder_add_callback(b, "log-level", '\0', offsetof(options_t, log_level),
                               &(log_level_t){LOG_INFO}, // Default: info level
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
                          "Terminal width in characters", "TERMINAL", false, NULL, NULL);

  options_builder_add_int(b, "height", 'y', offsetof(options_t, height), OPT_HEIGHT_DEFAULT,
                          "Terminal height in characters", "TERMINAL", false, NULL, NULL);
}

/**
 * @brief Add webcam options (device selection, flipping, test pattern)
 * Used by: client, mirror modes
 */
void options_builder_add_webcam_group(options_builder_t *b) {
  options_builder_add_int(b, "webcam-index", 'c', offsetof(options_t, webcam_index), OPT_WEBCAM_INDEX_DEFAULT,
                          "Webcam device index", "WEBCAM", false, NULL, NULL);

  options_builder_add_bool(b, "webcam-flip", 'f', offsetof(options_t, webcam_flip), OPT_WEBCAM_FLIP_DEFAULT,
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
                               "Terminal color level (auto, none, 16, 256, truecolor)", "DISPLAY", false, NULL);

  options_builder_add_callback(b, "render-mode", 'M', offsetof(options_t, render_mode),
                               &(render_mode_t){RENDER_MODE_FOREGROUND}, // Default: foreground
                               sizeof(render_mode_t), parse_render_mode,
                               "Render mode (foreground, background, half-block)", "DISPLAY", false, NULL);

  options_builder_add_callback(
      b, "palette", 'P', offsetof(options_t, palette_type), &(palette_type_t){PALETTE_STANDARD}, // Default: standard
      sizeof(palette_type_t), parse_palette_type,
      "ASCII palette type (standard, blocks, digital, minimal, cool, custom)", "DISPLAY", false, NULL);

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
                          "DISPLAY", false, NULL, NULL);
}

/**
 * @brief Add snapshot mode options (snapshot, snapshot-delay)
 * Used by: client, mirror modes
 */
void options_builder_add_snapshot_group(options_builder_t *b) {
  options_builder_add_bool(b, "snapshot", 'S', offsetof(options_t, snapshot_mode), OPT_SNAPSHOT_MODE_DEFAULT,
                           "Snapshot mode (one frame and exit)", "SNAPSHOT", false, NULL);

  options_builder_add_double(b, "snapshot-delay", 'D', offsetof(options_t, snapshot_delay), SNAPSHOT_DELAY_DEFAULT,
                             "Snapshot delay in seconds", "SNAPSHOT", false, NULL, NULL);
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
                          OPT_COMPRESSION_LEVEL_DEFAULT, "zstd compression level (1-9)", "PERFORMANCE", false, NULL,
                          NULL);

  options_builder_add_bool(b, "no-compress", '\0', offsetof(options_t, no_compress), OPT_NO_COMPRESS_DEFAULT,
                           "Disable compression", "PERFORMANCE", false, NULL);
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
 * Adds --file and --loop options for media file playback.
 * Used by: client, mirror, discovery modes
 */
void options_builder_add_media_group(options_builder_t *b) {
  options_builder_add_string(b, "file", 'f', offsetof(options_t, media_file), "",
                             "Stream from media file or stdin (use '-' for stdin)", "MEDIA", false, NULL, NULL);

  options_builder_add_bool(b, "loop", 'l', offsetof(options_t, media_loop), false,
                           "Loop media file playback (not supported for stdin)", "MEDIA", false, NULL);
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
                           "Enable audio streaming", "AUDIO", false, NULL);

  options_builder_add_int(b, "microphone-index", '\0', offsetof(options_t, microphone_index),
                          OPT_MICROPHONE_INDEX_DEFAULT, "Microphone device index (-1=default)", "AUDIO", false, NULL,
                          NULL);

  options_builder_add_int(b, "speakers-index", '\0', offsetof(options_t, speakers_index), OPT_SPEAKERS_INDEX_DEFAULT,
                          "Speakers device index (-1=default)", "AUDIO", false, NULL, NULL);

  options_builder_add_double(b, "microphone-sensitivity", '\0', offsetof(options_t, microphone_sensitivity),
                             OPT_MICROPHONE_SENSITIVITY_DEFAULT, "Microphone volume multiplier (0.0-1.0)", "AUDIO",
                             false, NULL, NULL);

  options_builder_add_double(b, "speakers-volume", '\0', offsetof(options_t, speakers_volume),
                             OPT_SPEAKERS_VOLUME_DEFAULT, "Speaker volume multiplier (0.0-1.0)", "AUDIO", false, NULL,
                             NULL);

  options_builder_add_bool(b, "audio-analysis", '\0', offsetof(options_t, audio_analysis_enabled),
                           OPT_AUDIO_ANALYSIS_ENABLED_DEFAULT, "Enable audio analysis (debug)", "AUDIO", false, NULL);

  options_builder_add_bool(b, "no-audio-playback", '\0', offsetof(options_t, audio_no_playback),
                           OPT_AUDIO_NO_PLAYBACK_DEFAULT, "Disable speaker playback (debug)", "AUDIO", false, NULL);

  options_builder_add_bool(b, "encode-audio", '\0', offsetof(options_t, encode_audio), OPT_ENCODE_AUDIO_DEFAULT,
                           "Enable Opus audio encoding", "AUDIO", false, NULL);

  options_builder_add_bool(b, "no-encode-audio", '\0', offsetof(options_t, encode_audio), !OPT_ENCODE_AUDIO_DEFAULT,
                           "Disable Opus audio encoding", "AUDIO", false, NULL);
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
// Binary-Level Options Preset
// ============================================================================

const options_config_t *options_preset_binary(const char *program_name, const char *description) {
  // Note: Each call creates a new config (no static caching) since program_name/description vary
  options_builder_t *b = options_builder_create(sizeof(options_t));
  if (!b)
    return NULL;

  b->program_name = program_name ? program_name : "ascii-chat";
  b->description = description ? description : "Video chat in your terminal";

  // Help and version
  options_builder_add_bool(b, "help", '\0', offsetof(options_t, help), false, "Show this help", "GENERAL", false, NULL);

  options_builder_add_bool(b, "version", '\0', offsetof(options_t, version), false, "Show version information",
                           "GENERAL", false, NULL);

  // Add logging options
  options_builder_add_logging_group(b);

  // Add WebRTC strategy options
  options_builder_add_webrtc_strategy_group(b);

  // Server-specific options
  options_builder_add_port_option(b, OPT_PORT_DEFAULT, "ASCII_CHAT_PORT");
  options_builder_add_int(b, "max-clients", '\0', offsetof(options_t, max_clients), OPT_MAX_CLIENTS_DEFAULT,
                          "Maximum concurrent clients", "NETWORK", false, "ASCII_CHAT_MAX_CLIENTS", NULL);
  options_builder_add_compression_group(b);
  options_builder_add_bool(b, "no-audio-mixer", '\0', offsetof(options_t, no_audio_mixer), false,
                           "Disable audio mixer (debug)", "NETWORK", false, NULL);

  // Security options (common with client)
  options_builder_add_crypto_group(b);
  options_builder_add_string(b, "client-keys", '\0', offsetof(options_t, client_keys), "",
                             "Allowed client keys whitelist", "SECURITY", false, NULL, NULL);
  options_builder_add_bool(b, "discovery-expose-ip", '\0', offsetof(options_t, discovery_expose_ip), false,
                           "Explicitly allow public IP disclosure in discovery sessions (opt-in only)", "SECURITY",
                           false, NULL);
  options_builder_add_string(
      b, "discovery-service-key", '\0', offsetof(options_t, discovery_service_key), "",
      "Discovery service public key for verification (SSH/GPG file, HTTPS URL, or github:user/gitlab:user)", "SECURITY",
      false, NULL, NULL);

  // Discovery/ACDS options with discovery naming (all in NETWORK, no separate DISCOVERY section)
  options_builder_add_bool(
      b, "discovery", '\0', offsetof(options_t, discovery), false,
      "Enable discovery service registration (requires --key or --password or --discovery-expose-ip)", "NETWORK", false,
      NULL);
  options_builder_add_string(b, "discovery-server", '\0', offsetof(options_t, discovery_server),
                             "discovery-service.ascii-chat.com", "Discovery service address", "NETWORK", false, NULL,
                             NULL);
  options_builder_add_int(b, "discovery-port", '\0', offsetof(options_t, discovery_port), 27225,
                          "Discovery service port", "NETWORK", false, NULL, NULL);
  options_builder_add_bool(b, "upnp", '\0', offsetof(options_t, enable_upnp), false,
                           "Enable UPnP/NAT-PMP for automatic router port mapping (direct TCP for ~70%% of home users)",
                           "NETWORK", false, "ASCII_CHAT_UPNP");
  options_builder_add_bool(b, "no-mdns-advertise", '\0', offsetof(options_t, no_mdns_advertise), false,
                           "Disable mDNS service advertisement on local network (LAN discovery won't find this server)",
                           "NETWORK", false, NULL);

  // Client-specific options (terminal, webcam, display, media, audio, snapshot)
  options_builder_add_terminal_group(b);
  options_builder_add_webcam_group(b);
  options_builder_add_display_group(b);
  options_builder_add_snapshot_group(b);
  options_builder_add_media_group(b);
  options_builder_add_audio_group(b);

  // WebRTC server configuration
  options_builder_add_string(b, "stun-servers", '\0', offsetof(options_t, stun_servers), "",
                             "Comma-separated STUN server URLs (debug/test only - ACDS provides in production)",
                             "NETWORK", false, "ASCII_CHAT_STUN_SERVERS", NULL);
  options_builder_add_string(b, "turn-servers", '\0', offsetof(options_t, turn_servers), "",
                             "Comma-separated TURN server URLs (debug/test only - ACDS provides in production)",
                             "NETWORK", false, "ASCII_CHAT_TURN_SERVERS", NULL);
  options_builder_add_string(b, "turn-username", '\0', offsetof(options_t, turn_username), "",
                             "TURN authentication username (debug/test only - ACDS provides in production)", "NETWORK",
                             false, "ASCII_CHAT_TURN_USERNAME", NULL);
  options_builder_add_string(b, "turn-credential", '\0', offsetof(options_t, turn_credential), "",
                             "TURN authentication credential (debug/test only - ACDS provides in production)",
                             "NETWORK", false, "ASCII_CHAT_TURN_CREDENTIAL", NULL);

  // Usage lines for help output
  options_builder_add_usage(b, NULL, NULL, true, "Start a new session (share the session string)");

  options_builder_add_usage(b, NULL, "<session-string>", true, "Join an existing session");

  options_builder_add_usage(b, "<mode>", NULL, true, "Run in a specific mode");

  // Examples for help output
  options_builder_add_example(b, NULL, NULL, "Start new session (share the session string)");

  options_builder_add_example(b, NULL, "swift-river-mountain", "Join session with session string");

  options_builder_add_example(b, "server", NULL, "Run as dedicated server");

  options_builder_add_example(b, "client", "example.com", "Connect to specific server");

  options_builder_add_example(b, "mirror", NULL, "Preview local webcam as ASCII");

  // Modes for help output
  options_builder_add_mode(b, "server", "Run as multi-client video chat server");
  options_builder_add_mode(b, "client", "Run as video chat client (connect to server)");
  options_builder_add_mode(b, "mirror", "View local webcam as ASCII art (no server)");
  options_builder_add_mode(b, "discovery-service", "Secure P2P session signalling");

  const options_config_t *config = options_builder_build(b);
  options_builder_destroy(b);
  return config;
}

// ============================================================================
// Server Mode Options Preset
// ============================================================================

const options_config_t *options_preset_server(const char *program_name, const char *description) {
  // Note: Each call creates a new config (no static caching) since program_name/description vary
  options_builder_t *b = options_builder_create(sizeof(options_t));
  if (!b)
    return NULL;

  b->program_name = program_name ? program_name : "ascii-chat server";
  b->description = description ? description : "Start ascii-chat server";

  // Action options (GENERAL - add first so it appears first in help)
  options_builder_add_action(b, "help", 'h', action_help_server, "Show this help message and exit", "GENERAL");

  // Network options
  // Note: Server bind addresses are positional arguments only, not flags
  options_builder_add_port_option(b, OPT_PORT_DEFAULT, "ASCII_CHAT_PORT");

  options_builder_add_int(b, "max-clients", '\0', offsetof(options_t, max_clients), OPT_MAX_CLIENTS_DEFAULT,
                          "Maximum concurrent clients", "NETWORK", false, "ASCII_CHAT_MAX_CLIENTS", NULL);

  // Compression and audio encoding options (shared with client)
  options_builder_add_compression_group(b);

  options_builder_add_bool(b, "no-audio-mixer", '\0', offsetof(options_t, no_audio_mixer), false,
                           "Disable audio mixer (debug)", "NETWORK", false, NULL);

  // Security options (common with client, plus server-specific client-keys)
  options_builder_add_crypto_group(b);

  options_builder_add_string(b, "client-keys", '\0', offsetof(options_t, client_keys), "",
                             "Allowed client keys whitelist", "SECURITY", false, NULL, NULL);

  options_builder_add_bool(
      b, "discovery-expose-ip", '\0', offsetof(options_t, discovery_expose_ip), false,
      "Explicitly allow public IP disclosure in discovery sessions (requires discovery, opt-in only)", "SECURITY",
      false, NULL);

  // Discovery Session Registration
  options_builder_add_bool(
      b, "discovery", '\0', offsetof(options_t, discovery), false,
      "Enable discovery session registration (requires --key or --password or --discovery-expose-ip)", "NETWORK", false,
      NULL);

  // Discovery service options (shared with client)
  options_builder_add_acds_group(b);

  options_builder_add_bool(b, "upnp", '\0', offsetof(options_t, enable_upnp), false,
                           "Enable UPnP/NAT-PMP for automatic router port mapping (direct TCP for ~70%% of home users)",
                           "NETWORK", false, "ASCII_CHAT_UPNP");

  options_builder_add_bool(b, "no-mdns-advertise", '\0', offsetof(options_t, no_mdns_advertise), false,
                           "Disable mDNS service advertisement on local network (LAN discovery won't find this server)",
                           "NETWORK", false, NULL);

  // Dependencies
  options_builder_add_dependency_conflicts(b, "no-encrypt", "encrypt", "Cannot use --no-encrypt with --encrypt");
  options_builder_add_dependency_conflicts(b, "no-encrypt", "key", "Cannot use --no-encrypt with --key");
  options_builder_add_dependency_conflicts(b, "no-encrypt", "password", "Cannot use --no-encrypt with --password");
  options_builder_add_dependency_conflicts(b, "no-compress", "compression-level",
                                           "Cannot use --no-compress with --compression-level");
  options_builder_add_dependency_conflicts(b, "encode-audio", "no-encode-audio",
                                           "Cannot use both --encode-audio and --no-encode-audio");

  // Positional arguments: 0-2 bind addresses (IPv4 and/or IPv6)
  options_builder_add_positional(b, "bind-address", "IPv4 or IPv6 bind address (can specify 0-2 addresses)",
                                 false, // Not required (defaults to localhost)
                                 "BIND ADDRESS FORMATS", g_server_bind_address_examples,
                                 sizeof(g_server_bind_address_examples) / sizeof(g_server_bind_address_examples[0]),
                                 parse_server_bind_address);

  // Usage examples for server help
  options_builder_add_usage(b, "server", "[bind-address...]", true,
                            "Start server (can specify 0-2 bind addresses for IPv4/IPv6)");

  // Examples for server help
  options_builder_add_example(b, "server", NULL, "Start on localhost (127.0.0.1 and ::1)");

  options_builder_add_example(b, "server", "0.0.0.0", "Start on all IPv4 interfaces");

  options_builder_add_example(b, "server", "0.0.0.0 ::", "Start on all IPv4 and IPv6 interfaces (dual-stack)");

  options_builder_add_example(b, "server", "--port 8080", "Start on custom port");

  options_builder_add_example(b, "server", "--key ~/.ssh/id_ed25519 --discovery",
                              "Start with identity key and discovery registration");

  const options_config_t *config = options_builder_build(b);
  options_builder_destroy(b);
  return config;
}

// ============================================================================
// Client Mode Options Preset
// ============================================================================

const options_config_t *options_preset_client(const char *program_name, const char *description) {
  // Note: Each call creates a new config (no static caching) since program_name/description vary
  options_builder_t *b = options_builder_create(sizeof(options_t));
  if (!b)
    return NULL;

  b->program_name = program_name ? program_name : "ascii-chat client";
  b->description = description ? description : "Connect to ascii-chat server";

  // Action options (GENERAL - add first so it appears first in help)
  options_builder_add_action(b, "help", 'h', action_help_client, "Show this help message and exit", "GENERAL");

  // Network options
  // Note: Server address and port are specified via positional argument [address][:port], not flags
  options_builder_add_port_option(b, OPT_PORT_DEFAULT, "ASCII_CHAT_PORT");

  options_builder_add_int(b, "reconnect", 'r', offsetof(options_t, reconnect_attempts), OPT_RECONNECT_ATTEMPTS_DEFAULT,
                          "Reconnection attempts (-1=infinite)", "NETWORK", false, NULL, NULL);

  options_builder_add_bool(b, "scan", '\0', offsetof(options_t, lan_discovery), false,
                           "Scan for ascii-chat servers on local network (mDNS)", "NETWORK", false, NULL);

  // Terminal dimensions, webcam, display, and snapshot options (shared with mirror)
  options_builder_add_terminal_group(b);
  options_builder_add_webcam_group(b);
  options_builder_add_display_group(b);
  options_builder_add_snapshot_group(b);

  // Media file streaming options (shared with mirror)
  options_builder_add_media_group(b);

  // Audio options (client only)
  options_builder_add_audio_group(b);

  // ACDS Discovery options in NETWORK section (client doesn't need separate DISCOVERY section)
  options_builder_add_acds_network_group(b);

  // WebRTC Connection Strategy Options (Phase 3 fallback control)
  options_builder_add_webrtc_strategy_group(b);

  // WebRTC Server Configuration (for testing/debugging - production uses ACDS)
  // Note: In production, ACDS provides these automatically via SESSION_JOINED response
  options_builder_add_string(b, "stun-servers", '\0', offsetof(options_t, stun_servers), "",
                             "Comma-separated STUN server URLs (debug/test only - ACDS provides in production)",
                             "NETWORK", false, "ASCII_CHAT_STUN_SERVERS", NULL);

  options_builder_add_string(b, "turn-servers", '\0', offsetof(options_t, turn_servers), "",
                             "Comma-separated TURN server URLs (debug/test only - ACDS provides in production)",
                             "NETWORK", false, "ASCII_CHAT_TURN_SERVERS", NULL);

  options_builder_add_string(b, "turn-username", '\0', offsetof(options_t, turn_username), "",
                             "TURN authentication username (debug/test only - ACDS provides in production)", "NETWORK",
                             false, "ASCII_CHAT_TURN_USERNAME", NULL);

  options_builder_add_string(b, "turn-credential", '\0', offsetof(options_t, turn_credential), "",
                             "TURN authentication credential (debug/test only - ACDS provides in production)",
                             "NETWORK", false, "ASCII_CHAT_TURN_CREDENTIAL", NULL);

  // Compression and audio encoding options (shared with server)
  options_builder_add_compression_group(b);

  // Security options (common with server, plus client-specific server-key)
  options_builder_add_crypto_group(b);

  options_builder_add_string(b, "server-key", '\0', offsetof(options_t, server_key), "", "Expected server public key",
                             "SECURITY", false, NULL, NULL);

  // ACDS security verification options
  options_builder_add_acds_security_group(b);

  // Dependencies
  options_builder_add_dependency_requires(b, "snapshot-delay", "snapshot",
                                          "Option --snapshot-delay requires --snapshot");
  options_builder_add_dependency_requires(b, "loop", "file", "Option --loop requires --file");
  options_builder_add_dependency_conflicts(b, "no-compress", "compression-level",
                                           "Cannot use --no-compress with --compression-level");
  options_builder_add_dependency_conflicts(b, "encode-audio", "no-encode-audio",
                                           "Cannot use both --encode-audio and --no-encode-audio");
  options_builder_add_dependency_conflicts(b, "no-encrypt", "encrypt", "Cannot use --no-encrypt with --encrypt");
  options_builder_add_dependency_conflicts(b, "no-encrypt", "key", "Cannot use --no-encrypt with --key");
  options_builder_add_dependency_conflicts(b, "no-encrypt", "password", "Cannot use --no-encrypt with --password");

  // Webcam options
  options_builder_add_action(b, "list-webcams", '\0', action_list_webcams, "List available webcam devices and exit",
                             "WEBCAM");

  // Audio options
  options_builder_add_action(b, "list-microphones", '\0', action_list_microphones,
                             "List available microphone devices and exit", "AUDIO");

  options_builder_add_action(b, "list-speakers", '\0', action_list_speakers, "List available speaker devices and exit",
                             "AUDIO");

  // Terminal options
  options_builder_add_action(b, "show-capabilities", '\0', action_show_capabilities,
                             "Show terminal capabilities and exit", "TERMINAL");

  // Positional argument: [address][:port]
  options_builder_add_positional(
      b, "address", "[address][:port] - Server address (IPv4, IPv6, or hostname) with optional port",
      false, // Not required (defaults to localhost:27224)
      "ADDRESS FORMATS", g_client_address_examples,
      sizeof(g_client_address_examples) / sizeof(g_client_address_examples[0]), parse_client_address);

  // Usage examples for client help
  options_builder_add_usage(b, "client", "[address]", true, "Connect to server (defaults to localhost:27224)");

  // Examples for client help
  options_builder_add_example(b, "client", NULL, "Connect to localhost");

  options_builder_add_example(b, "client", "example.com", "Connect to remote server");

  options_builder_add_example(b, "client", "example.com:8080", "Connect to remote server on custom port");

  options_builder_add_example(b, "client", "--color-mode mono --render-mode half-block --width 120",
                              "Connect with custom display options");

  options_builder_add_example(b, "client", "-f '-'",
                              "Stream media from stdin (cat file.mp4 | ascii-chat client -f '-')");

  options_builder_add_example(b, "client", "--snapshot", "Capture single frame and exit");

  const options_config_t *config = options_builder_build(b);
  options_builder_destroy(b);
  return config;
}

// ============================================================================
// Mirror Mode Options Preset
// ============================================================================

const options_config_t *options_preset_mirror(const char *program_name, const char *description) {
  // Note: Each call creates a new config (no static caching) since program_name/description vary
  options_builder_t *b = options_builder_create(sizeof(options_t));
  if (!b)
    return NULL;

  b->program_name = program_name ? program_name : "ascii-chat mirror";
  b->description = description ? description : "Local webcam viewing (no network)";

  // Action options (GENERAL - add first so it appears first in help)
  options_builder_add_action(b, "help", 'h', action_help_mirror, "Show this help message and exit", "GENERAL");

  // Terminal dimensions, webcam, display, and snapshot options (shared with client)
  options_builder_add_terminal_group(b);
  options_builder_add_webcam_group(b);
  options_builder_add_display_group(b);
  options_builder_add_snapshot_group(b);

  // Media file streaming options (shared with client)
  options_builder_add_media_group(b);

  // Dependencies
  options_builder_add_dependency_requires(b, "snapshot-delay", "snapshot",
                                          "Option --snapshot-delay requires --snapshot");

  options_builder_add_dependency_requires(b, "loop", "file", "Option --loop requires --file");

  // Webcam options
  options_builder_add_action(b, "list-webcams", '\0', action_list_webcams, "List available webcam devices and exit",
                             "WEBCAM");

  // Terminal options
  options_builder_add_action(b, "show-capabilities", '\0', action_show_capabilities,
                             "Show terminal capabilities and exit", "TERMINAL");

  // Usage examples for mirror help
  options_builder_add_usage(b, "mirror", NULL, true, "View local webcam or media file as ASCII art");

  // Examples for mirror help
  options_builder_add_example(b, "mirror", NULL, "View local webcam");

  options_builder_add_example(b, "mirror", "--color-mode mono", "View webcam in black and white");

  options_builder_add_example(b, "mirror", "-f video.mp4",
                              "Stream from video file (supports mp4, mkv, webm, mov, etc)");

  options_builder_add_example(b, "mirror", "-f '-'",
                              "Stream media from stdin (cat file.gif | ascii-chat mirror -f '-')");

  options_builder_add_example(b, "mirror", "-f '-' --loop",
                              "Stream from stdin and loop (cat file.webm | ascii-chat mirror -f '-' --loop)");

  options_builder_add_example(b, "mirror", "--snapshot", "Capture single frame and exit");

  const options_config_t *config = options_builder_build(b);
  options_builder_destroy(b);
  return config;
}

// ============================================================================
// ACDS Mode Options Preset
// ============================================================================

const options_config_t *options_preset_acds(const char *program_name, const char *description) {
  // Note: Each call creates a new config (no static caching) since program_name/description vary
  options_builder_t *b = options_builder_create(sizeof(options_t));
  if (!b)
    return NULL;

  b->program_name = program_name ? program_name : "ascii-chat discovery service";
  b->description = description ? description : "session management and WebRTC signaling";

  // Action options (execute and exit)
  options_builder_add_action(b, "help", 'h', action_help_acds, "Show this help message and exit", "GENERAL");

  // Network options
  // Note: Bind addresses are specified via positional arguments, not flags
  options_builder_add_port_option(b, OPT_ACDS_PORT_DEFAULT, "ACDS_PORT");

  // ACDS-specific options
  options_builder_add_string(b, "database", 'd', offsetof(options_t, discovery_database_path), "",
                             "Path to discovery service database file (default: ~/.config/ascii-chat/discovery.db or "
                             "%%APPDATA%%\\ascii-chat\\discovery.db)",
                             "DATABASE", false, "DISCOVERY_DATABASE_PATH", NULL);

  // Encryption options (shared with client and server)
  options_builder_add_crypto_group(b);

  // Identity verification options
  options_builder_add_bool(b, "require-server-identity", 'S', offsetof(options_t, require_server_identity), false,
                           "Require servers to provide signed Ed25519 identity when creating sessions", "SECURITY",
                           false, NULL);

  options_builder_add_bool(b, "require-client-identity", 'C', offsetof(options_t, require_client_identity), false,
                           "Require clients to provide signed Ed25519 identity when joining sessions", "SECURITY",
                           false, NULL);

  options_builder_add_bool(b, "require-server-verify", 'V', offsetof(options_t, require_server_verify), false,
                           "ACDS policy: require servers to verify client identity during handshake", "SECURITY", false,
                           NULL);

  options_builder_add_bool(b, "require-client-verify", 'c', offsetof(options_t, require_client_verify), false,
                           "ACDS policy: require clients to verify server identity during handshake", "SECURITY", false,
                           NULL);

  // WebRTC connectivity options
  options_builder_add_string(b, "stun-servers", '\0', offsetof(options_t, stun_servers), OPT_STUN_SERVERS_DEFAULT,
                             "Comma-separated list of STUN server URLs", "NETWORK", false, "ASCII_CHAT_STUN_SERVERS",
                             NULL);

  options_builder_add_string(b, "turn-servers", '\0', offsetof(options_t, turn_servers), OPT_TURN_SERVERS_DEFAULT,
                             "Comma-separated list of TURN server URLs", "NETWORK", false, "ASCII_CHAT_TURN_SERVERS",
                             NULL);

  options_builder_add_string(b, "turn-username", '\0', offsetof(options_t, turn_username), OPT_TURN_USERNAME_DEFAULT,
                             "Username for TURN server authentication", "NETWORK", false, "ASCII_CHAT_TURN_USERNAME",
                             NULL);

  options_builder_add_string(b, "turn-credential", '\0', offsetof(options_t, turn_credential),
                             OPT_TURN_CREDENTIAL_DEFAULT, "Credential/password for TURN server authentication",
                             "NETWORK", false, "ASCII_CHAT_TURN_CREDENTIAL", NULL);

  options_builder_add_string(b, "turn-secret", '\0', offsetof(options_t, turn_secret), "",
                             "Shared secret for dynamic TURN credential generation (HMAC-SHA1)", "NETWORK", false,
                             "ASCII_CHAT_TURN_SECRET", NULL);

  options_builder_add_bool(b, "upnp", '\0', offsetof(options_t, enable_upnp), false,
                           "Enable UPnP/NAT-PMP for automatic router port mapping (direct TCP for ~70%% of home users)",
                           "NETWORK", false, "ASCII_CHAT_UPNP");

  // Dependencies
  options_builder_add_dependency_conflicts(b, "no-encrypt", "encrypt", "Cannot use --no-encrypt with --encrypt");
  options_builder_add_dependency_conflicts(b, "no-encrypt", "key", "Cannot use --no-encrypt with --key");
  options_builder_add_dependency_conflicts(b, "no-encrypt", "password", "Cannot use --no-encrypt with --password");

  // Positional arguments: 0-2 bind addresses (IPv4 and/or IPv6)
  options_builder_add_positional(b, "bind-address", "IPv4 or IPv6 bind address (can specify 0-2 addresses)",
                                 false, // Not required (defaults to localhost)
                                 "BIND ADDRESS FORMATS", g_server_bind_address_examples,
                                 sizeof(g_server_bind_address_examples) / sizeof(g_server_bind_address_examples[0]),
                                 parse_server_bind_address);

  // Usage examples for discovery-service help
  options_builder_add_usage(b, "discovery-service", "[bind-address]", true,
                            "Start discovery service (secure session signalling and WebRTC coordination)");

  // Examples for discovery-service help
  options_builder_add_example(b, "discovery-service", NULL, "Start on localhost");

  options_builder_add_example(b, "discovery-service", "0.0.0.0", "Start on all IPv4 interfaces");

  options_builder_add_example(b, "discovery-service", "--port 5000", "Start on custom port");

  options_builder_add_example(b, "discovery-service", "--require-server-identity --require-client-identity",
                              "Enforce identity verification for all parties");

  const options_config_t *config = options_builder_build(b);
  options_builder_destroy(b);
  return config;
}

// ============================================================================
// Discovery Mode Options Preset
// ============================================================================

const options_config_t *options_preset_discovery(const char *program_name, const char *description) {
  // Note: Each call creates a new config (no static caching) since program_name/description vary
  options_builder_t *b = options_builder_create(sizeof(options_t));
  if (!b)
    return NULL;

  b->program_name = program_name ? program_name : "ascii-chat discovery";
  b->description =
      description ? description : "Discovery mode - join a session, dynamically become host based on NAT quality";

  // Action options (GENERAL - add first so it appears first in help)
  options_builder_add_action(b, "help", 'h', action_help_discovery, "Show this help message and exit", "GENERAL");

  options_builder_add_action(b, "version", 'v', action_show_version, "Show version information and exit", "GENERAL");

  // Terminal dimensions, webcam, display, and snapshot options
  options_builder_add_terminal_group(b);
  options_builder_add_webcam_group(b);
  options_builder_add_display_group(b);
  options_builder_add_snapshot_group(b);

  // Media file streaming options
  options_builder_add_media_group(b);

  // Audio options
  options_builder_add_audio_group(b);

  // ACDS Discovery options (required for discovery mode)
  options_builder_add_acds_group(b);

  // Security options
  options_builder_add_crypto_group(b);

  // ACDS security verification options
  options_builder_add_acds_security_group(b);

  // Compression options
  options_builder_add_compression_group(b);

  // WebRTC Connection Strategy Options (Phase 3 fallback control)
  options_builder_add_webrtc_strategy_group(b);

  // Webcam options
  options_builder_add_action(b, "list-webcams", '\0', action_list_webcams, "List available webcam devices and exit",
                             "WEBCAM");

  // Terminal options
  options_builder_add_action(b, "show-capabilities", '\0', action_show_capabilities,
                             "Show terminal capabilities and exit", "TERMINAL");

  // Audio options
  options_builder_add_action(b, "list-microphones", '\0', action_list_microphones,
                             "List available microphone devices and exit", "AUDIO");

  options_builder_add_action(b, "list-speakers", '\0', action_list_speakers, "List available speaker devices and exit",
                             "AUDIO");

  // Dependencies
  options_builder_add_dependency_requires(b, "snapshot-delay", "snapshot",
                                          "Option --snapshot-delay requires --snapshot");

  options_builder_add_dependency_requires(b, "loop", "file", "Option --loop requires --file");

  options_builder_add_dependency_conflicts(b, "no-encrypt", "encrypt", "Cannot use --no-encrypt with --encrypt");
  options_builder_add_dependency_conflicts(b, "no-encrypt", "key", "Cannot use --no-encrypt with --key");

  // Positional argument: session string (word-word-word format)
  // Discovery mode requires a session string to join
  static const char *session_examples[] = {"swift-river-mountain      Join session 'swift-river-mountain'",
                                           "cool-blue-sky             Join session 'cool-blue-sky'"};
  options_builder_add_positional(b, "session", "Session string to join (word-word-word format)",
                                 true, // Required
                                 "SESSION STRING FORMAT", session_examples,
                                 sizeof(session_examples) / sizeof(session_examples[0]), NULL);

  // Usage examples for discovery help
  options_builder_add_usage(b, "discovery", "<session-string>", true,
                            "Join a discovery session (use session string from initiator)");

  // Examples for discovery help
  options_builder_add_example(b, "discovery", "swift-river-mountain", "Join a session using the session string");

  options_builder_add_example(b, "discovery", "swift-river-mountain --color", "Join session with color support");

  options_builder_add_example(b, "discovery", "swift-river-mountain --discovery-server discovery.example.com",
                              "Join session via custom discovery server");

  options_builder_add_example(
      b, "discovery", "swift-river-mountain -f '-'",
      "Join session and stream media from stdin (cat file.mov | ascii-chat discovery ... -f '-')");

  const options_config_t *config = options_builder_build(b);
  options_builder_destroy(b);
  return config;
}
