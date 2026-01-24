/**
 * @file registry.c
 * @brief Central options registry implementation
 * @ingroup options
 *
 * Defines all command-line options exactly once with mode bitmasks.
 * This is the single source of truth for all options.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "options/registry.h"
#include "options/parsers.h" // For parse_log_level, parse_color_mode, parse_palette_type, parse_palette_chars, etc.
#include "options/validation.h"
#include "options/actions.h" // For action_list_webcams, action_list_microphones, action_list_speakers
#include "options/common.h"
#include "common.h"
#include "log/logging.h"
#include "platform/terminal.h"
#include "video/palette.h"
#include "discovery/strings.h" // For SESSION_STRING_BUFFER_SIZE

#include <stdlib.h>
#include <string.h>
#include <ctype.h> // For islower in parse_cookies_from_browser

// ============================================================================
// Static Parser Functions (Moved from presets.c)
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
      *error_msg = strdup("Invalid timestamp format: too many colons");
    }
    return false;
  }
}

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

// ============================================================================
// Registry Entry Structure Definition
// ============================================================================
// Note: All default_*_value variables are now defined in options.h with their
// corresponding OPT_*_DEFAULT constants for single source of truth.

/**
 * @brief Registry entry - stores option definition with mode bitmask
 */
typedef struct {
  const char *long_name;
  char short_name;
  option_type_t type;
  size_t offset;
  const void *default_value;
  size_t default_value_size;
  const char *help_text;
  const char *group;
  bool required;
  const char *env_var_name;
  bool (*validate_fn)(const void *options_struct, char **error_msg);
  bool (*parse_fn)(const char *arg, void *dest, char **error_msg);
  bool owns_memory;
  bool optional_arg;
  option_mode_bitmask_t mode_bitmask;
} registry_entry_t;

