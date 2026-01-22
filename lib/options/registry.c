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
#include "options/common.h"
#include "common.h"
#include "log/logging.h"
#include "platform/terminal.h"
#include "video/palette.h"

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
// Static Default Values for Non-String Types
// ============================================================================
// These small wrappers are needed because registry_entry_t uses const void * for
// default_value. This replaces the old g_default_* approach - we now have just one
// variable per unique default value, automatically used by all options sharing it.

static const int default_log_level_value = DEFAULT_LOG_LEVEL;
static const int default_width_value = OPT_WIDTH_DEFAULT;
static const int default_height_value = OPT_HEIGHT_DEFAULT;
static const int default_webcam_index_value = OPT_WEBCAM_INDEX_DEFAULT;
static const bool default_webcam_flip_value = OPT_WEBCAM_FLIP_DEFAULT;
static const bool default_test_pattern_value = OPT_TEST_PATTERN_DEFAULT;
static const int default_color_mode_value = OPT_COLOR_MODE_DEFAULT;
static const int default_render_mode_value = OPT_RENDER_MODE_DEFAULT;
static const int default_palette_type_value = OPT_PALETTE_TYPE_DEFAULT;
static const bool default_show_capabilities_value = OPT_SHOW_CAPABILITIES_DEFAULT;
static const bool default_force_utf8_value = OPT_FORCE_UTF8_DEFAULT;
static const bool default_stretch_value = OPT_STRETCH_DEFAULT;
static const bool default_strip_ansi_value = OPT_STRIP_ANSI_DEFAULT;
static const bool default_snapshot_mode_value = OPT_SNAPSHOT_MODE_DEFAULT;
static const double default_snapshot_delay_value = SNAPSHOT_DELAY_DEFAULT;
static const int default_compression_level_value = OPT_COMPRESSION_LEVEL_DEFAULT;
static const bool default_no_compress_value = OPT_NO_COMPRESS_DEFAULT;
static const bool default_encrypt_enabled_value = OPT_ENCRYPT_ENABLED_DEFAULT;
static const bool default_no_encrypt_value = OPT_NO_ENCRYPT_DEFAULT;
static const int default_max_clients_value = OPT_MAX_CLIENTS_DEFAULT;
static const int default_reconnect_attempts_value = OPT_RECONNECT_ATTEMPTS_DEFAULT;
static const int default_discovery_port_value = OPT_ACDS_PORT_INT_DEFAULT;
static const bool default_webrtc_value = OPT_WEBRTC_DEFAULT;
static const bool default_enable_upnp_value = OPT_ENABLE_UPNP_DEFAULT;
static const bool default_lan_discovery_value = OPT_LAN_DISCOVERY_DEFAULT;
static const bool default_prefer_webrtc_value = OPT_PREFER_WEBRTC_DEFAULT;
static const bool default_no_webrtc_value = OPT_NO_WEBRTC_DEFAULT;
static const bool default_webrtc_skip_stun_value = OPT_WEBRTC_SKIP_STUN_DEFAULT;
static const bool default_webrtc_disable_turn_value = OPT_WEBRTC_DISABLE_TURN_DEFAULT;
static const bool default_media_loop_value = OPT_MEDIA_LOOP_DEFAULT;
static const double default_media_seek_value = OPT_MEDIA_SEEK_TIMESTAMP_DEFAULT;
static const bool default_no_cookies_value = OPT_NO_COOKIES_FROM_BROWSER_DEFAULT;
static const bool default_audio_enabled_value = OPT_AUDIO_ENABLED_DEFAULT;
static const int default_microphone_index_value = OPT_MICROPHONE_INDEX_DEFAULT;
static const int default_speakers_index_value = OPT_SPEAKERS_INDEX_DEFAULT;
static const float default_microphone_sensitivity_value = OPT_MICROPHONE_SENSITIVITY_DEFAULT;
static const float default_speakers_volume_value = OPT_SPEAKERS_VOLUME_DEFAULT;
static const bool default_audio_analysis_value = OPT_AUDIO_ANALYSIS_ENABLED_DEFAULT;
static const bool default_no_audio_playback_value = OPT_AUDIO_NO_PLAYBACK_DEFAULT;
static const bool default_encode_audio_value = OPT_ENCODE_AUDIO_DEFAULT;
static const bool default_no_encode_audio_value = !OPT_ENCODE_AUDIO_DEFAULT;
static const bool default_discovery_expose_ip_value = OPT_ACDS_EXPOSE_IP_DEFAULT;
static const bool default_discovery_insecure_value = OPT_ACDS_INSECURE_DEFAULT;
static const bool default_require_server_identity_value = OPT_REQUIRE_SERVER_IDENTITY_DEFAULT;
static const bool default_require_client_identity_value = OPT_REQUIRE_CLIENT_IDENTITY_DEFAULT;

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
    {"log-file", 'L', OPTION_TYPE_STRING, offsetof(options_t, log_file), "", 0, "Redirect logs to FILE", "LOGGING",
     false, "ASCII_CHAT_LOG_FILE", NULL, NULL, false, false, OPTION_MODE_BINARY},
    {"log-level", '\0', OPTION_TYPE_CALLBACK, offsetof(options_t, log_level), &default_log_level_value,
     sizeof(log_level_t), "Set log level: dev, debug, info, warn, error, fatal", "LOGGING", false, NULL, NULL,
     parse_log_level, false, false, OPTION_MODE_BINARY},
    {"verbose", 'V', OPTION_TYPE_CALLBACK, offsetof(options_t, verbose_level), 0, sizeof(unsigned short int),
     "Increase log verbosity (stackable: -VV, -VVV, or --verbose)", "LOGGING", false, NULL, NULL, parse_verbose_flag,
     false, true, OPTION_MODE_BINARY},
    {"quiet", 'q', OPTION_TYPE_BOOL, offsetof(options_t, quiet), OPT_QUIET_DEFAULT, sizeof(bool),
     "Disable console logging (log to file only)", "LOGGING", false, NULL, NULL, NULL, NULL, false, false,
     OPTION_MODE_BINARY},

    // TERMINAL GROUP (client, mirror, discovery)
    {"width", 'x', OPTION_TYPE_INT, offsetof(options_t, width), &default_width_value, sizeof(int),
     "Terminal width in characters", "TERMINAL", false, "ASCII_CHAT_WIDTH", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"height", 'y', OPTION_TYPE_INT, offsetof(options_t, height), &default_height_value, sizeof(int),
     "Terminal height in characters", "TERMINAL", false, "ASCII_CHAT_HEIGHT", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},

    // WEBCAM GROUP (client, mirror, discovery)
    {"webcam-index", 'c', OPTION_TYPE_INT, offsetof(options_t, webcam_index), &default_webcam_index_value,
     sizeof(unsigned short int), "Webcam device index", "WEBCAM", false, "ASCII_CHAT_WEBCAM_INDEX", NULL, NULL, false,
     false, OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"webcam-flip", 'g', OPTION_TYPE_BOOL, offsetof(options_t, webcam_flip), &default_webcam_flip_value, sizeof(bool),
     "Flip webcam horizontally", "WEBCAM", false, NULL, NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"test-pattern", '\0', OPTION_TYPE_BOOL, offsetof(options_t, test_pattern), &default_test_pattern_value,
     sizeof(bool), "Use test pattern instead of webcam", "WEBCAM", false, "WEBCAM_DISABLED", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},

    // DISPLAY GROUP (client, mirror, discovery)
    {"color-mode", '\0', OPTION_TYPE_CALLBACK, offsetof(options_t, color_mode), &default_color_mode_value,
     sizeof(terminal_color_mode_t), "Terminal color level (auto, none, 16, 256, truecolor)", "DISPLAY", false,
     "ASCII_CHAT_COLOR_MODE", NULL, parse_color_mode, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"render-mode", 'M', OPTION_TYPE_CALLBACK, offsetof(options_t, render_mode), &default_render_mode_value,
     sizeof(render_mode_t), "Render mode (foreground, background, half-block)", "DISPLAY", false,
     "ASCII_CHAT_RENDER_MODE", NULL, parse_render_mode, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"palette", 'P', OPTION_TYPE_CALLBACK, offsetof(options_t, palette_type), &default_palette_type_value,
     sizeof(palette_type_t), "ASCII palette type (standard, blocks, digital, minimal, cool, custom)", "DISPLAY", false,
     "ASCII_CHAT_PALETTE", NULL, parse_palette_type, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"palette-chars", 'C', OPTION_TYPE_CALLBACK, offsetof(options_t, palette_custom), "", 0,
     "Custom palette characters (implies --palette=custom)", "DISPLAY", false, NULL, NULL, parse_palette_chars, false,
     false, OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"show-capabilities", '\0', OPTION_TYPE_BOOL, offsetof(options_t, show_capabilities),
     &default_show_capabilities_value, sizeof(bool), "Show terminal capabilities and exit", "DISPLAY", false, NULL,
     NULL, NULL, false, false, OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"utf8", '\0', OPTION_TYPE_BOOL, offsetof(options_t, force_utf8), &default_force_utf8_value, sizeof(bool),
     "Force UTF-8 support", "DISPLAY", false, NULL, NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"stretch", 's', OPTION_TYPE_BOOL, offsetof(options_t, stretch), &default_stretch_value, sizeof(bool),
     "Allow aspect ratio distortion", "DISPLAY", false, NULL, NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"strip-ansi", '\0', OPTION_TYPE_BOOL, offsetof(options_t, strip_ansi), &default_strip_ansi_value, sizeof(bool),
     "Strip ANSI escape sequences", "DISPLAY", false, NULL, NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"fps", '\0', OPTION_TYPE_INT, offsetof(options_t, fps), 0, sizeof(int), "Target framerate (1-144, 0=use default)",
     "DISPLAY", false, "ASCII_CHAT_FPS", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},

    // SNAPSHOT GROUP (client, mirror, discovery)
    {"snapshot", 'S', OPTION_TYPE_BOOL, offsetof(options_t, snapshot_mode), &default_snapshot_mode_value, sizeof(bool),
     "Snapshot mode (one frame and exit)", "SNAPSHOT", false, "ASCII_CHAT_SNAPSHOT", NULL, NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"snapshot-delay", 'D', OPTION_TYPE_DOUBLE, offsetof(options_t, snapshot_delay), &default_snapshot_delay_value,
     sizeof(double), "Snapshot delay in seconds", "SNAPSHOT", false, "ASCII_CHAT_SNAPSHOT_DELAY", NULL, NULL, false,
     false, OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},

    // PERFORMANCE GROUP (client, server, discovery)
    {"compression-level", '\0', OPTION_TYPE_INT, offsetof(options_t, compression_level),
     &default_compression_level_value, sizeof(int), "zstd compression level (1-9)", "PERFORMANCE", false,
     "ASCII_CHAT_COMPRESSION_LEVEL", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY},
    {"no-compress", '\0', OPTION_TYPE_BOOL, offsetof(options_t, no_compress), &default_no_compress_value, sizeof(bool),
     "Disable compression", "PERFORMANCE", false, "ASCII_CHAT_NO_COMPRESS", NULL, NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY},

    // SECURITY GROUP (client, server, discovery)
    {"encrypt", 'E', OPTION_TYPE_BOOL, offsetof(options_t, encrypt_enabled), &default_encrypt_enabled_value,
     sizeof(bool), "Enable encryption", "SECURITY", false, NULL, NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY | OPTION_MODE_DISCOVERY_SVC},
    {"key", 'K', OPTION_TYPE_STRING, offsetof(options_t, encrypt_key), "", 0, "SSH/GPG key file path", "SECURITY",
     false, "ASCII_CHAT_KEY", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY | OPTION_MODE_DISCOVERY_SVC},
    {"password", '\0', OPTION_TYPE_STRING, offsetof(options_t, password), "", 0, "Shared password for authentication",
     "SECURITY", false, "ASCII_CHAT_PASSWORD", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY | OPTION_MODE_DISCOVERY_SVC},
    {"no-encrypt", '\0', OPTION_TYPE_BOOL, offsetof(options_t, no_encrypt), &default_no_encrypt_value, sizeof(bool),
     "Disable encryption", "SECURITY", false, NULL, NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY | OPTION_MODE_DISCOVERY_SVC},
    {"server-key", '\0', OPTION_TYPE_STRING, offsetof(options_t, server_key), "", 0,
     "Expected server public key (client)", "SECURITY", false, NULL, NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"client-keys", '\0', OPTION_TYPE_STRING, offsetof(options_t, client_keys), "", 0, "Allowed client keys (server)",
     "SECURITY", false, NULL, NULL, NULL, false, false,
     OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY | OPTION_MODE_DISCOVERY_SVC},
    {"discovery-insecure", '\0', OPTION_TYPE_BOOL, offsetof(options_t, discovery_insecure),
     &default_discovery_insecure_value, sizeof(bool),
     "Skip server key verification (MITM-vulnerable, requires explicit opt-in)", "SECURITY", false, NULL, NULL, NULL,
     false, false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"discovery-server-key", '\0', OPTION_TYPE_STRING, offsetof(options_t, discovery_service_key), "", 0,
     "Discovery server public key for verification (SSH/GPG key or HTTPS URL)", "SECURITY", false, NULL, NULL, NULL,
     false, false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},

    // NETWORK GROUP (general network options, various modes)
    {"port", 'p', OPTION_TYPE_STRING, offsetof(options_t, port), OPT_PORT_DEFAULT, 0, "Server port", "NETWORK", false,
     "ASCII_CHAT_PORT", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC | OPTION_MODE_DISCOVERY},
    {"max-clients", '\0', OPTION_TYPE_INT, offsetof(options_t, max_clients), &default_max_clients_value, sizeof(int),
     "Maximum concurrent clients (server only)", "NETWORK", false, NULL, NULL, NULL, false, false,
     OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC}, // Server and Discovery Service
    {"reconnect-attempts", '\0', OPTION_TYPE_INT, offsetof(options_t, reconnect_attempts),
     &default_reconnect_attempts_value, sizeof(int), "Number of reconnection attempts (-1=infinite, 0=none)", "NETWORK",
     false, NULL, NULL, NULL, false, false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY}, // Client and Discovery

    // WebRTC options
    {"webrtc", '\0', OPTION_TYPE_BOOL, offsetof(options_t, webrtc), &default_webrtc_value, sizeof(bool),
     "Use WebRTC P2P mode (default: Direct TCP)", "NETWORK", false, NULL, NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY},
    {"upnp", '\0', OPTION_TYPE_BOOL, offsetof(options_t, enable_upnp), &default_enable_upnp_value, sizeof(bool),
     "Enable UPnP/NAT-PMP port mapping for direct TCP", "NETWORK", false, NULL, NULL, NULL, false, false,
     OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC},
    {"scan", '\0', OPTION_TYPE_BOOL, offsetof(options_t, lan_discovery), &default_lan_discovery_value, sizeof(bool),
     "Scan for servers on local network via mDNS", "NETWORK", false, NULL, NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"prefer-webrtc", '\0', OPTION_TYPE_BOOL, offsetof(options_t, prefer_webrtc), &default_prefer_webrtc_value,
     sizeof(bool), "Try WebRTC before Direct TCP (useful when Direct TCP fails)", "NETWORK", false, NULL, NULL, NULL,
     false, false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"no-webrtc", '\0', OPTION_TYPE_BOOL, offsetof(options_t, no_webrtc), &default_no_webrtc_value, sizeof(bool),
     "Disable WebRTC, use Direct TCP only", "NETWORK", false, NULL, NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"webrtc-skip-stun", '\0', OPTION_TYPE_BOOL, offsetof(options_t, webrtc_skip_stun), &default_webrtc_skip_stun_value,
     sizeof(bool), "Skip WebRTC+STUN stage, go straight to TURN relay", "NETWORK", false, NULL, NULL, NULL, false,
     false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"webrtc-disable-turn", '\0', OPTION_TYPE_BOOL, offsetof(options_t, webrtc_disable_turn),
     &default_webrtc_disable_turn_value, sizeof(bool), "Disable WebRTC+TURN relay, use STUN only", "NETWORK", false,
     NULL, NULL, NULL, false, false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},

    {"stun-servers", '\0', OPTION_TYPE_STRING, offsetof(options_t, stun_servers), OPT_STUN_SERVERS_DEFAULT, 0,
     "Comma-separated list of STUN server URLs", "NETWORK", false, NULL, NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC | OPTION_MODE_DISCOVERY},
    {"turn-servers", '\0', OPTION_TYPE_STRING, offsetof(options_t, turn_servers), OPT_TURN_SERVERS_DEFAULT, 0,
     "Comma-separated list of TURN server URLs", "NETWORK", false, NULL, NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC | OPTION_MODE_DISCOVERY},
    {"turn-username", '\0', OPTION_TYPE_STRING, offsetof(options_t, turn_username), OPT_TURN_USERNAME_DEFAULT, 0,
     "Username for TURN authentication", "NETWORK", false, NULL, NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC | OPTION_MODE_DISCOVERY},
    {"turn-credential", '\0', OPTION_TYPE_STRING, offsetof(options_t, turn_credential), OPT_TURN_CREDENTIAL_DEFAULT, 0,
     "Credential/password for TURN authentication", "NETWORK", false, NULL, NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC | OPTION_MODE_DISCOVERY},
    {"turn-secret", '\0', OPTION_TYPE_STRING, offsetof(options_t, turn_secret), "", 0,
     "Shared secret for dynamic TURN credential generation (HMAC-SHA1)", "NETWORK", false, NULL, NULL, NULL, false,
     false, OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC | OPTION_MODE_DISCOVERY},

    // Media File Streaming Options
    {"file", 'f', OPTION_TYPE_STRING, offsetof(options_t, media_file), "", 0,
     "Stream from media file or stdin (use '-' for stdin)", "MEDIA", false, NULL, NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"url", 'u', OPTION_TYPE_STRING, offsetof(options_t, media_url), "", 0,
     "Stream from network URL (HTTP/HTTPS/YouTube/RTSP) - takes priority over --file", "MEDIA", false, NULL, NULL, NULL,
     false, false, OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"loop", 'l', OPTION_TYPE_BOOL, offsetof(options_t, media_loop), &default_media_loop_value, sizeof(bool),
     "Loop media file playback (not supported for network URLs)", "MEDIA", false, NULL, NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"seek", 's', OPTION_TYPE_CALLBACK, offsetof(options_t, media_seek_timestamp), &default_media_seek_value,
     sizeof(double), "Seek to timestamp before playback (format: seconds, MM:SS, or HH:MM:SS.ms)", "MEDIA", false, NULL,
     NULL, parse_timestamp, false, false, OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"cookies-from-browser", '\0', OPTION_TYPE_CALLBACK, offsetof(options_t, cookies_from_browser), NULL, 0,
     "Browser for reading cookies from (chrome, firefox, edge, safari, brave, opera, vivaldi, whale). "
     "Use without argument to default to chrome.",
     "MEDIA", false, NULL, NULL, parse_cookies_from_browser, false, true,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"no-cookies-from-browser", '\0', OPTION_TYPE_BOOL, offsetof(options_t, no_cookies_from_browser), false,
     sizeof(bool), "Explicitly disable reading cookies from browser", "MEDIA", false, NULL, NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},

    // AUDIO GROUP (client, discovery)
    {"audio", 'A', OPTION_TYPE_BOOL, offsetof(options_t, audio_enabled), &default_audio_enabled_value, sizeof(bool),
     "Enable audio streaming", "AUDIO", false, "ASCII_CHAT_AUDIO", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"microphone-index", '\0', OPTION_TYPE_INT, offsetof(options_t, microphone_index), &default_microphone_index_value,
     sizeof(int), "Microphone device index (-1=default)", "AUDIO", false, "ASCII_CHAT_MICROPHONE_INDEX", NULL, NULL,
     false, false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"speakers-index", '\0', OPTION_TYPE_INT, offsetof(options_t, speakers_index), &default_speakers_index_value,
     sizeof(int), "Speakers device index (-1=default)", "AUDIO", false, "ASCII_CHAT_SPEAKERS_INDEX", NULL, NULL, false,
     false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"microphone-sensitivity", '\0', OPTION_TYPE_DOUBLE, offsetof(options_t, microphone_sensitivity),
     &default_microphone_sensitivity_value, sizeof(float), "Microphone volume multiplier (0.0-1.0)", "AUDIO", false,
     "ASCII_CHAT_MICROPHONE_SENSITIVITY", NULL, NULL, false, false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"speakers-volume", '\0', OPTION_TYPE_DOUBLE, offsetof(options_t, speakers_volume), &default_speakers_volume_value,
     sizeof(float), "Speaker volume multiplier (0.0-1.0)", "AUDIO", false, "ASCII_CHAT_SPEAKERS_VOLUME", NULL, NULL,
     false, false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"audio-analysis", '\0', OPTION_TYPE_BOOL, offsetof(options_t, audio_analysis_enabled),
     &default_audio_analysis_value, sizeof(bool), "Enable audio analysis (debug)", "AUDIO", false,
     "ASCII_CHAT_AUDIO_ANALYSIS", NULL, NULL, NULL, false, false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"no-audio-playback", '\0', OPTION_TYPE_BOOL, offsetof(options_t, audio_no_playback),
     &default_no_audio_playback_value, sizeof(bool), "Disable speaker playback (debug)", "AUDIO", false, NULL, NULL,
     NULL, false, false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"encode-audio", '\0', OPTION_TYPE_BOOL, offsetof(options_t, encode_audio), &default_encode_audio_value,
     sizeof(bool), "Enable Opus audio encoding", "AUDIO", false, "ASCII_CHAT_ENCODE_AUDIO", NULL, NULL, NULL, false,
     false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},
    {"no-encode-audio", '\0', OPTION_TYPE_BOOL, offsetof(options_t, encode_audio), &default_no_encode_audio_value,
     sizeof(bool), "Disable Opus audio encoding", "AUDIO", false, "ASCII_CHAT_NO_ENCODE_AUDIO", NULL, NULL, NULL, false,
     false, OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY},

    // ACDS Server Specific Options
    {"database", '\0', OPTION_TYPE_STRING, offsetof(options_t, discovery_database_path), "", 0,
     "Path to SQLite database for discovery service", "DATABASE", false, NULL, NULL, NULL, false, false,
     OPTION_MODE_DISCOVERY_SVC},
    {"discovery-server", '\0', OPTION_TYPE_STRING, offsetof(options_t, discovery_server),
     OPT_ENDPOINT_DISCOVERY_SERVICE, 0, "Discovery service address (for ACDS registration)", "NETWORK", false, NULL,
     NULL, NULL, false, false, OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY},
    {"discovery-port", '\0', OPTION_TYPE_INT, offsetof(options_t, discovery_port), &default_discovery_port_value,
     sizeof(int), "Discovery service port", "NETWORK", false, NULL, NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY},
    {"discovery-expose-ip", '\0', OPTION_TYPE_BOOL, offsetof(options_t, discovery_expose_ip),
     &default_discovery_expose_ip_value, sizeof(bool),
     "Allow public IP disclosure in discovery sessions (requires confirmation)", "NETWORK", false, NULL, NULL, NULL,
     false, false, OPTION_MODE_CLIENT | OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY},
    {"require-server-identity", '\0', OPTION_TYPE_BOOL, offsetof(options_t, require_server_identity), false,
     sizeof(bool), "ACDS: require servers to provide signed Ed25519 identity", "SECURITY", false, NULL, NULL, NULL,
     false, false, OPTION_MODE_DISCOVERY_SVC},
    {"require-client-identity", '\0', OPTION_TYPE_BOOL, offsetof(options_t, require_client_identity), false,
     sizeof(bool), "ACDS: require clients to provide signed Ed25519 identity", "SECURITY", false, NULL, NULL, NULL,
     false, false, OPTION_MODE_DISCOVERY_SVC},

    // Generic placeholder to mark end of array
    {NULL, '\0', OPTION_TYPE_BOOL, 0, NULL, 0, NULL, NULL, false, NULL, NULL, NULL, false, false, OPTION_MODE_NONE}};

