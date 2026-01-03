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
 * @brief Add binary-level logging options to a builder
 *
 * This ensures consistent option definitions across all modes.
 * These options can be used before OR after the mode name.
 */
static void add_binary_logging_options(options_builder_t *b) {
  options_builder_add_string(b, "log-file", 'L', offsetof(options_t, log_file), "", "Redirect logs to FILE", "LOGGING",
                             false, "ASCII_CHAT_LOG_FILE", NULL);

  options_builder_add_callback(b, "log-level", '\0', offsetof(options_t, log_level),
                               &(log_level_t){LOG_INFO}, // Default: info level
                               sizeof(log_level_t), parse_log_level,
                               "Set log level: dev, debug, info, warn, error, fatal", "LOGGING", false, NULL);

  options_builder_add_int(b, "verbose", 'V', offsetof(options_t, verbose_level), 0,
                          "Increase log verbosity (stackable: -VV, -VVV)", "LOGGING", false, NULL, NULL);

  options_builder_add_bool(b, "quiet", 'q', offsetof(options_t, quiet), false,
                           "Disable console logging (log to file only)", "LOGGING", false, NULL);
}

// ============================================================================
// Binary-Level Options Preset
// ============================================================================

const options_config_t *options_preset_binary(void) {
  static options_config_t *config = NULL;
  if (config)
    return config;

  options_builder_t *b = options_builder_create(sizeof(options_t));
  if (!b)
    return NULL;

  b->program_name = "ascii-chat";
  b->description = "Terminal-based video chat with ASCII art rendering";

  // Help and version
  options_builder_add_bool(b, "help", '\0', offsetof(options_t, help), false, "Show this help", "GENERAL", false, NULL);

  options_builder_add_bool(b, "version", '\0', offsetof(options_t, version), false, "Show version information",
                           "GENERAL", false, NULL);

  // Add logging options
  add_binary_logging_options(b);

  config = options_builder_build(b);
  options_builder_destroy(b);
  return config;
}

// ============================================================================
// Server Mode Options Preset
// ============================================================================