// ============================================================================
// Complete Options Registry
// ============================================================================
static const registry_entry_t g_options_registry[] = {
    // LOGGING GROUP (binary-level)
    {"log-file", 'L', OPTION_TYPE_STRING, offsetof(options_t, log_file), "", 0, "Set FILE as path for log file",
     "LOGGING", false, "ASCII_CHAT_LOG_FILE", NULL, NULL, false, false, OPTION_MODE_BINARY},
    {"log-level", '\0', OPTION_TYPE_CALLBACK, offsetof(options_t, log_level), &default_log_level_value,
     sizeof(log_level_t), "Set log level: dev, debug, info, warn, error, fatal", "LOGGING", false,
     "ASCII_CHAT_LOG_LEVEL", NULL, parse_log_level, false, false, OPTION_MODE_BINARY},
    {"verbose", 'V', OPTION_TYPE_CALLBACK, offsetof(options_t, verbose_level), 0, sizeof(unsigned short int),
     "Increase log verbosity (stackable: -VV, -VVV)", "LOGGING", false, "ASCII_CHAT_VERBOSE", NULL, parse_verbose_flag,
     false, true, OPTION_MODE_BINARY},
    {"quiet", 'q', OPTION_TYPE_BOOL, offsetof(options_t, quiet), OPT_QUIET_DEFAULT, sizeof(bool),
     "Disable console logging (log to file only)", "LOGGING", false, "ASCII_CHAT_QUIET", NULL, NULL, false, false,
     OPTION_MODE_BINARY},

    // CONFIGURATION GROUP (binary-level)
    {"config", '\0', OPTION_TYPE_STRING, offsetof(options_t, config_file), "", 0, "Load configuration from toml FILE",
     "CONFIGURATION", false, NULL, NULL, NULL, false, false, OPTION_MODE_BINARY},
    {"config-create", '\0', OPTION_TYPE_BOOL, 0, NULL, 0,
     "Create default config file and exit (optionally specify output path)", "CONFIGURATION", false, NULL, NULL, NULL,
     false, false, OPTION_MODE_BINARY},

    // SHELL GROUP (binary-level)
    {"completions", '\0', OPTION_TYPE_STRING, 0, NULL, 0, "Generate shell completions (bash, fish, zsh, powershell)",
     "SHELL", false, NULL, NULL, NULL, false, false, OPTION_MODE_BINARY},
    {"create-man-page", '\0', OPTION_TYPE_BOOL, 0, NULL, 0, "Create man page for ascii-chat", "SHELL", false, NULL,
     NULL, NULL, false, false, OPTION_MODE_BINARY},

    // TERMINAL GROUP (client, mirror, discovery)
    {"width", 'x', OPTION_TYPE_INT, offsetof(options_t, width), &default_width_value, sizeof(int),
     "Terminal width in characters. Can be controlled using $COLUMNS.", "TERMINAL", false, "ASCII_CHAT_WIDTH", NULL,
     NULL, false, false, OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"height", 'y', OPTION_TYPE_INT, offsetof(options_t, height), &default_height_value, sizeof(int),
     "Terminal height in characters. Can be controlled using $ROWS.", "TERMINAL", false, "ASCII_CHAT_HEIGHT", NULL,
     NULL, false, false, OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},

    // WEBCAM GROUP (client, mirror, discovery)
    {"webcam-index", 'c', OPTION_TYPE_INT, offsetof(options_t, webcam_index), &default_webcam_index_value,
     sizeof(unsigned short int), "Webcam device index to use for video input", "WEBCAM", false,
     "ASCII_CHAT_WEBCAM_INDEX", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"webcam-flip", 'g', OPTION_TYPE_BOOL, offsetof(options_t, webcam_flip), &default_webcam_flip_value, sizeof(bool),
     "Flip webcam output horizontally before using it", "WEBCAM", false, "ASCII_CHAT_WEBCAM_FLIP", NULL, NULL, false,
     false, OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"test-pattern", '\0', OPTION_TYPE_BOOL, offsetof(options_t, test_pattern), &default_test_pattern_value,
     sizeof(bool), "Use test pattern instead of webcam", "WEBCAM", false, "WEBCAM_DISABLED", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"list-webcams", '\0', OPTION_TYPE_ACTION, 0, NULL, 0, "List available webcam devices and exit", "WEBCAM", false,
     NULL, NULL, NULL, false, false, OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"list-microphones", '\0', OPTION_TYPE_ACTION, 0, NULL, 0, "List available audio input devices and exit", "AUDIO",
     false, NULL, NULL, NULL, false, false, OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"list-speakers", '\0', OPTION_TYPE_ACTION, 0, NULL, 0, "List available audio output devices and exit", "AUDIO",
     false, NULL, NULL, NULL, false, false, OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},

    // DISPLAY GROUP (client, mirror, discovery)
    {"color-mode", '\0', OPTION_TYPE_CALLBACK, offsetof(options_t, color_mode), &default_color_mode_value,
     sizeof(terminal_color_mode_t),
     "Terminal color level (auto, none, 16, 256, truecolor). This controls what ANSI escape codes ascii-chat will use.",
     "TERMINAL", false, "ASCII_CHAT_COLOR_MODE", NULL, parse_color_mode, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"render-mode", 'M', OPTION_TYPE_CALLBACK, offsetof(options_t, render_mode), &default_render_mode_value,
     sizeof(render_mode_t), "ascii render mode (foreground, background, half-block)", "DISPLAY", false,
     "ASCII_CHAT_RENDER_MODE", NULL, parse_render_mode, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"palette", 'P', OPTION_TYPE_CALLBACK, offsetof(options_t, palette_type), &default_palette_type_value,
     sizeof(palette_type_t),
     "ascii palette type (standard, blocks, digital, minimal, cool, custom). All but custom are built-in presets that "
     "look nice. Try them out!",
     "DISPLAY", false, "ASCII_CHAT_PALETTE", NULL, parse_palette_type, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"palette-chars", 'C', OPTION_TYPE_CALLBACK, offsetof(options_t, palette_custom), "", 0,
     "Custom palette characters (implies --palette=custom) for rendering images to ascii. These characters only will "
     "be used to create the rendered output. Can be UTF-8 content (see --utf8).",
     "DISPLAY", false, "ASCII_CHAT_PALETTE_CHARS", NULL, parse_palette_chars, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"show-capabilities", '\0', OPTION_TYPE_BOOL, offsetof(options_t, show_capabilities),
     &default_show_capabilities_value, sizeof(bool), "Show detected terminal capabilities and exit", "TERMINAL", false,
     "ASCII_CHAT_SHOW_CAPABILITIES", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"utf8", '\0', OPTION_TYPE_BOOL, offsetof(options_t, force_utf8), &default_force_utf8_value, sizeof(bool),
     "Force UTF-8 support. By default UTF-8 is automatically detected and enabled if the terminal supports it.",
     "TERMINAL", false, "ASCII_CHAT_UTF8", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"stretch", 's', OPTION_TYPE_BOOL, offsetof(options_t, stretch), &default_stretch_value, sizeof(bool),
     "Allow aspect ratio distortion of image for rendering ascii output. This can allow the rendered ascii to fill "
     "your terminal.",
     "DISPLAY", false, "ASCII_CHAT_STRETCH", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"strip-ansi", '\0', OPTION_TYPE_BOOL, offsetof(options_t, strip_ansi), &default_strip_ansi_value, sizeof(bool),
     "Strip ANSI escape sequences from output before printing. Useful for scripting and debugging.", "TERMINAL", false,
     "ASCII_CHAT_STRIP_ANSI", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"fps", '\0', OPTION_TYPE_INT, offsetof(options_t, fps), 0, sizeof(int),
     "Target framerate for rendering ascii (1-144, 0=use default).", "DISPLAY", false, "ASCII_CHAT_FPS", NULL, NULL,
     false, false, OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},

    // SNAPSHOT GROUP (client, mirror, discovery)
    {"snapshot", 'S', OPTION_TYPE_BOOL, offsetof(options_t, snapshot_mode), &default_snapshot_mode_value, sizeof(bool),
     "Snapshot mode (one frame and exit)", "SNAPSHOT", false, "ASCII_CHAT_SNAPSHOT", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"snapshot-delay", 'D', OPTION_TYPE_DOUBLE, offsetof(options_t, snapshot_delay), &default_snapshot_delay_value,
     sizeof(double),
     "Snapshot delay in seconds. The timer starts right before the client-side program prints the first frame. "
     "--snapshot --snapshot-delay=0 will print the first frame and exit.",
     "SNAPSHOT", false, "ASCII_CHAT_SNAPSHOT_DELAY", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},

    // PERFORMANCE GROUP (client, server, discovery)
    {"compression-level", '\0', OPTION_TYPE_INT, offsetof(options_t, compression_level),
     &default_compression_level_value, sizeof(int), "zstd compression level (1-9)", "PERFORMANCE", false,
     "ASCII_CHAT_COMPRESSION_LEVEL", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY},
    {"no-compress", '\0', OPTION_TYPE_BOOL, offsetof(options_t, no_compress), &default_no_compress_value, sizeof(bool),
     "Disable compression", "PERFORMANCE", false, "ASCII_CHAT_NO_COMPRESS", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY},

    // SECURITY GROUP (client, server, discovery)
    {"encrypt", 'E', OPTION_TYPE_BOOL, offsetof(options_t, encrypt_enabled), &default_encrypt_enabled_value,
     sizeof(bool), "Enable end-to-end encryption (requires the other party to be encrypted as well)", "SECURITY", false,
     "ASCII_CHAT_ENCRYPT", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY | OPTION_MODE_DISCOVERY_SVC},
    {"key", 'K', OPTION_TYPE_STRING, offsetof(options_t, encrypt_key), "", 0,
     "Server identity key (SSH Ed25519 or GPG key file, gpg:FINGERPRINT, github:USER[.gpg], gitlab:USER[.gpg], or "
     "HTTPS URL like https://example.com/key.pub or .gpg)",
     "SECURITY", false, "ASCII_CHAT_KEY", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY | OPTION_MODE_DISCOVERY_SVC},
    {"password", '\0', OPTION_TYPE_STRING, offsetof(options_t, password), "", 0,
     "Shared password for authentication (8-256 characters)", "SECURITY", false, "ASCII_CHAT_PASSWORD", NULL, NULL,
     false, false, OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY | OPTION_MODE_DISCOVERY_SVC},
    {"no-encrypt", '\0', OPTION_TYPE_BOOL, offsetof(options_t, no_encrypt), &default_no_encrypt_value, sizeof(bool),
     "Disable encryption (requires the other party to be unencrypted as well)", "SECURITY", false,
     "ASCII_CHAT_NO_ENCRYPT", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY | OPTION_MODE_DISCOVERY_SVC},
    {"server-key", '\0', OPTION_TYPE_STRING, offsetof(options_t, server_key), "", 0,
     "Expected server public key for verification (SSH Ed25519 or GPG key file, gpg:FINGERPRINT, github:USER[.gpg], "
     "gitlab:USER[.gpg], or HTTPS URL like https://example.com/key.pub or .gpg)",
     "SECURITY", false, "ASCII_CHAT_SERVER_KEY", NULL, NULL, false, false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"client-keys", '\0', OPTION_TYPE_STRING, offsetof(options_t, client_keys), "", 0,
     "Allowed client keys (comma-separated: file paths with one key per line, github:USER[.gpg], gitlab:USER[.gpg], "
     "gpg:KEYID, or HTTPS URLs)",
     "SECURITY", false, "ASCII_CHAT_CLIENT_KEYS", NULL, NULL, false, false,
     OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY | OPTION_MODE_DISCOVERY_SVC},
    {"discovery-insecure", '\0', OPTION_TYPE_BOOL, offsetof(options_t, discovery_insecure),
     &default_discovery_insecure_value, sizeof(bool),
     "Skip server key verification (MITM-vulnerable, requires explicit opt-in)", "SECURITY", false,
     "ASCII_CHAT_DISCOVERY_INSECURE", NULL, NULL, false, false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"discovery-server-key", '\0', OPTION_TYPE_STRING, offsetof(options_t, discovery_service_key), "", 0,
     "Discovery server public key for verification (SSH Ed25519 or GPG key file, gpg:FINGERPRINT, github:USER, "
     "gitlab:USER, or HTTPS URL like https://discovery.ascii-chat.com/key.pub)",
     "SECURITY", false, "ASCII_CHAT_DISCOVERY_SERVER_KEY", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},

    // NETWORK GROUP (general network options, various modes)
    {"port", 'p', OPTION_TYPE_CALLBACK, offsetof(options_t, port), OPT_PORT_DEFAULT, OPTIONS_BUFF_SIZE,
     "Port to host a server or discovery-service on, or port to connect to a server as a client", "NETWORK", false,
     "ASCII_CHAT_PORT", NULL, parse_port_option, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC | OPTION_MODE_DISCOVERY},
    {"max-clients", '\0', OPTION_TYPE_INT, offsetof(options_t, max_clients), &default_max_clients_value, sizeof(int),
     "Maximum concurrent clients", "NETWORK", false, "ASCII_CHAT_MAX_CLIENTS", NULL, NULL, false, false,
     OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC}, // Server and Discovery Service
    {"reconnect-attempts", '\0', OPTION_TYPE_INT, offsetof(options_t, reconnect_attempts),
     &default_reconnect_attempts_value, sizeof(int),
     "Number of reconnection attempts before giving up (-1=infinite, 0=none)", "NETWORK", false,
     "ASCII_CHAT_RECONNECT_ATTEMPTS", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY}, // Client and Discovery
    {"port-forwarding", '\0', OPTION_TYPE_BOOL, offsetof(options_t, enable_upnp), &default_enable_upnp_value,
     sizeof(bool),
     "Use UPnP/NAT-PMP port mapping to open a port in your router to ascii-chat (might fail with some routers)",
     "NETWORK", false, "ASCII_CHAT_PORT_FORWARDING", NULL, NULL, false, false,
     OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC},
    {"scan", '\0', OPTION_TYPE_BOOL, offsetof(options_t, lan_discovery), &default_lan_discovery_value, sizeof(bool),
     "Scan for servers on local network via mDNS", "NETWORK", false, "ASCII_CHAT_SCAN", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},

    // WebRTC options
    {"webrtc", '\0', OPTION_TYPE_BOOL, offsetof(options_t, webrtc), &default_webrtc_value, sizeof(bool),
     "Make calls using WebRTC p2p connections", "NETWORK", false, "ASCII_CHAT_WEBRTC", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY},
    {"no-webrtc", '\0', OPTION_TYPE_BOOL, offsetof(options_t, no_webrtc), &default_no_webrtc_value, sizeof(bool),
     "Disable WebRTC, use direct TCP only", "NETWORK", false, "ASCII_CHAT_NO_WEBRTC", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"prefer-webrtc", '\0', OPTION_TYPE_BOOL, offsetof(options_t, prefer_webrtc), &default_prefer_webrtc_value,
     sizeof(bool), "Try WebRTC before direct TCP", "NETWORK", false, "ASCII_CHAT_PREFER_WEBRTC", NULL, NULL, false,
     false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"webrtc-skip-stun", '\0', OPTION_TYPE_BOOL, offsetof(options_t, webrtc_skip_stun), &default_webrtc_skip_stun_value,
     sizeof(bool), "Skip WebRTC+STUN stage, go straight to TURN relay", "NETWORK", false, "ASCII_CHAT_WEBRTC_SKIP_STUN",
     NULL, NULL, false, false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"webrtc-disable-turn", '\0', OPTION_TYPE_BOOL, offsetof(options_t, webrtc_disable_turn),
     &default_webrtc_disable_turn_value, sizeof(bool), "Disable WebRTC+TURN relay, use STUN only", "NETWORK", false,
     "ASCII_CHAT_WEBRTC_DISABLE_TURN", NULL, NULL, false, false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"stun-servers", '\0', OPTION_TYPE_STRING, offsetof(options_t, stun_servers), OPT_STUN_SERVERS_DEFAULT, 0,
     "Comma-separated list of WebRTC+STUN server URLs", "NETWORK", false, "ASCII_CHAT_STUN_SERVERS", NULL, NULL, false,
     false, OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC | OPTION_MODE_DISCOVERY},
    {"turn-servers", '\0', OPTION_TYPE_STRING, offsetof(options_t, turn_servers), OPT_TURN_SERVERS_DEFAULT, 0,
     "Comma-separated list of WebRTC+TURN server URLs", "NETWORK", false, "ASCII_CHAT_TURN_SERVERS", NULL, NULL, false,
     false, OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC | OPTION_MODE_DISCOVERY},
    {"turn-username", '\0', OPTION_TYPE_STRING, offsetof(options_t, turn_username), OPT_TURN_USERNAME_DEFAULT, 0,
     "Username for WebRTC+TURN authentication", "NETWORK", false, "ASCII_CHAT_TURN_USERNAME", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC | OPTION_MODE_DISCOVERY},
    {"turn-credential", '\0', OPTION_TYPE_STRING, offsetof(options_t, turn_credential), OPT_TURN_CREDENTIAL_DEFAULT, 0,
     "Credential/password for WebRTC+TURN authentication", "NETWORK", false, "ASCII_CHAT_TURN_CREDENTIAL", NULL, NULL,
     false, false, OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC | OPTION_MODE_DISCOVERY},
    {"turn-secret", '\0', OPTION_TYPE_STRING, offsetof(options_t, turn_secret), "", 0,
     "Shared secret for dynamic WebRTC+TURN credential generation (HMAC-SHA1)", "NETWORK", false,
     "ASCII_CHAT_TURN_SECRET", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC | OPTION_MODE_DISCOVERY},

    // Media File Streaming Options
    {"file", 'f', OPTION_TYPE_STRING, offsetof(options_t, media_file), "", 0,
     "Stream from media file or stdin (use '-' for stdin). Supported formats: see man ffmpeg-formats; codecs: see man "
     "ffmpeg-codecs",
     "MEDIA", false, "ASCII_CHAT_FILE", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"url", 'u', OPTION_TYPE_STRING, offsetof(options_t, media_url), "", 0,
     "Stream from network URL (HTTP/HTTPS/YouTube/RTSP). URL handler: see man yt-dlp; supported formats: see man "
     "ffmpeg-formats; codecs: see man ffmpeg-codecs",
     "MEDIA", false, "ASCII_CHAT_URL", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"loop", 'l', OPTION_TYPE_BOOL, offsetof(options_t, media_loop), &default_media_loop_value, sizeof(bool),
     "Loop media file playback (not supported for --url)", "MEDIA", false, "ASCII_CHAT_LOOP", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"seek", 's', OPTION_TYPE_CALLBACK, offsetof(options_t, media_seek_timestamp), &default_media_seek_value,
     sizeof(double), "Seek to timestamp before playback (format: seconds, MM:SS, or HH:MM:SS.ms)", "MEDIA", false,
     "ASCII_CHAT_SEEK", NULL, parse_timestamp, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"cookies-from-browser", '\0', OPTION_TYPE_CALLBACK, offsetof(options_t, cookies_from_browser), NULL, 0,
     "yt-dlp option (man yt-dlp). Browser for reading cookies from (chrome, firefox, edge, safari, brave, opera, "
     "vivaldi, whale). Use without argument to default to chrome.",
     "MEDIA", false, "ASCII_CHAT_COOKIES_FROM_BROWSER", NULL, parse_cookies_from_browser, false, true,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"no-cookies-from-browser", '\0', OPTION_TYPE_BOOL, offsetof(options_t, no_cookies_from_browser), false,
     sizeof(bool), "yt-dlp option (man yt-dlp). Explicitly disable reading cookies from browser", "MEDIA", false,
     "ASCII_CHAT_NO_COOKIES_FROM_BROWSER", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},

    // AUDIO GROUP (client, discovery)
    {"audio", 'A', OPTION_TYPE_BOOL, offsetof(options_t, audio_enabled), &default_audio_enabled_value, sizeof(bool),
     "Enable audio streaming", "AUDIO", false, "ASCII_CHAT_AUDIO", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"microphone-index", '\0', OPTION_TYPE_INT, offsetof(options_t, microphone_index), &default_microphone_index_value,
     sizeof(int), "Microphone device index for audio input", "AUDIO", false, "ASCII_CHAT_MICROPHONE_INDEX", NULL, NULL,
     false, false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"speakers-index", '\0', OPTION_TYPE_INT, offsetof(options_t, speakers_index), &default_speakers_index_value,
     sizeof(int), "Speakers device index to use for audio output", "AUDIO", false, "ASCII_CHAT_SPEAKERS_INDEX", NULL,
     NULL, false, false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"microphone-sensitivity", '\0', OPTION_TYPE_DOUBLE, offsetof(options_t, microphone_sensitivity),
     &default_microphone_sensitivity_value, sizeof(float), "Microphone volume multiplier (0.0-1.0)", "AUDIO", false,
     "ASCII_CHAT_MICROPHONE_SENSITIVITY", NULL, NULL, false, false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"speakers-volume", '\0', OPTION_TYPE_DOUBLE, offsetof(options_t, speakers_volume), &default_speakers_volume_value,
     sizeof(float), "Speaker volume multiplier (0.0-1.0)", "AUDIO", false, "ASCII_CHAT_SPEAKERS_VOLUME", NULL, NULL,
     false, false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
#ifdef DEBUG
    {"audio-analysis", '\0', OPTION_TYPE_BOOL, offsetof(options_t, audio_analysis_enabled),
     &default_audio_analysis_value, sizeof(bool), "Enable audio analysis (debug)", "AUDIO", false,
     "ASCII_CHAT_AUDIO_ANALYSIS", NULL, NULL, false, false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
#endif
    {"no-audio-playback", '\0', OPTION_TYPE_BOOL, offsetof(options_t, audio_no_playback),
     &default_no_audio_playback_value, sizeof(bool), "Disable speakers output", "AUDIO", false,
     "ASCII_CHAT_NO_AUDIO_PLAYBACK", NULL, NULL, false, false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"encode-audio", '\0', OPTION_TYPE_BOOL, offsetof(options_t, encode_audio), &default_encode_audio_value,
     sizeof(bool), "Enable Opus audio encoding", "AUDIO", false, "ASCII_CHAT_ENCODE_AUDIO", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"no-encode-audio", '\0', OPTION_TYPE_BOOL, offsetof(options_t, encode_audio), &default_no_encode_audio_value,
     sizeof(bool), "Disable Opus audio encoding", "AUDIO", false, "ASCII_CHAT_NO_ENCODE_AUDIO", NULL, NULL, false,
     false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"no-audio-mixer", '\0', OPTION_TYPE_BOOL, offsetof(options_t, no_audio_mixer), &default_no_audio_mixer_value,
     sizeof(bool), "Use simple audio mixing without ducking or compression (debug mode only)", "AUDIO", false,
     "ASCII_CHAT_NO_AUDIO_MIXER", NULL, NULL, false, false, OPTION_MODE_SERVER},

    // ACDS Server Specific Options
    {"database", '\0', OPTION_TYPE_STRING, offsetof(options_t, discovery_database_path), "", 0,
     "Path to SQLite database for discovery session storage", "DATABASE", false, "ASCII_CHAT_DATABASE", NULL, NULL,
     false, false, OPTION_MODE_DISCOVERY_SVC},
    {"discovery-server", '\0', OPTION_TYPE_STRING, offsetof(options_t, discovery_server),
     OPT_ENDPOINT_DISCOVERY_SERVICE, 0, "Discovery service endpoint (IP address or hostname).", "NETWORK", false,
     "ASCII_CHAT_DISCOVERY_SERVER", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY},
    {"discovery-port", '\0', OPTION_TYPE_INT, offsetof(options_t, discovery_port), &default_discovery_port_value,
     sizeof(int), "Discovery service port (1-65535)", "NETWORK", false, "ASCII_CHAT_DISCOVERY_PORT", NULL, NULL, false,
     false, OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY},
    {"discovery-expose-ip", '\0', OPTION_TYPE_BOOL, offsetof(options_t, discovery_expose_ip),
     &default_discovery_expose_ip_value, sizeof(bool),
     "Allow public IP disclosure in discovery sessions (requires confirmation)", "NETWORK", false,
     "ASCII_CHAT_DISCOVERY_EXPOSE_IP", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY},
    {"require-server-identity", '\0', OPTION_TYPE_BOOL, offsetof(options_t, require_server_identity), false,
     sizeof(bool), "Require servers to provide signed Ed25519 identity", "SECURITY", false,
     "ASCII_CHAT_REQUIRE_SERVER_IDENTITY", NULL, NULL, false, false, OPTION_MODE_DISCOVERY_SVC},
    {"require-client-identity", '\0', OPTION_TYPE_BOOL, offsetof(options_t, require_client_identity), false,
     sizeof(bool), "Require clients to provide signed Ed25519 identity", "SECURITY", false,
     "ASCII_CHAT_REQUIRE_CLIENT_IDENTITY", NULL, NULL, false, false, OPTION_MODE_DISCOVERY_SVC},

    // Generic placeholder to mark end of array
    {NULL, '\0', OPTION_TYPE_BOOL, 0, NULL, 0, NULL, NULL, false, NULL, NULL, NULL, false, false, OPTION_MODE_NONE}};

static size_t g_registry_size = 0;
static bool g_metadata_populated = false;

// Permanent metadata cache - prevents metadata from being lost on subsequent descriptor lookups
typedef struct {
  const char *long_name;
  option_metadata_t metadata;
} cached_metadata_t;

#define MAX_CACHED_METADATA 200
static cached_metadata_t g_metadata_cache[MAX_CACHED_METADATA];
static size_t g_metadata_cache_count = 0;

static void cache_metadata(const char *long_name, const option_metadata_t *meta) {
  if (!long_name || !meta || g_metadata_cache_count >= MAX_CACHED_METADATA) {
    return;
  }
  // Check if already cached
  for (size_t i = 0; i < g_metadata_cache_count; i++) {
    if (strcmp(g_metadata_cache[i].long_name, long_name) == 0) {
      g_metadata_cache[i].metadata = *meta;
      return;
    }
  }
  // Add new entry
  g_metadata_cache[g_metadata_cache_count].long_name = long_name;
  g_metadata_cache[g_metadata_cache_count].metadata = *meta;
  g_metadata_cache_count++;
}

static const option_metadata_t *get_cached_metadata(const char *long_name) {
  if (!long_name) {
    return NULL;
  }
  for (size_t i = 0; i < g_metadata_cache_count; i++) {
    if (strcmp(g_metadata_cache[i].long_name, long_name) == 0) {
      return &g_metadata_cache[i].metadata;
    }
  }
  return NULL;
}

// ============================================================================
// Metadata Initialization for Critical Options (forward implementation)
// ============================================================================

/**
 * @brief Populate metadata for critical options
 *
 * This function initializes completion metadata for options that benefit
 * from smart completion generation (enums, numeric ranges, examples).
 *
 * Called during registry initialization to set up metadata for:
 * - color-mode: enum values with descriptions
 * - compression-level: numeric range (1-9)
 * - fps: numeric range with examples
 * - palette: enum values
 * - etc.
 */
static void registry_populate_metadata_for_critical_options(void);

/**
 * @brief Initialize registry size and metadata
 */
static void registry_init_size(void) {
  if (g_registry_size == 0) {
    for (size_t i = 0; g_options_registry[i].long_name != NULL; i++) {
      g_registry_size++;
    }
    // Populate metadata after registry is sized
    registry_populate_metadata_for_critical_options();
    g_metadata_populated = true;
  }
}

asciichat_error_t options_registry_add_all_to_builder(options_builder_t *builder) {
  if (!builder) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Builder is NULL");
  }

  registry_init_size();

  for (size_t i = 0; i < g_registry_size; i++) {
    const registry_entry_t *entry = &g_options_registry[i];
    if (!entry->long_name) {
      continue;
    }
    // Silent - no debug logging needed

    switch (entry->type) {
    case OPTION_TYPE_STRING:
      options_builder_add_string(builder, entry->long_name, entry->short_name, entry->offset,
                                 entry->default_value ? (const char *)entry->default_value : "", entry->help_text,
                                 entry->group, entry->required, entry->env_var_name, entry->validate_fn);
      break;
    case OPTION_TYPE_INT:
      options_builder_add_int(builder, entry->long_name, entry->short_name, entry->offset,
                              entry->default_value ? *(const int *)entry->default_value : 0, entry->help_text,
                              entry->group, entry->required, entry->env_var_name, entry->validate_fn);
      break;
    case OPTION_TYPE_BOOL:
      options_builder_add_bool(builder, entry->long_name, entry->short_name, entry->offset,
                               entry->default_value ? *(const bool *)entry->default_value : false, entry->help_text,
                               entry->group, entry->required, entry->env_var_name);
      break;
    case OPTION_TYPE_DOUBLE:
      options_builder_add_double(builder, entry->long_name, entry->short_name, entry->offset,
                                 entry->default_value ? *(const double *)entry->default_value : 0.0, entry->help_text,
                                 entry->group, entry->required, entry->env_var_name, entry->validate_fn);
      break;
    case OPTION_TYPE_CALLBACK:
      if (entry->optional_arg) {
        options_builder_add_callback_optional(builder, entry->long_name, entry->short_name, entry->offset,
                                              entry->default_value, entry->default_value_size, entry->parse_fn,
                                              entry->help_text, entry->group, entry->required, entry->env_var_name,
                                              entry->optional_arg);
      } else {
        options_builder_add_callback(builder, entry->long_name, entry->short_name, entry->offset,
                                     entry->default_value ? entry->default_value : NULL, entry->default_value_size,
                                     entry->parse_fn, entry->help_text, entry->group, entry->required,
                                     entry->env_var_name);
      }
      break;
    case OPTION_TYPE_ACTION:
      // Actions are now registered as options with help text
      // Look up the corresponding action function based on option name
      if (strcmp(entry->long_name, "list-webcams") == 0) {
        options_builder_add_action(builder, entry->long_name, entry->short_name, action_list_webcams, entry->help_text,
                                   entry->group);
      } else if (strcmp(entry->long_name, "list-microphones") == 0) {
        options_builder_add_action(builder, entry->long_name, entry->short_name, action_list_microphones,
                                   entry->help_text, entry->group);
      } else if (strcmp(entry->long_name, "list-speakers") == 0) {
        options_builder_add_action(builder, entry->long_name, entry->short_name, action_list_speakers, entry->help_text,
                                   entry->group);
      }
      break;
    }

    // Set mode bitmask on the last added descriptor
    options_builder_set_mode_bitmask(builder, entry->mode_bitmask);
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Get a registry entry by long name
 * @note This is used internally for option lookup
 */
static const registry_entry_t *registry_find_entry_by_name(const char *long_name) {
  if (!long_name) {
    return NULL;
  }

  for (size_t i = 0; g_options_registry[i].long_name != NULL; i++) {
    if (strcmp(g_options_registry[i].long_name, long_name) == 0) {
      return &g_options_registry[i];
    }
  }
  return NULL;
}

/**
 * @brief Get a registry entry by short name
 * @note This is used internally for option lookup
 */
static const registry_entry_t *registry_find_entry_by_short(char short_name) {
  if (short_name == '\0') {
    return NULL;
  }

  for (size_t i = 0; g_options_registry[i].long_name != NULL; i++) {
    if (g_options_registry[i].short_name == short_name) {
      return &g_options_registry[i];
    }
  }
  return NULL;
}

/**
 * @brief Get raw access to registry for completions filtering
 *
 * Returns a pointer to the internal registry array. The array is NULL-terminated
 * (final entry has long_name == NULL). Used by completions generators.
 *
 * @return Pointer to registry array (read-only), or NULL on error
 */
const registry_entry_t *options_registry_get_raw(void) {
  registry_init_size();
  return g_options_registry;
}

/**
 * @brief Get total number of registry entries
 *
 * @return Number of options in registry (not including NULL terminator)
 */
size_t options_registry_get_count(void) {
  registry_init_size();
  return g_registry_size;
}

const option_descriptor_t *options_registry_find_by_name(const char *long_name) {
  if (!long_name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Long name is NULL");
    return NULL;
  }

  registry_init_size();

  const registry_entry_t *entry = registry_find_entry_by_name(long_name);
  if (!entry) {
    // Don't log error for binary-level options like "config" that aren't in registry
    if (strcmp(long_name, "config") != 0) {
      SET_ERRNO(ERROR_NOT_FOUND, "Option not found: %s", long_name);
    }
    return NULL;
  }

  /* Create descriptor from registry entry */
  static option_descriptor_t desc;
  desc.long_name = entry->long_name;
  desc.short_name = entry->short_name;
  desc.type = entry->type;
  desc.offset = entry->offset;
  desc.help_text = entry->help_text;
  desc.group = entry->group;
  desc.hide_from_mode_help = false;
  desc.hide_from_binary_help = false;
  desc.default_value = entry->default_value;
  desc.required = entry->required;
  desc.env_var_name = entry->env_var_name;
  desc.validate = entry->validate_fn;
  desc.parse_fn = entry->parse_fn;
  desc.action_fn = NULL;
  desc.owns_memory = entry->owns_memory;
  desc.optional_arg = entry->optional_arg;
  desc.mode_bitmask = entry->mode_bitmask;

  return &desc;
}

const option_descriptor_t *options_registry_find_by_short(char short_name) {
  if (short_name == '\0') {
    SET_ERRNO(ERROR_INVALID_PARAM, "Short name is empty");
    return NULL;
  }

  registry_init_size();

  const registry_entry_t *entry = registry_find_entry_by_short(short_name);
  if (!entry) {
    SET_ERRNO(ERROR_NOT_FOUND, "Option with short name '%c' not found", short_name);
    return NULL;
  }

  /* Create descriptor from registry entry */
  static option_descriptor_t desc;
  desc.long_name = entry->long_name;
  desc.short_name = entry->short_name;
  desc.type = entry->type;
  desc.offset = entry->offset;
  desc.help_text = entry->help_text;
  desc.group = entry->group;
  desc.hide_from_mode_help = false;
  desc.hide_from_binary_help = false;
  desc.default_value = entry->default_value;
  desc.required = entry->required;
  desc.env_var_name = entry->env_var_name;
  desc.validate = entry->validate_fn;
  desc.parse_fn = entry->parse_fn;
  desc.action_fn = NULL;
  desc.owns_memory = entry->owns_memory;
  desc.optional_arg = entry->optional_arg;
  desc.mode_bitmask = entry->mode_bitmask;

  return &desc;
}

/**
 * @brief Convert registry entry to option descriptor
 */
static option_descriptor_t registry_entry_to_descriptor(const registry_entry_t *entry) {
  option_descriptor_t desc = {0};
  if (entry) {
    desc.long_name = entry->long_name;
    desc.short_name = entry->short_name;
    desc.type = entry->type;
    desc.offset = entry->offset;
    desc.help_text = entry->help_text;
    desc.group = entry->group;
    desc.hide_from_mode_help = false;
    // Hide discovery service options from binary-level help (they're for discovery-service mode only)
    desc.hide_from_binary_help = (entry->mode_bitmask == OPTION_MODE_DISCOVERY_SVC);
    desc.default_value = entry->default_value;
    desc.required = entry->required;
    desc.env_var_name = entry->env_var_name;
    desc.validate = entry->validate_fn;
    desc.parse_fn = entry->parse_fn;
    desc.action_fn = NULL;
    desc.owns_memory = entry->owns_memory;
    desc.optional_arg = entry->optional_arg;
    desc.mode_bitmask = entry->mode_bitmask;
  }
  return desc;
}

const option_descriptor_t *options_registry_get_for_mode(asciichat_mode_t mode, size_t *num_options) {
  if (!num_options) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Number of options is NULL");
    return NULL;
  }

  registry_init_size();

  /* Convert mode to bitmask */
  option_mode_bitmask_t mode_bitmask = 0;
  switch (mode) {
  case MODE_SERVER:
    mode_bitmask = OPTION_MODE_SERVER;
    break;
  case MODE_CLIENT:
    mode_bitmask = OPTION_MODE_CLIENT;
    break;
  case MODE_MIRROR:
    mode_bitmask = OPTION_MODE_MIRROR;
    break;
  case MODE_DISCOVERY_SERVER:
    mode_bitmask = OPTION_MODE_DISCOVERY_SVC;
    break;
  case MODE_DISCOVERY:
    mode_bitmask = OPTION_MODE_DISCOVERY;
    break;
  default:
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid mode: %d", mode);
    *num_options = 0;
    return NULL;
  }

  /* Count matching options */
  size_t count = 0;
  for (size_t i = 0; i < g_registry_size; i++) {
    if (g_options_registry[i].mode_bitmask & mode_bitmask) {
      count++;
    }
  }

  if (count == 0) {
    *num_options = 0;
    return NULL;
  }

  /* Allocate array for matching options */
  option_descriptor_t *filtered = SAFE_MALLOC(count * sizeof(option_descriptor_t), option_descriptor_t *);
  if (!filtered) {
    SET_ERRNO(ERROR_INVALID_STATE, "Failed to allocate filtered options array");
    *num_options = 0;
    return NULL;
  }

  /* Copy matching options */
  size_t idx = 0;
  for (size_t i = 0; i < g_registry_size; i++) {
    if (g_options_registry[i].mode_bitmask & mode_bitmask) {
      filtered[idx++] = registry_entry_to_descriptor(&g_options_registry[i]);
    }
  }

  *num_options = count;
  return filtered;
}

const option_descriptor_t *options_registry_get_binary_options(size_t *num_options) {
  if (!num_options) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Number of options is NULL");
    return NULL;
  }

  registry_init_size();

  /* Count binary-level options */
  size_t count = 0;
  for (size_t i = 0; i < g_registry_size; i++) {
    if (g_options_registry[i].mode_bitmask & OPTION_MODE_BINARY) {
      count++;
    }
  }

  if (count == 0) {
    *num_options = 0;
    return NULL;
  }

  /* Allocate array for binary options */
  option_descriptor_t *binary_opts = SAFE_MALLOC(count * sizeof(option_descriptor_t), option_descriptor_t *);
  if (!binary_opts) {
    SET_ERRNO(ERROR_INVALID_STATE, "Failed to allocate binary options array");
    *num_options = 0;
    return NULL;
  }

  /* Copy binary options */
  size_t idx = 0;
  for (size_t i = 0; i < g_registry_size; i++) {
    if (g_options_registry[i].mode_bitmask & OPTION_MODE_BINARY) {
      binary_opts[idx++] = registry_entry_to_descriptor(&g_options_registry[i]);
    }
  }

  *num_options = count;
  return binary_opts;
}

/**
 * @brief Check if an option applies to the given mode for display purposes
 *
 * This implements the same filtering logic as the help system's option_applies_to_mode().
 * Used by options_registry_get_for_display() to ensure completions match help output.
 *
 * @param entry Registry entry to check
 * @param mode Mode to check (use MODE_DISCOVERY for binary help)
 * @param for_binary_help If true, show all options for any mode; if false, filter by mode
 * @return true if option should be displayed for this mode
 */
static bool registry_entry_applies_to_mode(const registry_entry_t *entry, asciichat_mode_t mode, bool for_binary_help) {
  if (!entry) {
    return false;
  }

  // Hardcoded list of options to hide from binary help (matches builder.c line 752)
  // These are options that have hide_from_binary_help=true set in builder.c
  const char *hidden_from_binary[] = {"create-man-page", // Development tool, hidden from help
                                      NULL};

  // When for_binary_help is true (i.e., for 'ascii-chat --help'),
  // we want to show all options that apply to any mode, plus binary-level options.
  if (for_binary_help) {
    // Check if this option is explicitly hidden from binary help
    for (int i = 0; hidden_from_binary[i] != NULL; i++) {
      if (strcmp(entry->long_name, hidden_from_binary[i]) == 0) {
        return false; // Hidden from binary help
      }
    }

    // An option applies if its mode_bitmask has any bit set for any valid mode.
    // OPTION_MODE_ALL is a bitmask of all modes (including OPTION_MODE_BINARY).
    return (entry->mode_bitmask & OPTION_MODE_ALL) != 0;
  }

  // For mode-specific help, show only options for that mode.
  // Do not show binary options here unless it also specifically applies to the mode.
  if (mode < 0 || mode > MODE_DISCOVERY) {
    return false;
  }
  option_mode_bitmask_t mode_bit = (1 << mode);

  // Check if it's a binary option. If so, only show if it also explicitly applies to this mode.
  if ((entry->mode_bitmask & OPTION_MODE_BINARY) && !(entry->mode_bitmask & mode_bit)) {
    return false; // Binary options not shown in mode-specific help unless also mode-specific
  }

  return (entry->mode_bitmask & mode_bit) != 0;
}

const option_descriptor_t *options_registry_get_for_display(asciichat_mode_t mode, bool for_binary_help,
                                                            size_t *num_options) {
  if (!num_options) {
    SET_ERRNO(ERROR_INVALID_PARAM, "num_options is NULL");
    return NULL;
  }

  registry_init_size();

  // Count matching options
  size_t count = 0;
  for (size_t i = 0; i < g_registry_size; i++) {
    if (registry_entry_applies_to_mode(&g_options_registry[i], mode, for_binary_help)) {
      count++;
    }
  }

  if (count == 0) {
    *num_options = 0;
    return NULL;
  }

  // Allocate array
  option_descriptor_t *descriptors = SAFE_MALLOC(count * sizeof(option_descriptor_t), option_descriptor_t *);
  if (!descriptors) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate descriptors array");
    *num_options = 0;
    return NULL;
  }

  // Copy matching options
  size_t idx = 0;
  for (size_t i = 0; i < g_registry_size; i++) {
    if (registry_entry_applies_to_mode(&g_options_registry[i], mode, for_binary_help)) {
      descriptors[idx++] = registry_entry_to_descriptor(&g_options_registry[i]);
    }
  }

  *num_options = count;
  return descriptors;
}