static size_t g_registry_size = 0;

/**
 * @brief Initialize registry size
 */
static void registry_init_size(void) {
  if (g_registry_size == 0) {
    for (size_t i = 0; g_options_registry[i].long_name != NULL; i++) {
      g_registry_size++;
    }
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
      SET_ERRNO(ERROR_INVALID_STATE, "Actions are not supported in this function");
      // Actions are added separately, not in registry
      break;
    }

    // Set mode bitmask on the last added descriptor
    options_builder_set_mode_bitmask(builder, entry->mode_bitmask);
  }

  return ASCIICHAT_OK;
}

const option_descriptor_t *options_registry_find_by_name(const char *long_name) {
  if (!long_name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Long name is NULL");
    return NULL;
  }

  registry_init_size();

  // This would need to search through a built config, not the registry directly
  // For now, return NULL - will be implemented when we have access to built configs
  (void)long_name;
  return NULL;
}

const option_descriptor_t *options_registry_find_by_short(char short_name) {
  if (short_name == '\0') {
    SET_ERRNO(ERROR_INVALID_PARAM, "Short name is empty");
    return NULL;
  }

  registry_init_size();

  // This would need to search through a built config, not the registry directly
  // For now, return NULL - will be implemented when we have access to built configs
  (void)short_name;
  return NULL;
}

const option_descriptor_t *options_registry_get_for_mode(asciichat_mode_t mode, size_t *num_options) {
  if (!num_options) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Number of options is NULL");
    return NULL;
  }

  registry_init_size();

  // This would filter registry by mode bitmask and return array
  // For now, return NULL - will be implemented when we have access to built configs
  (void)mode;
  *num_options = 0;
  return NULL;
}