const options_config_t *options_preset_server(void) {
  static options_config_t *config = NULL;
  if (config)
    return config;

  options_builder_t *b = options_builder_create(sizeof(options_t));
  if (!b)
    return NULL;

  b->program_name = "ascii-chat server";
  b->description = "Start ascii-chat server";

  // Network options
  // Note: Server bind addresses are positional arguments only, not flags

  options_builder_add_string(b, "port", 'p', offsetof(options_t, port), OPT_PORT_DEFAULT, "Server port", "NETWORK",
                             false, "ASCII_CHAT_PORT", NULL);

  options_builder_add_int(b, "max-clients", '\0', offsetof(options_t, max_clients), OPT_MAX_CLIENTS_DEFAULT,
                          "Maximum concurrent clients", "NETWORK", false, "ASCII_CHAT_MAX_CLIENTS", NULL);

  // Performance options
  options_builder_add_int(b, "compression-level", '\0', offsetof(options_t, compression_level),
                          OPT_COMPRESSION_LEVEL_DEFAULT, "zstd compression level (1-9)", "PERFORMANCE", false, NULL,
                          NULL);

  options_builder_add_bool(b, "no-compress", '\0', offsetof(options_t, no_compress), false, "Disable compression",
                           "PERFORMANCE", false, NULL);

  options_builder_add_bool(b, "encode-audio", '\0', offsetof(options_t, encode_audio), OPT_ENCODE_AUDIO_DEFAULT,
                           "Enable Opus audio encoding", "PERFORMANCE", false, NULL);

  options_builder_add_bool(b, "no-encode-audio", '\0',
                           offsetof(options_t, encode_audio), // Note: sets same field to opposite value
                           !OPT_ENCODE_AUDIO_DEFAULT, "Disable Opus audio encoding", "PERFORMANCE", false, NULL);

  options_builder_add_bool(b, "no-audio-mixer", '\0', offsetof(options_t, no_audio_mixer), false,
                           "Disable audio mixer (debug)", "PERFORMANCE", false, NULL);

  // Security options
  options_builder_add_bool(b, "encrypt", 'E', offsetof(options_t, encrypt_enabled), false, "Enable encryption",
                           "SECURITY", false, NULL);

  options_builder_add_string(b, "key", 'K', offsetof(options_t, encrypt_key), "", "SSH/GPG key file path", "SECURITY",
                             false, "ASCII_CHAT_KEY", NULL);

  options_builder_add_string(b, "password", '\0', offsetof(options_t, password), "",
                             "Shared password for authentication", "SECURITY", false, "ASCII_CHAT_PASSWORD", NULL);

  options_builder_add_string(b, "keyfile", 'F', offsetof(options_t, encrypt_keyfile), "", "Alternative key file path",
                             "SECURITY", false, NULL, NULL);

  options_builder_add_string(b, "client-keys", '\0', offsetof(options_t, client_keys), "",
                             "Allowed client keys whitelist", "SECURITY", false, NULL, NULL);

  options_builder_add_bool(b, "acds-expose-ip", '\0', offsetof(options_t, acds_expose_ip), false,
                           "Explicitly allow public IP disclosure in ACDS sessions (requires ACDS, opt-in only)",
                           "SECURITY", false, NULL);

  options_builder_add_bool(b, "no-encrypt", '\0', offsetof(options_t, no_encrypt), false, "Disable encryption",
                           "SECURITY", false, NULL);

  // ACDS Discovery options
  options_builder_add_string(b, "acds-server", '\0', offsetof(options_t, acds_server), "127.0.0.1",
                             "ACDS discovery server address", "DISCOVERY", false, NULL, NULL);

  options_builder_add_int(b, "acds-port", '\0', offsetof(options_t, acds_port), 27225, "ACDS discovery server port",
                          "DISCOVERY", false, NULL, NULL);

  options_builder_add_bool(b, "webrtc", '\0', offsetof(options_t, webrtc), false,
                           "Enable WebRTC mode for ACDS session (default: Direct TCP)", "DISCOVERY", false, NULL);

  options_builder_add_bool(b, "upnp", '\0', offsetof(options_t, enable_upnp), true,
                           "Enable UPnP/NAT-PMP for automatic port mapping (enables direct TCP for most home users)",
                           "DISCOVERY", false, "ASCII_CHAT_UPNP");

  options_builder_add_bool(b, "no-upnp", '\0', offsetof(options_t, no_upnp), false,
                           "Disable UPnP/NAT-PMP port mapping (requires manual port forwarding)", "DISCOVERY", false,
                           NULL);

  options_builder_add_bool(b, "no-mdns-advertise", '\0', offsetof(options_t, no_mdns_advertise), false,
                           "Disable mDNS service advertisement on local network (LAN discovery won't find this server)",
                           "DISCOVERY", false, NULL);

  // Add binary-level logging options (--log-file, --log-level, -V, -q)
  // These work before or after the mode name
  add_binary_logging_options(b);

  // Dependencies
  options_builder_add_dependency_conflicts(b, "no-encrypt", "encrypt", "Cannot use --no-encrypt with --encrypt");
  options_builder_add_dependency_conflicts(b, "no-encrypt", "key", "Cannot use --no-encrypt with --key");
  options_builder_add_dependency_conflicts(b, "no-encrypt", "password", "Cannot use --no-encrypt with --password");
  options_builder_add_dependency_conflicts(b, "no-compress", "compression-level",
                                           "Cannot use --no-compress with --compression-level");
  options_builder_add_dependency_conflicts(b, "encode-audio", "no-encode-audio",
                                           "Cannot use both --encode-audio and --no-encode-audio");

  // Action options (execute and exit)
  options_builder_add_action(b, "help", 'h', action_help_server, "Show this help message and exit", "ACTIONS");

  options_builder_add_action(b, "list-webcams", '\0', action_list_webcams, "List available webcam devices and exit",
                             "ACTIONS");

  options_builder_add_action(b, "list-microphones", '\0', action_list_microphones,
                             "List available microphone devices and exit", "ACTIONS");

  options_builder_add_action(b, "list-speakers", '\0', action_list_speakers, "List available speaker devices and exit",
                             "ACTIONS");

  options_builder_add_action(b, "version", 'V', action_show_version, "Show version information and exit", "ACTIONS");

  // Positional arguments: 0-2 bind addresses (IPv4 and/or IPv6)
  options_builder_add_positional(b, "bind-address", "IPv4 or IPv6 bind address (can specify 0-2 addresses)",
                                 false, // Not required (defaults to localhost)
                                 parse_server_bind_address);

  config = options_builder_build(b);
  options_builder_destroy(b);
  return config;
}

// ============================================================================
// Client Mode Options Preset
// ============================================================================