// ============================================================================
// Completion Metadata Access (Phase 3 Implementation)
// ============================================================================

const option_metadata_t *options_registry_get_metadata(const char *long_name) {
  if (!long_name) {
    return NULL;
  }

  // Check cache first (faster and preserves metadata across calls)
  const option_metadata_t *cached = get_cached_metadata(long_name);
  if (cached) {
    return cached;
  }

  // If not in cache, return empty metadata
  static option_metadata_t empty_metadata = {0};
  return &empty_metadata;
}

const char **options_registry_get_enum_values(const char *option_name, const char ***descriptions, size_t *count) {
  if (!option_name || !count) {
    if (count)
      *count = 0;
    return NULL;
  }

  const option_metadata_t *meta = options_registry_get_metadata(option_name);
  if (!meta || meta->input_type != OPTION_INPUT_ENUM || meta->enum_count == 0) {
    *count = 0;
    if (descriptions)
      *descriptions = NULL;
    return NULL;
  }

  *count = meta->enum_count;
  if (descriptions) {
    *descriptions = meta->enum_descriptions;
  }
  return meta->enum_values;
}

bool options_registry_get_numeric_range(const char *option_name, int *min_out, int *max_out, int *step_out) {
  if (!option_name || !min_out || !max_out || !step_out) {
    return false;
  }

  const option_metadata_t *meta = options_registry_get_metadata(option_name);
  if (!meta || meta->input_type != OPTION_INPUT_NUMERIC) {
    *min_out = 0;
    *max_out = 0;
    *step_out = 0;
    return false;
  }

  *min_out = meta->numeric_range.min;
  *max_out = meta->numeric_range.max;
  *step_out = meta->numeric_range.step;
  return true;
}