const options_config_t *options_preset_client(void) {
  static options_config_t *config = NULL;
  if (config)
    return config;

  options_builder_t *b = options_builder_create(sizeof(options_t));
  if (!b)
    return NULL;

  b->program_name = "ascii-chat client";
  b->description = "Connect to ascii-chat server";

  // Network options
  options_builder_add_string(b, "address", 'a', offsetof(options_t, address), OPT_ADDRESS_DEFAULT, "Server address",
                             "NETWORK", false, "ASCII_CHAT_SERVER", NULL);

  options_builder_add_string(b, "port", 'p', offsetof(options_t, port), OPT_PORT_DEFAULT, "Server port", "NETWORK",
                             false, "ASCII_CHAT_PORT", NULL);

  options_builder_add_int(b, "reconnect", 'r', offsetof(options_t, reconnect_attempts), OPT_RECONNECT_ATTEMPTS_DEFAULT,
                          "Reconnection attempts (-1=infinite)", "NETWORK", false, NULL, NULL);

  options_builder_add_bool(b, "scan", '\0', offsetof(options_t, lan_discovery), false,
                           "Scan for ASCII-Chat servers on local network (mDNS)", "NETWORK", false, NULL);

  // Terminal dimensions
  options_builder_add_int(b, "width", 'x', offsetof(options_t, width), OPT_WIDTH_DEFAULT,
                          "Terminal width in characters", "TERMINAL", false, NULL, NULL);

  options_builder_add_int(b, "height", 'y', offsetof(options_t, height), OPT_HEIGHT_DEFAULT,
                          "Terminal height in characters", "TERMINAL", false, NULL, NULL);

  // Webcam options
  options_builder_add_int(b, "webcam-index", 'c', offsetof(options_t, webcam_index), OPT_WEBCAM_INDEX_DEFAULT,
                          "Webcam device index", "WEBCAM", false, NULL, NULL);

  options_builder_add_bool(b, "webcam-flip", 'f', offsetof(options_t, webcam_flip), OPT_WEBCAM_FLIP_DEFAULT,
                           "Flip webcam horizontally", "WEBCAM", false, NULL);

  options_builder_add_bool(b, "test-pattern", '\0', offsetof(options_t, test_pattern), false,
                           "Use test pattern instead of webcam", "WEBCAM", false, "WEBCAM_DISABLED");

  // Display options - use custom parsers for enum types
  options_builder_add_callback(b, "color-mode", '\0', offsetof(options_t, color_mode),
                               &(terminal_color_level_t){TERM_COLOR_AUTO}, // Auto-detect by default
                               sizeof(terminal_color_level_t), parse_color_mode,
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

  options_builder_add_bool(b, "show-capabilities", '\0', offsetof(options_t, show_capabilities), false,
                           "Show terminal capabilities and exit", "DISPLAY", false, NULL);

  options_builder_add_bool(b, "utf8", '\0', offsetof(options_t, force_utf8), false, "Force UTF-8 support", "DISPLAY",
                           false, NULL);

  options_builder_add_bool(b, "stretch", 's', offsetof(options_t, stretch), false, "Allow aspect ratio distortion",
                           "DISPLAY", false, NULL);

  options_builder_add_bool(b, "strip-ansi", '\0', offsetof(options_t, strip_ansi), false, "Strip ANSI escape sequences",
                           "DISPLAY", false, NULL);

  options_builder_add_int(b, "fps", '\0', offsetof(options_t, fps), 0, "Target framerate (1-144, default: 60)",
                          "DISPLAY", false, NULL, NULL);

  // Snapshot mode
  options_builder_add_bool(b, "snapshot", 'S', offsetof(options_t, snapshot_mode), false,
                           "Snapshot mode (one frame and exit)", "SNAPSHOT", false, NULL);

  options_builder_add_double(b, "snapshot-delay", 'D', offsetof(options_t, snapshot_delay), SNAPSHOT_DELAY_DEFAULT,
                             "Snapshot delay in seconds", "SNAPSHOT", false, NULL, NULL);

  // Audio options
  options_builder_add_bool(b, "audio", 'A', offsetof(options_t, audio_enabled), false, "Enable audio streaming",
                           "AUDIO", false, NULL);

  options_builder_add_int(b, "microphone-index", '\0', offsetof(options_t, microphone_index),
                          OPT_MICROPHONE_INDEX_DEFAULT, "Microphone device index (-1=default)", "AUDIO", false, NULL,
                          NULL);

  options_builder_add_int(b, "speakers-index", '\0', offsetof(options_t, speakers_index), OPT_SPEAKERS_INDEX_DEFAULT,
                          "Speakers device index (-1=default)", "AUDIO", false, NULL, NULL);

  options_builder_add_bool(b, "audio-analysis", '\0', offsetof(options_t, audio_analysis_enabled), false,
                           "Enable audio analysis (debug)", "AUDIO", false, NULL);

  options_builder_add_bool(b, "no-audio-playback", '\0', offsetof(options_t, audio_no_playback), false,
                           "Disable speaker playback (debug)", "AUDIO", false, NULL);

  // Performance options
  options_builder_add_int(b, "compression-level", '\0', offsetof(options_t, compression_level),
                          OPT_COMPRESSION_LEVEL_DEFAULT, "zstd compression level (1-9)", "PERFORMANCE", false, NULL,
                          NULL);

  options_builder_add_bool(b, "no-compress", '\0', offsetof(options_t, no_compress), false, "Disable compression",
                           "PERFORMANCE", false, NULL);

  options_builder_add_bool(b, "encode-audio", '\0', offsetof(options_t, encode_audio), OPT_ENCODE_AUDIO_DEFAULT,
                           "Enable Opus audio encoding", "PERFORMANCE", false, NULL);

  options_builder_add_bool(b, "no-encode-audio", '\0', offsetof(options_t, encode_audio), !OPT_ENCODE_AUDIO_DEFAULT,
                           "Disable Opus audio encoding", "PERFORMANCE", false, NULL);

  // ACDS Discovery options
  options_builder_add_string(b, "acds-server", '\0', offsetof(options_t, acds_server), "127.0.0.1",
                             "ACDS discovery server address (default: 127.0.0.1)", "DISCOVERY", false, NULL, NULL);

  options_builder_add_int(b, "acds-port", '\0', offsetof(options_t, acds_port), 27225, "ACDS discovery server port",
                          "DISCOVERY", false, NULL, NULL);

  options_builder_add_bool(b, "webrtc", '\0', offsetof(options_t, webrtc), false,
                           "Use WebRTC P2P mode (default: Direct TCP)", "DISCOVERY", false, NULL);

  // Security options
  options_builder_add_bool(b, "encrypt", 'E', offsetof(options_t, encrypt_enabled), false, "Enable encryption",
                           "SECURITY", false, NULL);

  options_builder_add_string(b, "key", 'K', offsetof(options_t, encrypt_key), "", "SSH/GPG key file path", "SECURITY",
                             false, "ASCII_CHAT_KEY", NULL);

  options_builder_add_string(b, "server-key", '\0', offsetof(options_t, server_key), "", "Expected server public key",
                             "SECURITY", false, NULL, NULL);

  options_builder_add_string(b, "password", '\0', offsetof(options_t, password), "",
                             "Shared password for authentication", "SECURITY", false, "ASCII_CHAT_PASSWORD", NULL);

  options_builder_add_string(b, "keyfile", 'F', offsetof(options_t, encrypt_keyfile), "", "Alternative key file path",
                             "SECURITY", false, NULL, NULL);

  // Add binary-level logging options (--log-file, --log-level, -V, -q)
  // These work before or after the mode name
  add_binary_logging_options(b);

  // Dependencies
  options_builder_add_dependency_requires(b, "snapshot-delay", "snapshot",
                                          "Option --snapshot-delay requires --snapshot");
  options_builder_add_dependency_conflicts(b, "no-compress", "compression-level",
                                           "Cannot use --no-compress with --compression-level");
  options_builder_add_dependency_conflicts(b, "encode-audio", "no-encode-audio",
                                           "Cannot use both --encode-audio and --no-encode-audio");

  // Action options (execute and exit)
  options_builder_add_action(b, "help", 'h', action_help_client, "Show this help message and exit", "ACTIONS");

  options_builder_add_action(b, "list-webcams", '\0', action_list_webcams, "List available webcam devices and exit",
                             "ACTIONS");

  options_builder_add_action(b, "list-microphones", '\0', action_list_microphones,
                             "List available microphone devices and exit", "ACTIONS");

  options_builder_add_action(b, "list-speakers", '\0', action_list_speakers, "List available speaker devices and exit",
                             "ACTIONS");

  options_builder_add_action(b, "show-capabilities", '\0', action_show_capabilities,
                             "Show terminal capabilities and exit", "ACTIONS");

  // Positional argument: [address][:port]
  options_builder_add_positional(b, "address",
                                 "[address][:port] - Server address (IPv4, IPv6, or hostname) with optional port",
                                 false, // Not required (defaults to localhost:27224)
                                 parse_client_address);

  config = options_builder_build(b);
  options_builder_destroy(b);
  return config;
}

// ============================================================================
// Mirror Mode Options Preset
// ============================================================================

const options_config_t *options_preset_mirror(void) {
  static options_config_t *config = NULL;
  if (config)
    return config;

  options_builder_t *b = options_builder_create(sizeof(options_t));
  if (!b)
    return NULL;

  b->program_name = "ascii-chat mirror";
  b->description = "Local webcam viewing (no network)";

  // Terminal dimensions
  options_builder_add_int(b, "width", 'x', offsetof(options_t, width), OPT_WIDTH_DEFAULT,
                          "Terminal width in characters", "TERMINAL", false, NULL, NULL);

  options_builder_add_int(b, "height", 'y', offsetof(options_t, height), OPT_HEIGHT_DEFAULT,
                          "Terminal height in characters", "TERMINAL", false, NULL, NULL);

  // Webcam options
  options_builder_add_int(b, "webcam-index", 'c', offsetof(options_t, webcam_index), OPT_WEBCAM_INDEX_DEFAULT,
                          "Webcam device index", "WEBCAM", false, NULL, NULL);

  options_builder_add_bool(b, "webcam-flip", 'f', offsetof(options_t, webcam_flip), OPT_WEBCAM_FLIP_DEFAULT,
                           "Flip webcam horizontally", "WEBCAM", false, NULL);

  options_builder_add_bool(b, "test-pattern", '\0', offsetof(options_t, test_pattern), false,
                           "Use test pattern instead of webcam", "WEBCAM", false, "WEBCAM_DISABLED");

  // Display options - use custom parsers for enum types
  options_builder_add_callback(b, "color-mode", '\0', offsetof(options_t, color_mode),
                               &(terminal_color_level_t){TERM_COLOR_AUTO}, // Auto-detect by default
                               sizeof(terminal_color_level_t), parse_color_mode,
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

  options_builder_add_bool(b, "show-capabilities", '\0', offsetof(options_t, show_capabilities), false,
                           "Show terminal capabilities and exit", "DISPLAY", false, NULL);

  options_builder_add_bool(b, "utf8", '\0', offsetof(options_t, force_utf8), false, "Force UTF-8 support", "DISPLAY",
                           false, NULL);

  options_builder_add_bool(b, "stretch", 's', offsetof(options_t, stretch), false, "Allow aspect ratio distortion",
                           "DISPLAY", false, NULL);

  options_builder_add_bool(b, "strip-ansi", '\0', offsetof(options_t, strip_ansi), false, "Strip ANSI escape sequences",
                           "DISPLAY", false, NULL);

  options_builder_add_int(b, "fps", '\0', offsetof(options_t, fps), 0, "Target framerate (1-144, default: 30)",
                          "DISPLAY", false, NULL, NULL);

  // Snapshot mode
  options_builder_add_bool(b, "snapshot", 'S', offsetof(options_t, snapshot_mode), false,
                           "Snapshot mode (one frame and exit)", "SNAPSHOT", false, NULL);

  options_builder_add_double(b, "snapshot-delay", 'D', offsetof(options_t, snapshot_delay), SNAPSHOT_DELAY_DEFAULT,
                             "Snapshot delay in seconds", "SNAPSHOT", false, NULL, NULL);

  // Add binary-level logging options (--log-file, --log-level, -V, -q)
  // These work before or after the mode name
  add_binary_logging_options(b);

  // Dependencies
  options_builder_add_dependency_requires(b, "snapshot-delay", "snapshot",
                                          "Option --snapshot-delay requires --snapshot");

  // Action options (execute and exit)
  options_builder_add_action(b, "help", 'h', action_help_mirror, "Show this help message and exit", "ACTIONS");

  options_builder_add_action(b, "list-webcams", '\0', action_list_webcams, "List available webcam devices and exit",
                             "ACTIONS");

  options_builder_add_action(b, "show-capabilities", '\0', action_show_capabilities,
                             "Show terminal capabilities and exit", "ACTIONS");

  config = options_builder_build(b);
  options_builder_destroy(b);
  return config;
}

// ============================================================================
// ACDS Mode Options Preset
// ============================================================================

const options_config_t *options_preset_acds(void) {
  static options_config_t *config = NULL;
  if (config)
    return config;

  options_builder_t *b = options_builder_create(sizeof(options_t));
  if (!b)
    return NULL;

  b->program_name = "ascii-chat acds";
  b->description = "ASCII Chat Discovery Service - session management and WebRTC signaling";

  // Help and version
  options_builder_add_bool(b, "help", 'h', offsetof(options_t, help), false, "Show this help", "GENERAL", false, NULL);

  // Network options
  options_builder_add_string(b, "address", 'a', offsetof(options_t, address), "127.0.0.1", "Bind address", "NETWORK",
                             false, "ACDS_ADDRESS", NULL);

  options_builder_add_string(b, "port", 'p', offsetof(options_t, port), "27225", "Server port", "NETWORK", false,
                             "ACDS_PORT", NULL);

  options_builder_add_string(b, "address6", '\0', offsetof(options_t, address6), "::1", "IPv6 bind address", "NETWORK",
                             false, NULL, NULL);

  // ACDS-specific options
  options_builder_add_string(b, "key", 'k', offsetof(options_t, acds_key_path), "",
                             "Path to ACDS identity key file (default: ~/.ascii-chat/acds_identity)", "ACDS", false,
                             "ACDS_KEY_PATH", NULL);

  options_builder_add_string(b, "database", 'd', offsetof(options_t, acds_database_path), "",
                             "Path to ACDS database file (default: ~/.ascii-chat/acds.db)", "ACDS", false,
                             "ACDS_DATABASE_PATH", NULL);

  // Logging options (binary-level, work before or after mode name)
  // Note: ACDS doesn't include --verbose/-V and --quiet/-q since they conflict with
  // ACDS-specific security options (require-server-verify uses 'V')
  options_builder_add_string(b, "log-file", 'L', offsetof(options_t, log_file), "", "Redirect logs to FILE", "LOGGING",
                             false, "ASCII_CHAT_LOG_FILE", NULL);

  options_builder_add_callback(b, "log-level", '\0', offsetof(options_t, log_level), &(log_level_t){LOG_INFO},
                               sizeof(log_level_t), parse_log_level,
                               "Set log level: dev, debug, info, warn, error, fatal", "LOGGING", false, NULL);

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
  options_builder_add_string(b, "stun-servers", '\0', offsetof(options_t, stun_servers),
                             "stun:stun.ascii-chat.com:3478,stun:stun.l.google.com:19302",
                             "Comma-separated list of STUN server URLs", "WEBRTC", false, "ASCII_CHAT_STUN_SERVERS",
                             NULL);

  options_builder_add_string(b, "turn-servers", '\0', offsetof(options_t, turn_servers),
                             "turn:turn.ascii-chat.com:3478", "Comma-separated list of TURN server URLs", "WEBRTC",
                             false, "ASCII_CHAT_TURN_SERVERS", NULL);

  options_builder_add_string(b, "turn-username", '\0', offsetof(options_t, turn_username), "ascii",
                             "Username for TURN server authentication", "WEBRTC", false, "ASCII_CHAT_TURN_USERNAME",
                             NULL);

  options_builder_add_string(b, "turn-credential", '\0', offsetof(options_t, turn_credential),
                             "0aa9917b4dad1b01631e87a32b875e09", "Credential/password for TURN server authentication",
                             "WEBRTC", false, "ASCII_CHAT_TURN_CREDENTIAL", NULL);

  options_builder_add_string(b, "turn-secret", '\0', offsetof(options_t, turn_secret), "",
                             "Shared secret for dynamic TURN credential generation (HMAC-SHA1)", "WEBRTC", false,
                             "ASCII_CHAT_TURN_SECRET", NULL);

  options_builder_add_bool(b, "upnp", '\0', offsetof(options_t, enable_upnp), true,
                           "Enable UPnP/NAT-PMP for automatic port mapping (enables direct TCP for most home users)",
                           "WEBRTC", false, "ASCII_CHAT_UPNP");

  options_builder_add_bool(b, "no-upnp", '\0', offsetof(options_t, no_upnp), false,
                           "Disable UPnP/NAT-PMP port mapping (requires manual port forwarding)", "WEBRTC", false,
                           NULL);

  // Action options (execute and exit)
  options_builder_add_action(b, "version", 'v', action_show_version, "Show version information and exit", "ACTIONS");

  // Positional arguments: 0-2 bind addresses (IPv4 and/or IPv6)
  options_builder_add_positional(b, "bind-address", "IPv4 or IPv6 bind address (can specify 0-2 addresses)",
                                 false, // Not required (defaults to localhost)
                                 parse_server_bind_address);

  config = options_builder_build(b);
  options_builder_destroy(b);
  return config;
}