const char **options_registry_get_examples(const char *option_name, size_t *count) {
  if (!option_name || !count) {
    if (count)
      *count = 0;
    return NULL;
  }

  const option_metadata_t *meta = options_registry_get_metadata(option_name);
  if (!meta || meta->example_count == 0) {
    *count = 0;
    return NULL;
  }

  *count = meta->example_count;
  return meta->examples;
}

option_input_type_t options_registry_get_input_type(const char *option_name) {
  if (!option_name) {
    return OPTION_INPUT_NONE;
  }

  const option_metadata_t *meta = options_registry_get_metadata(option_name);
  if (!meta) {
    return OPTION_INPUT_NONE;
  }

  return meta->input_type;
}

// ============================================================================
// Metadata Initialization for Critical Options
// ============================================================================

/**
 * @brief Populate metadata for critical options
 *
 * This function initializes completion metadata for options that benefit
 * from smart completion generation (enums, numeric ranges, examples).
 *
 * Called during registry initialization to set up metadata for:
 * - color-mode: enum values with descriptions
 * - compression-level: numeric range (1-9)
 * - fps: numeric range with examples
 * - palette: enum values
 * - etc.
 */
static void registry_populate_metadata_for_critical_options(void) {
  // Color mode enum values
  {
    static const char *color_mode_values[] = {"auto", "none", "16", "256", "truecolor"};
    static const char *color_mode_descs[] = {"Auto-detect from terminal", "Monochrome only", "16 colors (ANSI)",
                                             "256 colors (xterm)", "24-bit truecolor (modern terminals)"};
    option_metadata_t meta = {0};
    meta.enum_values = color_mode_values;
    meta.enum_count = 5;
    meta.enum_descriptions = color_mode_descs;
    meta.input_type = OPTION_INPUT_ENUM;
    cache_metadata("color-mode", &meta);
  }

  // Compression level numeric range
  {
    static const char *compress_examples[] = {"1", "3", "9"};
    option_metadata_t meta = {0};
    meta.numeric_range.min = 1;
    meta.numeric_range.max = 9;
    meta.numeric_range.step = 1;
    meta.input_type = OPTION_INPUT_NUMERIC;
    meta.examples = compress_examples;
    meta.example_count = 3;
    cache_metadata("compression-level", &meta);
  }

  // FPS numeric range
  {
    static const char *fps_examples[] = {"30", "60", "144"};
    option_metadata_t meta = {0};
    meta.numeric_range.min = 1;
    meta.numeric_range.max = 144;
    meta.numeric_range.step = 0;
    meta.input_type = OPTION_INPUT_NUMERIC;
    meta.examples = fps_examples;
    meta.example_count = 3;
    cache_metadata("fps", &meta);
  }

  // Palette enum values
  {
    static const char *palette_values[] = {"standard", "blocks", "digital", "minimal", "cool", "custom"};
    static const char *palette_descs[] = {"Standard ASCII palette", "Block characters (full/half/quarter blocks)",
                                          "Digital/computer style", "Minimal palette (light aesthetic)",
                                          "Cool/modern style",      "Custom user-defined characters"};
    option_metadata_t meta = {0};
    meta.enum_values = palette_values;
    meta.enum_count = 6;
    meta.enum_descriptions = palette_descs;
    meta.input_type = OPTION_INPUT_ENUM;
    cache_metadata("palette", &meta);
  }

  // Render mode enum values
  {
    static const char *render_values[] = {"foreground", "fg", "background", "bg", "half-block"};
    static const char *render_descs[] = {
        "Render using foreground characters only", "Render using foreground characters only (alias)",
        "Render using background colors only", "Render using background colors only (alias)",
        "Use half-block characters for 2x vertical resolution"};
    option_metadata_t meta = {0};
    meta.enum_values = render_values;
    meta.enum_count = 5;
    meta.enum_descriptions = render_descs;
    meta.input_type = OPTION_INPUT_ENUM;
    cache_metadata("render-mode", &meta);
  }

  // Log level enum values
  {
    static const char *log_level_values[] = {"dev", "debug", "info", "warn", "error", "fatal"};
    static const char *log_level_descs[] = {"Development (most verbose, includes function traces)",
                                            "Debug (includes internal state tracking)",
                                            "Informational (key lifecycle events)",
                                            "Warnings (unusual conditions)",
                                            "Errors only",
                                            "Fatal errors only"};
    option_metadata_t meta = {0};
    meta.enum_values = log_level_values;
    meta.enum_count = 6;
    meta.enum_descriptions = log_level_descs;
    meta.input_type = OPTION_INPUT_ENUM;
    cache_metadata("log-level", &meta);
  }

  // File path options
  option_descriptor_t *logfile_desc = (option_descriptor_t *)options_registry_find_by_name("log-file");
  if (logfile_desc) {
    logfile_desc->metadata.input_type = OPTION_INPUT_FILEPATH;
  }

  option_descriptor_t *keyfile_desc = (option_descriptor_t *)options_registry_find_by_name("key");
  if (keyfile_desc) {
    keyfile_desc->metadata.input_type = OPTION_INPUT_FILEPATH;
  }

  option_descriptor_t *config_desc = (option_descriptor_t *)options_registry_find_by_name("config");
  if (config_desc) {
    config_desc->metadata.input_type = OPTION_INPUT_FILEPATH;
  } else {
    CLEAR_ERRNO(); // "config" is a binary-level option, not in registry - suppress spurious error
  }

  // STUN servers and TURN servers are lists
  option_descriptor_t *stun_desc = (option_descriptor_t *)options_registry_find_by_name("stun-servers");
  if (stun_desc) {
    stun_desc->metadata.input_type = OPTION_INPUT_STRING;
    stun_desc->metadata.is_list = true;
  }

  option_descriptor_t *turn_desc = (option_descriptor_t *)options_registry_find_by_name("turn-servers");
  if (turn_desc) {
    turn_desc->metadata.input_type = OPTION_INPUT_STRING;
    turn_desc->metadata.is_list = true;
  }

  // Microphone and speaker indices
  option_descriptor_t *mic_idx_desc = (option_descriptor_t *)options_registry_find_by_name("microphone-index");
  if (mic_idx_desc) {
    mic_idx_desc->metadata.numeric_range.min = -1;
    mic_idx_desc->metadata.numeric_range.max = 0; // 0 for max (no real limit)
    mic_idx_desc->metadata.numeric_range.step = 1;
    mic_idx_desc->metadata.input_type = OPTION_INPUT_NUMERIC;
  }

  option_descriptor_t *speaker_idx_desc = (option_descriptor_t *)options_registry_find_by_name("speakers-index");
  if (speaker_idx_desc) {
    speaker_idx_desc->metadata.numeric_range.min = -1;
    speaker_idx_desc->metadata.numeric_range.max = 0; // 0 for max (no real limit)
    speaker_idx_desc->metadata.numeric_range.step = 1;
    speaker_idx_desc->metadata.input_type = OPTION_INPUT_NUMERIC;
  }

  // Webcam device index
  option_descriptor_t *webcam_idx_desc = (option_descriptor_t *)options_registry_find_by_name("webcam-index");
  if (webcam_idx_desc) {
    webcam_idx_desc->metadata.numeric_range.min = 0;
    webcam_idx_desc->metadata.numeric_range.max = 0; // 0 for max (no real limit)
    webcam_idx_desc->metadata.numeric_range.step = 1;
    webcam_idx_desc->metadata.input_type = OPTION_INPUT_NUMERIC;
  }

  // Port option
  option_descriptor_t *port_desc = (option_descriptor_t *)options_registry_find_by_name("port");
  if (port_desc) {
    port_desc->metadata.numeric_range.min = 1;
    port_desc->metadata.numeric_range.max = 65535;
    port_desc->metadata.numeric_range.step = 0; // Continuous
    port_desc->metadata.input_type = OPTION_INPUT_NUMERIC;
  }

  // Width with practical examples
  {
    static const char *width_examples[] = {"80", "120", "160"};
    option_metadata_t meta = {0};
    meta.numeric_range.min = 20;
    meta.numeric_range.max = 512;
    meta.numeric_range.step = 0;
    meta.input_type = OPTION_INPUT_NUMERIC;
    meta.examples = width_examples;
    meta.example_count = 3;
    cache_metadata("width", &meta);
  }

  // Height with practical examples
  {
    static const char *height_examples[] = {"24", "40", "60"};
    option_metadata_t meta = {0};
    meta.numeric_range.min = 10;
    meta.numeric_range.max = 256;
    meta.numeric_range.step = 0;
    meta.input_type = OPTION_INPUT_NUMERIC;
    meta.examples = height_examples;
    meta.example_count = 3;
    cache_metadata("height", &meta);
  }

  // Max clients with practical examples
  {
    static const char *maxclients_examples[] = {"2", "4", "8"};
    option_metadata_t meta = {0};
    meta.numeric_range.min = 1;
    meta.numeric_range.max = 99;
    meta.numeric_range.step = 1;
    meta.input_type = OPTION_INPUT_NUMERIC;
    meta.examples = maxclients_examples;
    meta.example_count = 3;
    cache_metadata("max-clients", &meta);
  }

  // Reconnect attempts with practical examples
  {
    static const char *reconnect_examples[] = {"0", "5", "10"};
    option_metadata_t meta = {0};
    meta.numeric_range.min = -1;
    meta.numeric_range.max = 99;
    meta.numeric_range.step = 1;
    meta.input_type = OPTION_INPUT_NUMERIC;
    meta.examples = reconnect_examples;
    meta.example_count = 3;
    cache_metadata("reconnect-attempts", &meta);
  }

  // Webcam index with practical examples
  {
    static const char *webcam_examples[] = {"0", "1", "2"};
    option_metadata_t meta = {0};
    meta.numeric_range.min = 0;
    meta.numeric_range.max = 10;
    meta.numeric_range.step = 1;
    meta.input_type = OPTION_INPUT_NUMERIC;
    meta.examples = webcam_examples;
    meta.example_count = 3;
    cache_metadata("webcam-index", &meta);
  }

  // Microphone index with practical examples
  {
    static const char *mic_examples[] = {"-1", "0", "1"};
    option_metadata_t meta = {0};
    meta.numeric_range.min = -1;
    meta.numeric_range.max = 10;
    meta.numeric_range.step = 1;
    meta.input_type = OPTION_INPUT_NUMERIC;
    meta.examples = mic_examples;
    meta.example_count = 3;
    cache_metadata("microphone-index", &meta);
  }

  // Speakers index with practical examples
  {
    static const char *speakers_examples[] = {"-1", "0", "1"};
    option_metadata_t meta = {0};
    meta.numeric_range.min = -1;
    meta.numeric_range.max = 10;
    meta.numeric_range.step = 1;
    meta.input_type = OPTION_INPUT_NUMERIC;
    meta.examples = speakers_examples;
    meta.example_count = 3;
    cache_metadata("speakers-index", &meta);
  }

  // Cookies from browser enum
  {
    static const char *cookies_values[] = {"chrome", "firefox", "edge", "safari", "brave", "opera", "vivaldi", "whale"};
    option_metadata_t meta = {0};
    meta.enum_values = cookies_values;
    meta.enum_count = 8;
    meta.input_type = OPTION_INPUT_ENUM;
    cache_metadata("cookies-from-browser", &meta);
  }

  // Seek timestamp examples
  {
    static const char *seek_examples[] = {"0", "60", "3:45"};
    option_metadata_t meta = {0};
    meta.input_type = OPTION_INPUT_STRING;
    meta.examples = seek_examples;
    meta.example_count = 3;
    cache_metadata("seek", &meta);
  }
}
