/**
 * @defgroup options Options Module
 * @ingroup module_core
 * @brief ⚙️ Command-line option parsing with unified mode detection, builder API, and RCU-based thread-safe access
 *
 * @file options.h
 * @brief ⚙️ Unified options parsing system for ascii-chat with builder pattern and lock-free access
 * @ingroup options
 * @addtogroup options
 * @{
 *
 * This module provides comprehensive command-line argument parsing, configuration management,
 * and unified options state for ascii-chat with support for multiple modes (server, client,
 * mirror, discovery service). The system unifies several layers:
 *
 * - **Builder API** (`builder.h`): Flexible option configuration with mode bitmasks
 * - **Registry** (`registry.h`): Single definition of all options with mode applicability
 * - **RCU Thread-Safety** (`rcu.h`): Lock-free read access to options via `GET_OPTION()` macro
 * - **Unified State** (`options_t` struct): Single source of truth for all option values
 *
 * **Architecture Overview**:
 *
 * The options system is built in layers from bottom to top:
 *
 * 1. **Option Descriptors** (registry.c): Single-source-of-truth definitions of all options
 *    with metadata (long name, short name, mode bitmask, defaults, validators, etc.)
 *
 * 2. **Builder Pattern** (builder.h): Flexible API for programmatically constructing option
 *    configurations. Supports mode-specific options, dependencies, and custom validators.
 *
 * 3. **Presets** (presets.h): Pre-built configurations for common modes (unified, server, client,
 *    mirror, discovery service). Use `options_preset_unified()` for the standard multi-mode setup.
 *
 * 4. **Unified Parsing** (options.c): Single entry point `options_init()` that:
 *    - Detects mode from command-line arguments
 *    - Parses binary-level options (--help, --version, --log-file)
 *    - Parses mode-specific options
 *    - Validates and applies defaults
 *    - Publishes options via RCU for lock-free access
 *
 * 5. **RCU Thread-Safety** (rcu.h): Lock-free read access to options after initialization:
 *    - Use `GET_OPTION(field)` macro to safely read options from any thread
 *    - Use `options_get()` to get pointer to full options_t struct
 *    - Use `options_set_*()` functions for thread-safe updates
 *
 * **Design Philosophy**:
 *
 * - **Single Source of Truth**: All options defined once in registry.c with mode bitmasks
 * - **Builder Pattern Flexibility**: Clients can create custom option configs using builder API
 * - **Unified State**: Single `options_t` struct replaces scattered global variables
 * - **Lock-Free Reads**: RCU (Read-Copy-Update) allows lock-free option access after startup
 * - **Mode Awareness**: Options include mode bitmask so they apply to correct modes automatically
 * - **Sensible Defaults**: All options have OPT_*_DEFAULT values that work out-of-the-box
 * - **Comprehensive Validation**: Options validated at parse time, cross-field validators supported
 * - **Environment Variable Fallbacks**: Options can fall back to environment variables
 *
 * **Usage Pattern**:
 *
 * @code{.c}
 * #include "options.h"
 *
 * int main(int argc, char **argv) {
 *     // Initialize options (detects mode, parses args, validates)
 *     asciichat_error_t err = options_init(argc, argv);
 *     if (err != ASCIICHAT_OK) {
 *         // Could be ERROR_USAGE (invalid options) or others
 *         return 1;
 *     }
 *
 *     // Lock-free read access to options (works from any thread)
 *     const char *addr = GET_OPTION(address);
 *     int port = GET_OPTION(port);
 *     bool audio = GET_OPTION(audio_enabled);
 *     asciichat_mode_t mode = GET_OPTION(detected_mode);
 *
 *     // Or get full options_t pointer for multi-field access
 *     const options_t *opts = options_get();
 *     printf("Mode: %d, Dimensions: %dx%d\n",
 *            opts->detected_mode, opts->width, opts->height);
 *
 *     // Thread-safe updates (if needed at runtime)
 *     options_set_int("width", 120);
 *     options_set_bool("audio_enabled", true);
 *
 *     return 0;
 * }
 * @endcode
 *
 * **Option Lifecycle**:
 *
 * 1. **Definition**: Options defined in registry.c with metadata and mode bitmasks
 * 2. **Builder Creation**: Use `options_preset_unified()` to create builder with presets
 * 3. **Initialization**: Call `options_init(argc, argv)` once at program startup
 *    - Mode detection from argv
 *    - Binary-level option parsing
 *    - Mode-specific parsing
 *    - Validation and defaults
 *    - RCU publishing
 * 4. **Access**: Lock-free reads via `GET_OPTION()` from any thread
 * 5. **Updates** (optional): Thread-safe runtime updates via `options_set_*()`
 *
 * **Mode-Specific Behavior**:
 *
 * Options include a `mode_bitmask` that indicates which modes they apply to:
 *
 * - `OPTION_MODE_BINARY`: Parsed before mode detection (--help, --version, --log-file, etc.)
 * - `OPTION_MODE_SERVER`: Server-only options (--max-clients, --discovery, etc.)
 * - `OPTION_MODE_CLIENT`: Client-only options (--color, --audio, --snapshot, etc.)
 * - `OPTION_MODE_MIRROR`: Mirror mode options (local webcam preview)
 * - `OPTION_MODE_DISCOVERY_SVC`: Discovery service (ACDS) options
 * - `OPTION_MODE_ALL`: Options that apply to all modes
 *
 * **Thread Safety**:
 *
 * - **Startup Phase**: Options parsed once in main thread before worker threads created
 * - **Runtime Phase**: Options are read-only from worker threads via `GET_OPTION()`
 * - **Lock-Free Reads**: Guaranteed safe from any thread using RCU (no locks needed)
 * - **Runtime Updates**: Use `options_set_*()` for thread-safe field updates
 * - **Performance**: Single atomic pointer load (~1-2ns per read)
 *
 * **Builder API for Custom Configurations**:
 *
 * If you need a custom option set (not the standard unified preset), use the builder API:
 *
 * @code{.c}
 * // Create empty builder
 * options_builder_t *builder = options_builder_create("myapp");
 *
 * // Add options from registry (filtered by mode)
 * options_registry_add_all_to_builder(builder);
 *
 * // Or add custom options
 * option_descriptor_t my_opt = {
 *     .long_name = "my-option",
 *     .short_name = 'm',
 *     .type = OPTION_TYPE_STRING,
 *     .offset = offsetof(options_t, my_field),
 *     .mode_bitmask = OPTION_MODE_CLIENT,
 *     // ... other fields ...
 * };
 * options_builder_add_descriptor(builder, &my_opt);
 *
 * // Build immutable config
 * options_config_t *config = options_builder_build(builder);
 *
 * // Parse command line with custom config
 * options_t my_opts = options_t_new();
 * options_config_parse_args(config, argc, argv, &my_opts);
 *
 * // Publish to RCU for lock-free access
 * options_state_init();
 * options_state_set(&my_opts);
 * @endcode
 *
 * **Command-Line Syntax**:
 *
 * Supported formats:
 * - `ascii-chat --help` - Show usage (binary-level)
 * - `ascii-chat --version` - Show version (binary-level)
 * - `ascii-chat server [options]` - Server mode
 * - `ascii-chat client [address][:port] [options]` - Client mode
 * - `ascii-chat mirror [options]` - Mirror mode (local preview)
 * - `ascii-chat acds [options]` - Discovery service mode
 * - `ascii-chat word-word-word [options]` - Client mode (session string)
 *
 * **Option Categories**:
 *
 * Organized by functionality for help display:
 * - **Network Options**: address, port, max_clients, webrtc
 * - **Discovery Service Options**: discovery, discovery_server, discovery_port
 * - **Media Options**: webcam, test pattern, audio, compression
 * - **Display Options**: width, height, color mode, palette, render mode
 * - **Encryption Options**: keys, passwords, identity verification
 * - **Debug Options**: logging, quiet, verbose, snapshots
 *
 * **Default Values**:
 *
 * All options have sensible OPT_*_DEFAULT values:
 * - Terminal dimensions: Auto-detect or 110x70 fallback
 * - Network: localhost:27224
 * - Webcam: Device 0, horizontally flipped
 * - Color mode: Auto-detect terminal capabilities
 * - Encryption: Enabled (can be disabled with --no-encrypt)
 *
 * @note All options are stored in the `options_t` struct, replacing scattered globals
 * @note Options are parsed once at startup via `options_init()`
 * @note Most options remain constant after parsing (read-only)
 * @note Dynamic updates possible via `options_set_*()` for specific fields
 * @note Access via `GET_OPTION()` macro is lock-free and thread-safe
 *
 * @see options_init() - Main entry point for parsing
 * @see options_get() - Get pointer to current options struct
 * @see GET_OPTION() - Convenience macro for reading single fields
 * @see builder.h - Builder API for custom configurations
 * @see registry.h - Central registry of all options
 * @see rcu.h - RCU-based thread-safe access
 * @see presets.h - Pre-built configurations
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include <stdbool.h>
#include <stdio.h>
#include "../platform/terminal.h"
#include "../video/palette.h"
#include "../discovery/strings.h"

/** @brief Backward compatibility aliases for color mode enum values */
#define COLOR_MODE_AUTO TERM_COLOR_AUTO           ///< Auto-detect color support
#define COLOR_MODE_NONE TERM_COLOR_NONE           ///< Monochrome mode
#define COLOR_MODE_16 TERM_COLOR_16               ///< 16-color mode (alias)
#define COLOR_MODE_16_COLOR TERM_COLOR_16         ///< 16-color mode (full name)
#define COLOR_MODE_256 TERM_COLOR_256             ///< 256-color mode (alias)
#define COLOR_MODE_256_COLOR TERM_COLOR_256       ///< 256-color mode (full name)
#define COLOR_MODE_TRUECOLOR TERM_COLOR_TRUECOLOR ///< 24-bit truecolor mode

/**
 * @brief Color output setting (--color flag values)
 *
 * Enumeration for the --color option which controls color output behavior:
 * - COLOR_SETTING_AUTO: Smart detection (default) - colors if TTY, not piping, not CLAUDECODE
 * - COLOR_SETTING_TRUE: Force colors ON - always colorize regardless of TTY/piping/CLAUDECODE
 * - COLOR_SETTING_FALSE: Force colors OFF - disable all colors and logging colors
 *
 * @ingroup options
 */
typedef enum {
  /** @brief Smart detection (default): colors if TTY and not CLAUDECODE and not piping */
  COLOR_SETTING_AUTO = 0,
  /** @brief Force colors ON: always colorize regardless of TTY/piping/CLAUDECODE */
  COLOR_SETTING_TRUE = 1,
  /** @brief Force colors OFF: disable all colors and logging colors */
  COLOR_SETTING_FALSE = -1
} color_setting_t;

/**
 * @brief UTF-8 support setting (--utf8 flag values)
 *
 * Enumeration for the --utf8 option which controls UTF-8 support behavior:
 * - UTF8_SETTING_AUTO: Smart detection (default) - UTF-8 if terminal supports it
 * - UTF8_SETTING_TRUE: Force UTF-8 ON - always use UTF-8 regardless of terminal capability
 * - UTF8_SETTING_FALSE: Force UTF-8 OFF - disable UTF-8 support
 *
 * @ingroup options
 */
typedef enum {
  /** @brief Smart detection (default): UTF-8 if terminal supports it */
  UTF8_SETTING_AUTO = 0,
  /** @brief Force UTF-8 ON: always use UTF-8 regardless of terminal capability */
  UTF8_SETTING_TRUE = 1,
  /** @brief Force UTF-8 OFF: disable UTF-8 support */
  UTF8_SETTING_FALSE = -1
} utf8_setting_t;

/**
 * @brief Audio source selection for playback and capture
 *
 * Determines which audio sources are active during playback:
 * - AUDIO_SOURCE_AUTO: Smart selection based on context (default)
 *   - With media (--file/--url): media only
 *   - Without media: microphone only
 * - AUDIO_SOURCE_MICROPHONE: Capture from microphone only
 * - AUDIO_SOURCE_MEDIA: Playback media audio only (no microphone)
 * - AUDIO_SOURCE_BOTH: Both microphone and media audio simultaneously
 *
 * @ingroup options
 */
typedef enum {
  /** Smart selection: media only when playing, mic otherwise */
  AUDIO_SOURCE_AUTO = 0,
  /** Microphone input only */
  AUDIO_SOURCE_MIC = 1,
  /** Media audio only (no microphone) */
  AUDIO_SOURCE_MEDIA = 2,
  /** Both microphone and media audio */
  AUDIO_SOURCE_BOTH = 3
} audio_source_t;

/**
 * @name Configuration Constants
 * @{
 */

/** @brief Buffer size for option string values
 *
 * Maximum size for string-based options (e.g., addresses, file paths, passwords).
 * Used for arrays that store option values.
 *
 * @ingroup options
 */
#define OPTIONS_BUFF_SIZE 256

/** @brief Default terminal width in characters
 *
 * Default width used when terminal size cannot be detected or when auto-detection
 * is disabled. This default provides a reasonable size for ASCII art display.
 *
 * @note This is used as a fallback if `--width` is not specified and auto-detection fails.
 *
 * @ingroup options
 */
#define OPT_WIDTH_DEFAULT 110

/** @brief Default terminal height in characters
 *
 * Default height used when terminal size cannot be detected or when auto-detection
 * is disabled. This default provides a reasonable size for ASCII art display.
 *
 * @note This is used as a fallback if `--height` is not specified and auto-detection fails.
 *
 * @ingroup options
 */
#define OPT_HEIGHT_DEFAULT 70

/** @brief Default auto-detect width flag (true = auto-detect from terminal) */
#define OPT_AUTO_WIDTH_DEFAULT true

/** @brief Default auto-detect height flag (true = auto-detect from terminal) */
#define OPT_AUTO_HEIGHT_DEFAULT true

/** @brief Default snapshot delay in seconds
 *
 * Default delay for snapshot mode before exiting. macOS webcams show pure black
 * first then fade up into a real color image over a few seconds, so we use a
 * longer delay on macOS.
 *
 * @ingroup options
 */
#if defined(__APPLE__)
// macOS webcams show pure black first then fade up into a real color image over a few seconds
#define SNAPSHOT_DELAY_DEFAULT 4.0f
#else
#define SNAPSHOT_DELAY_DEFAULT 3.0f
#endif

/** @brief Default TCP port for client/server communication (string) */
#define OPT_PORT_DEFAULT "27224"

/** @brief Default TCP port for client/server communication (integer) */
#define OPT_PORT_INT_DEFAULT 27224

/** @brief Default ACDS discovery service port (integer) */
#define OPT_ACDS_PORT_INT_DEFAULT 27225

/** @brief Default ACDS discovery service port (string) */
#define OPT_ACDS_PORT_DEFAULT "27225"

/** @brief Default WebSocket port for server mode (integer) */
#define OPT_WEBSOCKET_PORT_SERVER_DEFAULT 27226

/** @brief Default WebSocket port for discovery-service mode (integer) */
#define OPT_WEBSOCKET_PORT_ACDS_DEFAULT 27227

/** @brief Default server address for client connections */
#define OPT_ADDRESS_DEFAULT "localhost"

/** @brief Default IPv6 server address */
#define OPT_ADDRESS6_DEFAULT "::1"

/**
 * @name Network Endpoint Defaults
 * @brief Centralized definitions for all network service endpoints
 * @{
 * @ingroup options
 */

/** @brief Discovery service (ACDS) endpoint for session management */
#define OPT_ENDPOINT_DISCOVERY_SERVICE "discovery-service.ascii-chat.com"

/** @brief Primary STUN server (ascii-chat hosted) */
#define OPT_ENDPOINT_STUN_PRIMARY "stun:stun.ascii-chat.com:3478"

/** @brief Fallback STUN server (Google public STUN) */
#define OPT_ENDPOINT_STUN_FALLBACK "stun:stun.l.google.com:19302"

/** @brief Default STUN servers (comma-separated list) */
#define OPT_ENDPOINT_STUN_SERVERS_DEFAULT OPT_ENDPOINT_STUN_PRIMARY "," OPT_ENDPOINT_STUN_FALLBACK

/** @brief Primary TURN server (ascii-chat hosted) */
#define OPT_ENDPOINT_TURN_PRIMARY "turn:turn.ascii-chat.com:3478"

/** @brief Default TURN servers (comma-separated list) */
#define OPT_ENDPOINT_TURN_SERVERS_DEFAULT OPT_ENDPOINT_TURN_PRIMARY

/** @brief STUN server hostname only (without protocol/port) */
#define OPT_STUN_SERVER_HOST_PRIMARY "stun.ascii-chat.com"

/** @brief STUN server port for primary server */
#define OPT_STUN_SERVER_PORT_PRIMARY 3478

/** @brief Fallback STUN server hostname only */
#define OPT_STUN_SERVER_HOST_FALLBACK "stun.l.google.com"

/** @brief Fallback STUN server port */
#define OPT_STUN_SERVER_PORT_FALLBACK 19302

/** @brief TURN server hostname only */
#define OPT_TURN_SERVER_HOST "turn.ascii-chat.com"

/** @brief TURN server port */
#define OPT_TURN_SERVER_PORT 3478

/** @} */

/** @brief Default maximum concurrent clients (server only) */
#define OPT_MAX_CLIENTS_DEFAULT 9

/** @brief Default compression level (1-9) */
#define OPT_COMPRESSION_LEVEL_DEFAULT 3

/** @brief Default FPS (frames per second) */
#define OPT_FPS_DEFAULT 60

/** @brief Default webcam device index */
#define OPT_WEBCAM_INDEX_DEFAULT 0

/** @brief Default microphone device index (-1 means system default) */
#define OPT_MICROPHONE_INDEX_DEFAULT (-1)

/** @brief Maximum number of identity keys that can be loaded (for multi-key support) */
#define MAX_IDENTITY_KEYS 32

/** @brief Default speakers device index (-1 means system default) */
#define OPT_SPEAKERS_INDEX_DEFAULT (-1)

/** @brief Default reconnect attempts (-1 means auto/infinite) */
#define OPT_RECONNECT_ATTEMPTS_DEFAULT (-1)

/** @brief Default horizontal flip state (true = horizontally flipped)
 * macOS webcams default to flipped (mirrored), other platforms default to normal */
#ifdef __APPLE__
#define OPT_FLIP_X_DEFAULT true
#else
#define OPT_FLIP_X_DEFAULT false
#endif

/** @brief Default vertical flip state (false = no vertical flip) */
#define OPT_FLIP_Y_DEFAULT false

/** @brief Default color setting (COLOR_SETTING_AUTO = smart detection) */
#define OPT_COLOR_DEFAULT COLOR_SETTING_AUTO

/** @brief Default color mode (auto-detect terminal capabilities) */
#define OPT_COLOR_MODE_DEFAULT COLOR_MODE_AUTO

/** @brief Default color filter (none - no filtering) */
#define OPT_COLOR_FILTER_DEFAULT COLOR_FILTER_NONE

/** @brief Default color scheme name (pastel) */
#define OPT_COLOR_SCHEME_NAME_DEFAULT "pastel"

/** @brief Default render mode (foreground characters only) */
#define OPT_RENDER_MODE_DEFAULT RENDER_MODE_FOREGROUND

/** @brief Default palette type (standard ASCII art) */
#define OPT_PALETTE_TYPE_DEFAULT PALETTE_STANDARD

/** @brief Default media seek timestamp (start from beginning) */
#define OPT_MEDIA_SEEK_TIMESTAMP_DEFAULT 0.0

/** @brief Default color mode (auto-detect) */
#define OPT_COLOR_MODE_DEFAULT COLOR_MODE_AUTO

/** @brief Default require-server-identity setting for ACDS */
#define OPT_REQUIRE_SERVER_IDENTITY_DEFAULT false

/** @brief Default require-client-identity setting for ACDS */
#define OPT_REQUIRE_CLIENT_IDENTITY_DEFAULT false

/** @brief Default audio encoding state (true = Opus encoding enabled) */
#define OPT_ENCODE_AUDIO_DEFAULT true

/** @brief Default test pattern mode (false = use actual webcam) */
#define OPT_TEST_PATTERN_DEFAULT false

/** @brief Default show terminal capabilities flag */
#define OPT_SHOW_CAPABILITIES_DEFAULT false

/** @brief Default list webcams flag */
#define OPT_LIST_WEBCAMS_DEFAULT false

/** @brief Default list microphones flag */
#define OPT_LIST_MICROPHONES_DEFAULT false

/** @brief Default list speakers flag */
#define OPT_LIST_SPEAKERS_DEFAULT false

/** @brief Default force UTF-8 support setting (auto-detect) */
#define OPT_FORCE_UTF8_DEFAULT UTF8_SETTING_AUTO

/** @brief Default allow aspect ratio distortion flag */
#define OPT_STRETCH_DEFAULT false

/** @brief Default strip ANSI escape sequences flag */
#define OPT_STRIP_ANSI_DEFAULT false

/** @brief Default snapshot mode flag (false = continuous) */
#define OPT_SNAPSHOT_MODE_DEFAULT false

/** @brief Default Matrix rain effect flag (false = disabled) */
#define OPT_MATRIX_RAIN_DEFAULT false

/** @brief Default no compression flag (false = enable compression) */
#define OPT_NO_COMPRESS_DEFAULT false

/** @brief Default encrypt enabled flag (true = encryption required) */
#define OPT_ENCRYPT_ENABLED_DEFAULT true

/** @brief Default no encrypt flag (false = allow encryption) */
#define OPT_NO_ENCRYPT_DEFAULT false

/** @brief Default no auth flag (false = allow authentication) */
#define OPT_NO_AUTH_DEFAULT false

/** @brief Default WebRTC mode flag (true = P2P WebRTC, false = direct TCP) */
#define OPT_WEBRTC_DEFAULT true

/** @brief Default audio enabled flag (true = audio enabled by default) */
#define OPT_AUDIO_ENABLED_DEFAULT true

/** @brief Default audio source (AUDIO_SOURCE_AUTO = smart selection) */
#define OPT_AUDIO_SOURCE_DEFAULT AUDIO_SOURCE_AUTO

/** @brief Default audio analysis enabled flag */
#define OPT_AUDIO_ANALYSIS_ENABLED_DEFAULT false

/** @brief Default audio playback flag (false = enable playback) */
#define OPT_AUDIO_NO_PLAYBACK_DEFAULT false

/** @brief Default help flag */
#define OPT_HELP_DEFAULT false

/** @brief Default version flag */
#define OPT_VERSION_DEFAULT false

/** @brief Default no audio mixer flag (false = enable mixer) */
#define OPT_NO_AUDIO_MIXER_DEFAULT false

/** @brief Default ACDS expose IP flag (false = private by default) */
#define OPT_ACDS_EXPOSE_IP_DEFAULT false

/** @brief Default ACDS registration flag (false = disabled) */
#define OPT_ACDS_DEFAULT false

/** @brief Default enable UPnP flag (false = UPnP disabled) */
#define OPT_ENABLE_UPNP_DEFAULT false

/** @brief Default no mDNS advertise flag (false = advertise enabled) */
#define OPT_NO_MDNS_ADVERTISE_DEFAULT false

/** @brief Default LAN discovery flag (false = discovery disabled) */
#define OPT_LAN_DISCOVERY_DEFAULT false

/** @brief Default prefer WebRTC flag (false = try direct TCP first) */
#define OPT_PREFER_WEBRTC_DEFAULT false

/** @brief Default no WebRTC flag (false = WebRTC enabled) */
#define OPT_NO_WEBRTC_DEFAULT false

/** @brief Default WebRTC skip STUN flag (false = use STUN) */
#define OPT_WEBRTC_SKIP_STUN_DEFAULT false

/** @brief Default WebRTC disable TURN flag (false = use TURN) */
#define OPT_WEBRTC_DISABLE_TURN_DEFAULT false

/** @brief Default WebRTC skip host candidates flag (false = use host candidates) */
#define OPT_WEBRTC_SKIP_HOST_DEFAULT false

/** @brief Default WebRTC ICE gathering timeout in milliseconds (10 seconds) */
#define OPT_WEBRTC_ICE_TIMEOUT_MS_DEFAULT 10000

/** @brief Default WebRTC reconnection attempts (3 = try initial + 3 retries) */
#define OPT_WEBRTC_RECONNECT_ATTEMPTS_DEFAULT 3

/** @brief Default ACDS insecure mode flag (false = verify server) */
#define OPT_ACDS_INSECURE_DEFAULT false

/** @brief Default microphone sensitivity (1.0 = normal volume) */
#define OPT_MICROPHONE_SENSITIVITY_DEFAULT 1.0

/** @brief Default speakers volume (1.0 = normal volume) */
#define OPT_SPEAKERS_VOLUME_DEFAULT 1.0

/** @brief Default quiet mode flag (false = logging enabled) */
#define OPT_QUIET_DEFAULT false

/** @brief Default loop media flag (false = play once) */
#define OPT_MEDIA_LOOP_DEFAULT false

/** @brief Default pause media flag (false = play immediately) */
#define OPT_PAUSE_DEFAULT false

/** @brief Default STUN server URLs (comma-separated) */
#define OPT_STUN_SERVERS_DEFAULT OPT_ENDPOINT_STUN_SERVERS_DEFAULT

/** @brief Default TURN server URLs (comma-separated) */
#define OPT_TURN_SERVERS_DEFAULT OPT_ENDPOINT_TURN_SERVERS_DEFAULT

/** @brief Default TURN server username
 *
 * @note In production (NDEBUG), empty string - ACDS provides credentials via SESSION_JOINED
 *       In debug builds, provides test credentials for testing without ACDS
 */
#ifdef NDEBUG
#define OPT_TURN_USERNAME_DEFAULT ""
#else
#define OPT_TURN_USERNAME_DEFAULT "ascii"
#endif

/** @brief Default TURN server credential
 *
 * @note In production (NDEBUG), empty string - ACDS provides credentials via SESSION_JOINED
 *       In debug builds, provides test credentials for testing without ACDS
 */
#ifdef NDEBUG
#define OPT_TURN_CREDENTIAL_DEFAULT ""
#else
#define OPT_TURN_CREDENTIAL_DEFAULT "0aa9917b4dad1b01631e87a32b875e09"
#endif

/** @brief Default verbose level (0 = not verbose) */
#define OPT_VERBOSE_LEVEL_DEFAULT 0

/** @brief Default grep pattern (empty = no filtering) */
#define OPT_GREP_PATTERN_DEFAULT ""

/** @brief Default log level (LOG_INFO) */
#define OPT_LOG_LEVEL_DEFAULT LOG_INFO

/** @brief Default media from stdin flag (false = not reading from stdin) */
#define OPT_MEDIA_FROM_STDIN_DEFAULT false

/** @brief Default custom palette set flag (false = not set) */
#define OPT_PALETTE_CUSTOM_SET_DEFAULT false

/** @brief Default require server verify flag (false = not required) */
#define OPT_REQUIRE_SERVER_VERIFY_DEFAULT false

/** @brief Default require client verify flag (false = not required) */
#define OPT_REQUIRE_CLIENT_VERIFY_DEFAULT false

/** @brief Default splash screen flag (true = show splash, false = hide splash) */
#define OPT_SPLASH_DEFAULT true

/** @brief Default status screen flag (true = show status, false = hide status) */
#define OPT_STATUS_SCREEN_DEFAULT true

/** @brief Default log format string - release mode (simple format with timestamp, level, and message) */
#define OPT_LOG_TEMPLATE_DEFAULT_RELEASE                                                                               \
  "[%color(*, %H):%color(*, %M):%color(*, %S).%color(*, %ms)] [%color(*, %level_aligned)] "                            \
  "%colored_message"

/** @brief Default log format string - debug mode (verbose with thread ID, file relative path, line, and function) */
#define OPT_LOG_TEMPLATE_DEFAULT_DEBUG                                                                                 \
  "[%color(*, %H):%color(*, %M):%color(*, %S).%color(*, %ms)] [%color(*, %level_aligned)] "                            \
  "[tid:%color(GREY, %tid)] %color(DEBUG, %file_relative):%color(GREY, %line)@%color(DEV, %func)(): "                  \
  "%colored_message"

/** @brief Default log template string (selected based on build mode)
 *
 * Release builds use simple format. Debug builds use verbose format with thread ID, file path, line, and function.
 * The get_default_log_template() function returns this value via the options system.
 */
#ifdef NDEBUG
#define OPT_LOG_TEMPLATE_DEFAULT OPT_LOG_TEMPLATE_DEFAULT_RELEASE
#else
#define OPT_LOG_TEMPLATE_DEFAULT OPT_LOG_TEMPLATE_DEFAULT_DEBUG
#endif

/** @brief Default log format output type (TEXT = human-readable, JSON = structured) */
#define OPT_LOG_FORMAT_OUTPUT_DEFAULT LOG_OUTPUT_TEXT

/** @brief Default log format console only flag (false = use default format everywhere) */
#define OPT_LOG_FORMAT_CONSOLE_DEFAULT false

// ============================================================================
// Render-to-file Options (macOS and Linux only)
// ============================================================================
#ifndef _WIN32
#define OPT_RENDER_FILE_DEFAULT ""
#define OPT_RENDER_THEME_DEFAULT 0  // 0=dark, 1=light, 2=auto
#define OPT_RENDER_FONT_DEFAULT ""
#define OPT_RENDER_FONT_SIZE_DEFAULT 12.0
#endif

// ============================================================================
// Static Default Value Variables
// ============================================================================
// These are referenced from the registry and provide const void * pointers
// for non-string default values. Defined alongside OPT_*_DEFAULT for clarity.

static const int default_log_level_value = DEFAULT_LOG_LEVEL;
static const bool default_quiet_value = false;
static const bool default_json_value = false;
static const bool default_enable_keepawake_value = false;
static const bool default_disable_keepawake_value = false;
static const int default_width_value = OPT_WIDTH_DEFAULT;
static const int default_height_value = OPT_HEIGHT_DEFAULT;
static const int default_port_value = OPT_PORT_INT_DEFAULT;
static const int default_websocket_port_value = OPT_WEBSOCKET_PORT_SERVER_DEFAULT;
static const int default_webcam_index_value = OPT_WEBCAM_INDEX_DEFAULT;
static const bool default_flip_x_value = OPT_FLIP_X_DEFAULT;
static const bool default_flip_y_value = OPT_FLIP_Y_DEFAULT;
static const bool default_test_pattern_value = OPT_TEST_PATTERN_DEFAULT;
static const int default_color_value = OPT_COLOR_DEFAULT;
static const int default_color_mode_value = OPT_COLOR_MODE_DEFAULT;
static const int default_color_filter_value = OPT_COLOR_FILTER_DEFAULT;
static const int default_render_mode_value = OPT_RENDER_MODE_DEFAULT;
static const int default_palette_type_value = OPT_PALETTE_TYPE_DEFAULT;
static const bool default_show_capabilities_value = OPT_SHOW_CAPABILITIES_DEFAULT;
static const int default_force_utf8_value = OPT_FORCE_UTF8_DEFAULT;
static const bool default_stretch_value = OPT_STRETCH_DEFAULT;
static const bool default_strip_ansi_value = OPT_STRIP_ANSI_DEFAULT;
static const bool default_snapshot_mode_value = OPT_SNAPSHOT_MODE_DEFAULT;
static const double default_snapshot_delay_value = SNAPSHOT_DELAY_DEFAULT;
static const bool default_matrix_rain_value = OPT_MATRIX_RAIN_DEFAULT;
static const int default_fps_value = OPT_FPS_DEFAULT;
static const int default_compression_level_value = OPT_COMPRESSION_LEVEL_DEFAULT;
static const bool default_no_compress_value = OPT_NO_COMPRESS_DEFAULT;
static const bool default_encrypt_enabled_value = OPT_ENCRYPT_ENABLED_DEFAULT;
static const bool default_no_encrypt_value = OPT_NO_ENCRYPT_DEFAULT;
static const bool default_no_auth_value = OPT_NO_AUTH_DEFAULT;
static const int default_max_clients_value = OPT_MAX_CLIENTS_DEFAULT;
static const int default_reconnect_attempts_value = OPT_RECONNECT_ATTEMPTS_DEFAULT;
static const int default_discovery_port_value = OPT_ACDS_PORT_INT_DEFAULT;
static const bool default_discovery_value = OPT_ACDS_DEFAULT;
static const bool default_webrtc_value = OPT_WEBRTC_DEFAULT;
static const bool default_enable_upnp_value = OPT_ENABLE_UPNP_DEFAULT;
static const bool default_lan_discovery_value = OPT_LAN_DISCOVERY_DEFAULT;
static const bool default_prefer_webrtc_value = OPT_PREFER_WEBRTC_DEFAULT;
static const bool default_no_webrtc_value = OPT_NO_WEBRTC_DEFAULT;
static const bool default_webrtc_skip_stun_value = OPT_WEBRTC_SKIP_STUN_DEFAULT;
static const bool default_webrtc_disable_turn_value = OPT_WEBRTC_DISABLE_TURN_DEFAULT;
static const bool default_webrtc_skip_host_value = OPT_WEBRTC_SKIP_HOST_DEFAULT;

static const int default_webrtc_ice_timeout_ms_value = OPT_WEBRTC_ICE_TIMEOUT_MS_DEFAULT;

static const int default_webrtc_reconnect_attempts_value = OPT_WEBRTC_RECONNECT_ATTEMPTS_DEFAULT;
static const bool default_media_loop_value = OPT_MEDIA_LOOP_DEFAULT;
static const bool default_pause_value = OPT_PAUSE_DEFAULT;
static const double default_media_seek_value = OPT_MEDIA_SEEK_TIMESTAMP_DEFAULT;
static const bool default_audio_enabled_value = OPT_AUDIO_ENABLED_DEFAULT;
static const audio_source_t default_audio_source_value = OPT_AUDIO_SOURCE_DEFAULT;
static const int default_microphone_index_value = OPT_MICROPHONE_INDEX_DEFAULT;
static const int default_speakers_index_value = OPT_SPEAKERS_INDEX_DEFAULT;
static const double default_microphone_sensitivity_value = OPT_MICROPHONE_SENSITIVITY_DEFAULT;
static const double default_speakers_volume_value = OPT_SPEAKERS_VOLUME_DEFAULT;
static const bool default_audio_analysis_value = OPT_AUDIO_ANALYSIS_ENABLED_DEFAULT;
static const bool default_no_audio_playback_value = OPT_AUDIO_NO_PLAYBACK_DEFAULT;
static const bool default_encode_audio_value = OPT_ENCODE_AUDIO_DEFAULT;
static const bool default_no_encode_audio_value = !OPT_ENCODE_AUDIO_DEFAULT;
static const bool default_no_audio_mixer_value = OPT_NO_AUDIO_MIXER_DEFAULT;
static const bool default_discovery_expose_ip_value = OPT_ACDS_EXPOSE_IP_DEFAULT;
static const bool default_discovery_insecure_value = OPT_ACDS_INSECURE_DEFAULT;
static const bool default_require_server_identity_value = OPT_REQUIRE_SERVER_IDENTITY_DEFAULT;
static const bool default_require_client_identity_value = OPT_REQUIRE_CLIENT_IDENTITY_DEFAULT;
static const bool default_splash_value = OPT_SPLASH_DEFAULT;
static const bool default_status_screen_value = OPT_STATUS_SCREEN_DEFAULT;
static const bool default_no_check_update_value = false;
static const bool default_log_format_console_only_value = OPT_LOG_FORMAT_CONSOLE_DEFAULT;

#ifndef _WIN32
static const int    default_render_theme_value     = OPT_RENDER_THEME_DEFAULT;
static const double default_render_font_size_value = OPT_RENDER_FONT_SIZE_DEFAULT;
#endif

/** @{ @} */

/**
 * @name Utility Functions
 * @{
 */

/**
 * @brief Safely parse string to integer with validation
 * @param str String to parse (must not be NULL)
 * @return Integer value on success, INT_MIN on error
 *
 * Parses a string to an integer with comprehensive validation:
 * - Validates that string is not NULL or empty
 * - Performs base-10 conversion using strtol()
 * - Checks for partial conversions (characters left unconverted)
 * - Validates result is within int range (INT_MIN to INT_MAX)
 * - Returns INT_MIN on any error condition
 *
 * This function is used internally by the options parser to safely convert
 * command-line argument strings to integer values with proper error handling.
 *
 * @note Returns INT_MIN on error (which is distinguishable from valid negative
 *       values since INT_MIN is a valid integer, but unlikely to be used as an
 *       option value). The options parser checks for INT_MIN to detect parse errors.
 *
 * @note Thread-safe: Uses only local variables, no static state.
 *
 * @par Example:
 * @code{.c}
 * const char *arg = "80";
 * int width = strtoint_safe(arg);
 * if (width == INT_MIN) {
 *     // Parse error
 * } else {
 *     // Valid integer: width == 80
 * }
 * @endcode
 *
 * @ingroup options
 */
int strtoint_safe(const char *str);

/** @} */

/**
 * @brief Mode type for options parsing
 *
 * Determines which set of options to use when parsing command-line arguments.
 */
typedef enum {
  MODE_SERVER,            ///< Server mode - network server options
  MODE_CLIENT,            ///< Client mode - network client options
  MODE_MIRROR,            ///< Mirror mode - local webcam viewing (no network)
  MODE_DISCOVERY_SERVICE, ///< Discovery server mode - session management and WebRTC signaling
  MODE_DISCOVERY,         ///< Discovery mode - participant that can dynamically become host
  MODE_INVALID            ///< Invalid mode
} asciichat_mode_t;

/**
 * @brief Option mode bitmask
 *
 * Indicates which modes an option applies to. Options can apply to multiple
 * modes by combining bitmasks with bitwise OR.
 */
typedef enum {
  OPTION_MODE_NONE = 0,                                      ///< No modes (invalid)
  OPTION_MODE_SERVER = (1 << MODE_SERVER),                   ///< Server mode (bit 0)
  OPTION_MODE_CLIENT = (1 << MODE_CLIENT),                   ///< Client mode (bit 1)
  OPTION_MODE_MIRROR = (1 << MODE_MIRROR),                   ///< Mirror mode (bit 2)
  OPTION_MODE_DISCOVERY_SVC = (1 << MODE_DISCOVERY_SERVICE), ///< Discovery server mode (bit 3)
  OPTION_MODE_DISCOVERY = (1 << MODE_DISCOVERY),             ///< Discovery mode (bit 4)
  OPTION_MODE_BINARY = 0x100,                                ///< Binary-level options (parsed before mode detection)
  OPTION_MODE_ALL = 0x1F | 0x100                             ///< All modes + binary
} option_mode_bitmask_t;

/**
 * @brief Mode group macros for examples
 *
 * These macros combine multiple mode bits to allow examples to apply to
 * logical groups of modes (e.g., server-like modes, client-like modes).
 */
#define OPTION_MODE_SERVER_LIKE (OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC)
#define OPTION_MODE_CLIENT_LIKE (OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY)
#define OPTION_MODE_NETWORKED                                                                                          \
  (OPTION_MODE_SERVER | OPTION_MODE_CLIENT | OPTION_MODE_DISCOVERY_SVC | OPTION_MODE_DISCOVERY)

/**
 * @brief Consolidated options structure
 *
 * All options from the scattered extern globals are now in a single struct.
 * This struct is immutable once published via RCU - modifications create a new copy.
 */
typedef struct options_state {
  // ============================================================================
  // Mode Detection (auto-detected during options_init)
  // ============================================================================
  asciichat_mode_t detected_mode; ///< Mode detected from command-line arguments

  // ============================================================================
  // Binary-Level Options (parsed first, before mode selection)
  // ============================================================================
  bool help;                           ///< Show help message
  bool version;                        ///< Show version information
  char config_file[OPTIONS_BUFF_SIZE]; ///< Config file path (--config)

  // ============================================================================
  // Terminal Dimensions
  // ============================================================================
  int width;        ///< Terminal width in characters (int for OPTION_TYPE_INT compat)
  int height;       ///< Terminal height in characters (int for OPTION_TYPE_INT compat)
  bool auto_width;  ///< Auto-detect width from terminal
  bool auto_height; ///< Auto-detect height from terminal

  // ============================================================================
  // Network Options
  // ============================================================================
  char address[OPTIONS_BUFF_SIZE];                 ///< Server address (client) or bind address (server)
  char address6[OPTIONS_BUFF_SIZE];                ///< IPv6 bind address (server only)
  int port;                                        ///< Server port number
  int websocket_port;                              ///< WebSocket server port (server/discovery-service only)
  int max_clients;                                 ///< Maximum concurrent clients (server only)
  char session_string[SESSION_STRING_BUFFER_SIZE]; ///< Session string for ACDS discovery (calculated max size: 38 chars
                                                   ///< + null)

  // ============================================================================
  // Discovery Service Options (server only)
  // ============================================================================
  bool discovery;                                ///< Enable discovery session registration (default: false)
  char discovery_server[OPTIONS_BUFF_SIZE];      ///< discovery server address (default: 127.0.0.1)
  int discovery_port;                            ///< discovery server port (default: 27225)
  char discovery_service_key[OPTIONS_BUFF_SIZE]; ///< discovery server public key for trust verification (SSH/GPG key or
                                                 ///< HTTPS URL)
  bool webrtc; ///< Enable WebRTC mode for discovery session (default: true, P2P WebRTC)
  char discovery_database_path[OPTIONS_BUFF_SIZE]; ///< discovery database file path (default:
                                                   ///< ~/.ascii-chat/discovery.db)

  // ============================================================================
  // LAN Discovery Options
  // ============================================================================
  bool lan_discovery;     ///< Enable LAN service discovery via mDNS (client only)
  bool no_mdns_advertise; ///< Disable mDNS service advertisement (server only)

  // ============================================================================
  // Network Performance Options
  // ============================================================================
  int compression_level; ///< zstd compression level (1-9)
  bool no_compress;      ///< Disable compression entirely
  bool encode_audio;     ///< Enable Opus audio encoding

  // ============================================================================
  // Client Reconnection Options
  // ============================================================================
  int reconnect_attempts; ///< Number of reconnection attempts (-1=infinite, 0=none)

  // ============================================================================
  // Webcam Options
  // ============================================================================
  int webcam_index;    ///< Webcam device index (0 = first)
  bool test_pattern;   ///< Use test pattern instead of webcam
  bool no_audio_mixer; ///< Disable audio mixer (debug)

  // ============================================================================
  // Media File Streaming Options
  // ============================================================================
  char media_file[OPTIONS_BUFF_SIZE]; ///< Media file path or "-" for stdin
  char media_url[OPTIONS_BUFF_SIZE];  ///< Network URL (HTTP/HTTPS/YouTube/RTSP) - takes priority over media_file
  bool media_loop;                    ///< Loop media file playback
  bool media_from_stdin;              ///< Reading from stdin (detected from "--file -")
  double media_seek_timestamp;        ///< Seek to timestamp in seconds before playback
  bool pause;                         ///< Start playback paused (toggle with spacebar)
  char yt_dlp_options[512];           ///< Arbitrary yt-dlp options (e.g., "--cookies-from-browser=chrome --embed-subs")

  // ============================================================================
  // Display Options
  // ============================================================================
  int color;                        ///< Color setting (COLOR_SETTING_AUTO/TRUE/FALSE)
  terminal_color_mode_t color_mode; ///< Color mode (auto/none/16/256/truecolor)
  color_filter_t color_filter;      ///< Monochromatic color filter (none/black/white/green/etc)
  char color_scheme_name[64];       ///< Color scheme name (e.g., "pastel", "nord")
  render_mode_t render_mode;        ///< Render mode (foreground/background/half-block)
  bool show_capabilities;           ///< Show terminal capabilities and exit
  int force_utf8;                   ///< UTF-8 support setting (auto/true/false)
  int fps;                          ///< Target framerate (1-144, default: 60)
  bool flip_x;                      ///< Flip video horizontally (X-axis). Ignored for webcam on macOS
  bool flip_y;                      ///< Flip video vertically (Y-axis)

  // ============================================================================
  // Audio Configuration
  // ============================================================================
  bool audio_enabled;           ///< Enable audio streaming
  audio_source_t audio_source;  ///< Audio source selection (auto/mic/media/both)
  int microphone_index;         ///< Microphone device index (-1 = default)
  int speakers_index;           ///< Speakers device index (-1 = default)
  float microphone_sensitivity; ///< Microphone volume multiplier (0.0-1.0, default 1.0)
  float speakers_volume;        ///< Speaker volume multiplier (0.0-1.0, default 1.0)
  bool audio_analysis_enabled;  ///< Enable audio analysis (debug)
  bool audio_no_playback;       ///< Disable speaker playback (debug)

  // ============================================================================
  // Image Options
  // ============================================================================
  bool stretch; ///< Allow aspect ratio distortion

  // ============================================================================
  // Output Options
  // ============================================================================
  bool quiet;                           ///< Quiet mode (suppress logs)
  unsigned short int verbose_level;     ///< Verbosity level (stackable -V)
  bool snapshot_mode;                   ///< Snapshot mode (one frame and exit)
  double snapshot_delay;                ///< Snapshot delay in seconds
  bool matrix_rain;                     ///< Matrix digital rain effect (false = disabled)
  bool strip_ansi;                      ///< Strip ANSI escape sequences
  char log_file[OPTIONS_BUFF_SIZE];     ///< Log file path
  log_level_t log_level;                ///< Log level threshold
  char grep_pattern[OPTIONS_BUFF_SIZE]; ///< PCRE2 regex for log filtering
  bool json;                            ///< Enable JSON logging (--json flag)
  char log_template[OPTIONS_BUFF_SIZE]; ///< Custom log format string (formerly --log-format)
  bool log_format_console_only;         ///< Apply log format only to console output
  bool enable_keepawake;                ///< Explicitly enable system sleep prevention
  bool disable_keepawake;               ///< Explicitly disable system sleep prevention (allow OS to sleep)

  // ============================================================================
  // Encryption Options
  // ============================================================================
  bool encrypt_enabled;                    ///< Enable encryption
  char encrypt_key[OPTIONS_BUFF_SIZE];     ///< SSH/GPG key file path (first --key flag, kept for compatibility)
  char password[OPTIONS_BUFF_SIZE];        ///< Password string
  char encrypt_keyfile[OPTIONS_BUFF_SIZE]; ///< Alternative key file path
  bool no_encrypt;                         ///< Disable encryption (opt-out)
  bool no_auth;                            ///< Disable authentication layer (--no-auth)
  char server_key[OPTIONS_BUFF_SIZE];      ///< Expected server public key (client)
  char client_keys[OPTIONS_BUFF_SIZE];     ///< Allowed client keys (server)

  // Multi-key support: When multiple --key flags are provided, all keys are stored here
  // encrypt_key is duplicated in identity_keys[0] for backward compatibility
  char identity_keys[MAX_IDENTITY_KEYS]
                    [OPTIONS_BUFF_SIZE]; ///< All identity keys (populated when --key is used multiple times)
  size_t num_identity_keys;              ///< Number of identity keys loaded (0 means single-key mode using encrypt_key)

  // ============================================================================
  // Identity Verification Options (ACDS + Crypto Handshake)
  // ============================================================================
  bool require_server_identity; ///< ACDS: require servers to provide signed Ed25519 identity
  bool require_client_identity; ///< ACDS: require clients to provide signed Ed25519 identity
  bool require_server_verify;   ///< Server: only accept clients who verified via ACDS
  bool require_client_verify;   ///< Client: only connect to servers whose identity was verified by ACDS
  bool discovery_expose_ip;     ///< ACDS: explicitly allow public IP disclosure without verification (opt-in)
  bool discovery_insecure;      ///< ACDS: skip server key verification (MITM-vulnerable, requires explicit opt-in)

  // ============================================================================
  // WebRTC Connection Strategy Options (client-side fallback control)
  // ============================================================================
  bool prefer_webrtc;            ///< --prefer-webrtc: Try WebRTC before Direct TCP
  bool no_webrtc;                ///< --no-webrtc: Disable WebRTC, use Direct TCP only
  bool webrtc_skip_stun;         ///< --webrtc-skip-stun: Skip Stage 2 (STUN), go to TURN
  bool webrtc_disable_turn;      ///< --webrtc-disable-turn: Disable Stage 3 (TURN), use STUN only
  bool webrtc_skip_host;         ///< --webrtc-skip-host: Skip host candidates, force STUN/TURN only
  int webrtc_ice_timeout_ms;     ///< --webrtc-ice-timeout: ICE gathering timeout in milliseconds (default: 10000)
  int webrtc_reconnect_attempts; ///< --webrtc-reconnect-attempts: Number of retry attempts (default: 3)

  // ============================================================================
  // WebRTC Connectivity Options (ACDS mode only)
  // ============================================================================
  bool enable_upnp;                        ///< Enable UPnP/NAT-PMP port mapping for direct TCP (opt-in via --upnp)
  char stun_servers[OPTIONS_BUFF_SIZE];    ///< ACDS: Comma-separated list of STUN server URLs
  char turn_servers[OPTIONS_BUFF_SIZE];    ///< ACDS: Comma-separated list of TURN server URLs
  char turn_username[OPTIONS_BUFF_SIZE];   ///< ACDS: Username for TURN authentication
  char turn_credential[OPTIONS_BUFF_SIZE]; ///< ACDS: Credential/password for TURN authentication
  char turn_secret[OPTIONS_BUFF_SIZE];     ///< ACDS: Shared secret for dynamic TURN credential generation (HMAC-SHA1)

  // ============================================================================
  // Palette Configuration
  // ============================================================================
  palette_type_t palette_type; ///< Selected palette type
  char palette_custom[256];    ///< Custom palette characters
  bool palette_custom_set;     ///< True if custom palette was set

  // ============================================================================
  // Splash Screen Options
  // ============================================================================
  bool splash_screen;                ///< Show splash screen (default: true = show, use --no-splash-screen to hide)
  bool splash_screen_explicitly_set; ///< True if splash screen was explicitly set by user
  bool status_screen;                ///< Show status screen (default: true = show, use --no-status-screen to hide)
  bool status_screen_explicitly_set; ///< True if status_screen was explicitly set by user
  bool no_check_update;              ///< Disable automatic update checks (default: false = checks enabled)

  // ============================================================================
  // Render-to-file Options (macOS and Linux only)
  // ============================================================================
#ifndef _WIN32
  char render_file[OPTIONS_BUFF_SIZE]; ///< Output file path (e.g. output.mp4)
  int  render_theme;                   ///< 0=dark 1=light 2=auto
  char render_font[OPTIONS_BUFF_SIZE]; ///< Font family name or .ttf path (empty = platform default)
  double render_font_size;             ///< Font size in points (default 12.0, supports e.g. 10.5)
#endif

  // Note: Luminance weights (weight_red, weight_green, weight_blue) and
  // lookup tables (RED[], GREEN[], BLUE[], GRAY[]) are kept as globals
  // since they're precomputed constants, not user-configurable options.
} options_t;

// ============================================================================
// RCU-based thread-safe options access
// ============================================================================

/**
 * @brief Get current options (lock-free read)
 *
 * Returns a pointer to the current options struct. This pointer is guaranteed
 * to remain valid for the lifetime of your function (no one will free it under you).
 *
 * **Performance**: Single atomic pointer load (~1-2ns on modern CPUs)
 *
 * @return Pointer to current options (never NULL after options_init())
 */
const options_t *options_get(void);

/**
 * @brief Safely get a specific option field (lock-free read)
 *
 * Convenience macro for accessing individual option fields without storing
 * the entire options pointer. Includes NULL check with warning log for safety.
 *
 * **Usage Examples**:
 * @code{.c}
 * // Simple field access
 * const char *addr = GET_OPTION(address6);
 * int width = GET_OPTION(width);
 * bool flip_x = GET_OPTION(flip_x);
 *
 * // In expressions
 * if (GET_OPTION(encrypt_enabled)) {
 *     // encryption is enabled
 * }
 *
 * // Function arguments
 * connect_to_server(GET_OPTION(address), GET_OPTION(port));
 * @endcode
 *
 * **Design**: This macro eliminates the need to store `const options_t *opts`
 * pointers around the codebase, reducing clutter and making code more readable.
 *
 * **Safety**: If options_get() returns NULL (shouldn't happen after initialization),
 * the macro will log a warning and return a zero-initialized field.
 *
 * **Performance**: Equivalent cost to direct options_get()->field access.
 *
 * @param field The field name to access (e.g., address, port, width, etc.)
 * @return The value of the requested field
 *
 * @note Must be called after options_init() has completed
 * @note Zero-initialized fields are returned if options pointer is somehow NULL
 *
 * @ingroup options
 */
#define GET_OPTION(field)                                                                                              \
  ({                                                                                                                   \
    const options_t *_opts = options_get();                                                                            \
    if (!_opts) {                                                                                                      \
      log_warn("GET_OPTION(" #field ") called but options not initialized");                                           \
    }                                                                                                                  \
    static typeof(((options_t *)0)->field) _default = {0};                                                             \
    (_opts ? (_opts->field) : _default);                                                                               \
  })

/**
 * @brief Check if an option was explicitly set via command-line
 *
 * Convenience macro for checking if a specific option was explicitly provided
 * by the user via command-line arguments (as opposed to using a default value).
 *
 * **Usage Examples**:
 * @code{.c}
 * // Check if user explicitly provided --width
 * if (IS_OPTION_EXPLICIT("width")) {
 *     printf("User set width explicitly\n");
 * }
 *
 * // Use in conditional logic
 * if (!IS_OPTION_EXPLICIT("color") && terminal_is_dark_mode()) {
 *     // Color not set, apply dark mode defaults
 * }
 * @endcode
 *
 * @param option_name The long name of the option (e.g., "width", "port", "color")
 * @return true if option was explicitly set, false otherwise
 *
 * @note Option name must be a string literal matching the long_name from registry
 * @note Case-sensitive: must match exact option name
 * @note No locks needed - lock-free read via atomic pointer
 *
 * @ingroup options
 */

/**
 * @brief Set a single option field (thread-safe, RCU-based)
 *
 * Convenience function for updating a single field in the options struct.
 * Uses RCU (copy-on-write) internally for thread-safe updates.
 *
 * **Example:**
 * ```c
 * options_set_int("width", 120);
 * options_set_int("port", 8080);
 * options_set_bool("audio_enabled", true);
 * ```
 *
 * **Thread Safety**: Multiple writers are serialized with a mutex.
 * Readers are never blocked (lock-free reads via GET_OPTION).
 */
asciichat_error_t options_set_int(const char *field_name, int value);
asciichat_error_t options_set_bool(const char *field_name, bool value);
asciichat_error_t options_set_string(const char *field_name, const char *value);
asciichat_error_t options_set_double(const char *field_name, double value);

/**
 * @brief Get help text for an option in a specific mode
 * @param mode The mode context (MODE_SERVER, MODE_CLIENT, MODE_MIRROR, etc.)
 * @param option_name The long name of the option (e.g., "color-mode", "fps")
 * @return Help text string (const, do not modify), or NULL if option doesn't apply to mode
 *
 * Searches the options registry for the given option name and mode combination.
 * Returns the help text if the option applies to the mode, or NULL otherwise.
 *
 * **Example:**
 * ```c
 * const char *help = options_get_help_text(MODE_CLIENT, "color-mode");
 * if (help) {
 *     printf("Help: %s\n", help);
 * } else {
 *     printf("Option not applicable to client mode\n");
 * }
 * ```
 *
 * @note Returned pointer is valid for the lifetime of the program (points to static data)
 * @note Thread-safe: reads only static registry data
 *
 * @ingroup options
 */
const char *options_get_help_text(asciichat_mode_t mode, const char *option_name);

/** @brief Red weight for luminance calculation
 *
 * Weight for red channel in luminance calculation.
 *
 * @ingroup options
 */
extern const float weight_red;

/** @brief Green weight for luminance calculation
 *
 * Weight for green channel in luminance calculation. See `weight_red` for details.
 *
 * **Default**: Typically `0.587` (standard ITU-R BT.601 weights)
 *
 * @ingroup options
 */
extern const float weight_green;

/** @brief Blue weight for luminance calculation
 *
 * Weight for blue channel in luminance calculation. See `weight_red` for details.
 *
 * **Default**: Typically `0.114` (standard ITU-R BT.601 weights)
 *
 * @ingroup options
 */
extern const float weight_blue;

/** @brief Red channel lookup table
 *
 * Lookup table for red channel values. Used for efficient color-to-ASCII
 * character mapping. Precomputed table for fast palette lookups.
 *
 * **Usage**: These lookup tables are used internally by the ASCII conversion
 * algorithm for efficient color mapping.
 *
 * @note These are internal implementation details
 * @note Tables are precomputed based on palette and color mode
 * @note Access these tables via palette functions, not directly
 *
 * @ingroup options
 */
extern unsigned short int RED[];

/** @brief Green channel lookup table
 *
 * Lookup table for green channel values. See `RED[]` for details.
 *
 * @ingroup options
 */
extern unsigned short int GREEN[];

/** @brief Blue channel lookup table
 *
 * Lookup table for blue channel values. See `RED[]` for details.
 *
 * @ingroup options
 */
extern unsigned short int BLUE[];

/** @brief Grayscale lookup table
 *
 * Lookup table for grayscale values. Used for monochrome ASCII conversion
 * when color information is not needed or available.
 *
 * @ingroup options
 */
extern unsigned short int GRAY[];

/** @} */

/**
 * @brief Check if an action flag was detected
 * @return true if an action flag was passed (--show-capabilities, --list-webcams, etc.)
 *
 * Used by action implementations to enable output logging temporarily.
 * When an action is passed, the logging system is disabled before the action runs
 * to keep output clean. Actions can use this to re-enable logging if needed.
 *
 * @ingroup options
 */
bool has_action_flag(void);

/**
 * @name Option Parsing Functions
 * @{
 */

/**
 * @brief Initialize options by parsing command-line arguments
 * @param argc Argument count from main()
 * @param argv Argument vector from main()
 * @param is_client true if parsing client options, false for server options
 * @return ASCIICHAT_OK on success, ERROR_USAGE on parse errors
 *
 * Parses command-line arguments and initializes all option global variables.
 * This function must be called once at program startup before accessing any options.
 *
 * **Parsing Process**:
 * 1. Parse command-line arguments using getopt() (POSIX-compliant)
 * 2. Validate option values (ranges, file existence, formats)
 * 3. Apply default values for unspecified options
 * 4. Perform mode-specific validation (client vs server)
 * 5. Initialize global option variables
 *
 * **Special Return Values**:
 * - `ASCIICHAT_OK`: Parsing succeeded (normal case)
 * - `ASCIICHAT_OK`: Also returned for `--help` and `--version` (after printing info)
 * - `ERROR_USAGE`: Parse error or invalid option (usage info should be printed)
 *
 * **Mode-Specific Behavior**:
 * - **Client mode** (`is_client = true`): Parses client-specific options
 *   (color mode, webcam, snapshot mode, etc.)
 * - **Server mode** (`is_client = false`): Parses server-specific options
 *   (bind address, client keys whitelist, etc.)
 *
 * **Validation**:
 * - Numeric ranges (e.g., port 1-65535, webcam index >= 0)
 * - File existence (key files, log files)
 * - Format correctness (IP addresses, port numbers)
 * - Mode compatibility (rejects client-only options in server mode)
 *
 * **Default Value Application**:
 * After parsing, unspecified options are set to defaults:
 * - Terminal dimensions: Auto-detect or use `OPT_WIDTH_DEFAULT`/`OPT_HEIGHT_DEFAULT`
 * - Network: `localhost:27224`
 * - Webcam: Index 0 (first device)
 * - Color mode: Auto-detect
 * - Encryption: Enabled if keys found, disabled otherwise
 *
 * **Environment Variables**:
 * The following environment variables are checked during option parsing:
 * - `WEBCAM_DISABLED`: When set to "1", "true", "yes", or "on", automatically
 *   enables test pattern mode (`opt_test_pattern = true`). Useful for CI/CD
 *   environments and testing without a physical webcam.
 *
 * @par Example:
 * @code{.c}
 * int main(int argc, char **argv) {
 *     // Parse options (client mode)
 *     asciichat_error_t err = options_init(argc, argv, true);
 *     if (err != ASCIICHAT_OK) {
 *         if (err == ERROR_USAGE) {
 *             usage(stderr, true);  // Print usage
 *         }
 *         return 1;
 *     }
 *
 *     // Options are now available via global variables
 *     printf("Connecting to %s:%s\n", opt_address, opt_port);
 *
 *     return 0;
 * }
 * @endcode
 *
 * @note Must be called once at program startup before accessing options
 * @note Global option variables are initialized by this function
 * @note Returns `ERROR_USAGE` for invalid options (caller should print usage)
 * @note `--help` and `--version` cause early exit (function still returns ASCIICHAT_OK)
 *
 * @ingroup options
 */

/**
 * @brief Create a new options_t struct with all defaults set
 *
 * Initializes an options_t struct with all fields set to their default values
 * from OPT_*_DEFAULT defines. This function is used internally by options_init()
 * to ensure consistent default initialization before command-line parsing.
 *
 * **Behavior**:
 * - All numeric fields set to their OPT_*_DEFAULT values
 * - All boolean fields set to their OPT_*_DEFAULT values
 * - All string fields initialized with their default values
 * - `detected_mode` set to MODE_INVALID (overwritten during parsing)
 *
 * **Usage**: Typically called by options_init() internally. Can be used by
 * custom code creating builder-based parsers for consistent defaults.
 *
 * @return A new options_t struct with all defaults applied (stack-allocated)
 *
 * @note Returns a stack-allocated struct (caller should use immediately or copy)
 * @note All fields initialized to their OPT_*_DEFAULT constant values
 * @note Does not allocate memory (all fields are static arrays or primitives)
 *
 * @ingroup options
 */
options_t options_t_new(void);

/**
 * @brief Initialize options by parsing command-line arguments with unified mode detection
 * @param argc Argument count from main()
 * @param argv Argument vector from main()
 * @return ASCIICHAT_OK on success, ERROR_USAGE on parse errors
 *
 * **Main Entry Point** for the options system. This function:
 *
 * 1. **Mode Detection**: Analyzes argv to detect requested mode:
 *    - Looks for mode keywords: "server", "client", "mirror", "acds", etc.
 *    - Checks for session string pattern (word-word-word) → client mode
 *    - Falls back to showing help if no mode specified
 *
 * 2. **Binary-Level Parsing**: Processes global options before mode detection:
 *    - `--help` / `-h` → prints help and exits
 *    - `--version` / `-v` → prints version and exits
 *    - `--verbose` / `-V` → sets verbose level (stackable)
 *    - `--quiet` / `-q` → sets quiet mode
 *    - `--log-file` / `-L` → redirects logs to file
 *    - `--log-level` → sets log verbosity level
 *
 * 3. **Mode-Specific Parsing**: Uses mode-specific parser for detected mode:
 *    - Parses mode-specific options (server, client, mirror, acds)
 *    - Each mode has different set of supported options
 *    - Options are validated with mode bitmasks
 *
 * 4. **Configuration File Loading** (if --config specified):
 *    - Loads TOML config file
 *    - Merges config file settings with command-line options
 *    - Command-line takes precedence over config file
 *
 * 5. **Validation and Defaults**:
 *    - Applies default values to unspecified options
 *    - Validates numeric ranges, file existence, formats
 *    - Performs cross-field validation (dependencies, conflicts)
 *    - Checks mode-specific constraints
 *
 * 6. **RCU Publishing**:
 *    - Initializes RCU state if not already done
 *    - Publishes options struct for lock-free thread-safe access
 *    - After this point, `GET_OPTION()` and `options_get()` are valid
 *
 * **Detected Mode Storage**:
 * The detected mode is stored in `options_t->detected_mode` and accessible via:
 * ```c
 * asciichat_mode_t mode = GET_OPTION(detected_mode);
 * ```
 *
 * **Mode Detection Examples**:
 * ```bash
 * # Mode keywords (explicit)
 * ./ascii-chat server --port 8080        # MODE_SERVER
 * ./ascii-chat client example.com:9000   # MODE_CLIENT
 * ./ascii-chat mirror --color            # MODE_MIRROR
 * ./ascii-chat acds --port 27225         # MODE_DISCOVERY_SERVICE
 *
 * # Session strings (implicit client mode)
 * ./ascii-chat word-word-word            # MODE_CLIENT (session string)
 *
 * # Help/version (special handling)
 * ./ascii-chat --help                    # Shows help, may exit(0)
 * ./ascii-chat --version                 # Shows version, may exit(0)
 * ```
 *
 * **Return Values**:
 * - `ASCIICHAT_OK`: Parsing succeeded normally
 * - `ERROR_USAGE`: Parse error occurred (usage info printed to stderr)
 * - Other error codes: Unexpected errors during initialization
 *
 * **Special Handling for --help and --version**:
 * These flags are handled specially and may cause early exit via exit(0).
 * The function will still return ASCIICHAT_OK in these cases for compatibility.
 *
 * **Environment Variables**:
 * Some options can fall back to environment variables:
 * - `WEBCAM_DISABLED`: Enable test pattern when set to "1", "true", etc.
 * - `ASCII_CHAT_KEY_PASSWORD`: Fallback for encrypted key passphrases
 * - `SSH_AUTH_SOCK`: For SSH agent key access
 *
 * **Typical Usage**:
 * ```c
 * int main(int argc, char **argv) {
 *     asciichat_error_t err = options_init(argc, argv);
 *     if (err != ASCIICHAT_OK) {
 *         // Could be ERROR_USAGE (invalid options) or other errors
 *         // usage() already printed to stderr for ERROR_USAGE
 *         return 1;
 *     }
 *
 *     // Options now available via GET_OPTION() and options_get()
 *     asciichat_mode_t mode = GET_OPTION(detected_mode);
 *     const char *addr = GET_OPTION(address);
 *     // ... application code ...
 *
 *     return 0;
 * }
 * ```
 *
 * @note Must be called once at program startup before accessing options
 * @note Should be called before creating worker threads
 * @note Global option variables initialized by this function
 * @note Returns ERROR_USAGE for invalid options (usage already printed)
 * @note --help and --version may exit directly via exit(0)
 * @note After return, options accessible via GET_OPTION() macro
 *
 * @ingroup options
 */
/**
 * @brief Initialize all command-line options from argv and environment variables
 *
 * **Main entry point for the options parsing system.** This function:
 * - Detects the mode from the first positional argument (server, client, mirror, etc.)
 * - Parses binary-level options (--help, --version, --log-file, etc.)
 * - Parses mode-specific options
 * - Validates cross-field option dependencies
 * - Initializes defaults from OPT_*_DEFAULT defines
 * - Publishes options via RCU for thread-safe lock-free access
 *
 * **Option Processing Order**:
 * 1. Binary-level options (parsed from all args regardless of mode)
 * 2. Mode detection (from first positional argument)
 * 3. Mode-specific options (from remaining args filtered by mode)
 * 4. Defaults initialization (for unspecified options)
 * 5. Cross-field validation (dependencies between options)
 * 6. RCU publication (make available to all threads)
 *
 * **Return Values**:
 * - ASCIICHAT_OK - Initialization successful
 * - ERROR_USAGE - Invalid usage (help/usage already printed to stdout)
 * - Other errors - Check errno context via HAS_ERRNO()
 *
 * **Special Behavior**:
 * - --help causes help text to print and exit(0) (never returns)
 * - --version causes version to print and exit(0) (never returns)
 *
 * @param argc Argument count from main()
 * @param argv Argument vector from main()
 * @return ASCIICHAT_OK on success, ERROR_USAGE on parse error, other on fatal error
 *
 * @note Global option variables initialized by this function
 * @note Returns ERROR_USAGE for invalid options (usage already printed)
 * @note --help and --version may exit directly via exit(0)
 * @note After return, options accessible via GET_OPTION() macro
 *
 * @ingroup options
 */
asciichat_error_t options_init(int argc, char **argv);

/**
 * @brief Print usage information for a specific mode
 * @param stream File stream to write to (typically stdout or stderr)
 * @param mode Mode to show usage for (MODE_SERVER, MODE_CLIENT, MODE_MIRROR, etc.)
 *
 * **Generates and prints comprehensive help text** for the requested mode.
 *
 * **Output Sections**:
 * - Program synopsis and description
 * - Mode-specific usage syntax
 * - All available options with descriptions
 * - Options grouped by category (Network, Display, Encryption, etc.)
 * - Option defaults and possible values
 * - Common usage examples
 *
 * **Options Displayed**:
 * - Shows only options applicable to the requested mode
 * - Includes both short and long forms (e.g., `-p`, `--port`)
 * - Shows mode bitmask to indicate applicability to other modes
 * - Marks required options where applicable
 * - Shows default values where available
 *
 * **Typical Usage**:
 * ```c
 * // In error handling
 * if (options_init(argc, argv) == ERROR_USAGE) {
 *     asciichat_mode_t mode = GET_OPTION(detected_mode);
 *     usage(stderr, mode);
 *     return 1;
 * }
 *
 * // For manual help display
 * usage(stdout, MODE_SERVER);
 * ```
 *
 * @note Called automatically by options_init() for --help
 * @note Mode must be valid (MODE_SERVER, MODE_CLIENT, etc.)
 * @note Output formatted for 80-column terminals
 * @note Options grouped by functionality for readability
 *
 * @ingroup options
 */
void usage(FILE *stream, asciichat_mode_t mode);

/** @} */

/**
 * @name Dimension Update Functions
 * @{
 */

/**
 * @brief Update dimensions to match current terminal size
 *
 * Queries the current terminal for its size and updates width/height in the
 * options struct. Uses platform-specific terminal size detection APIs.
 *
 * **Platform Implementations**:
 * - **POSIX** (Linux/macOS): `TIOCGWINSZ` ioctl on stdout
 * - **Windows**: Console API `GetConsoleScreenBufferInfo`
 * - **Fallback**: Environment variables (`$COLUMNS`, `$LINES`)
 * - **Final Fallback**: Default constants (`OPT_WIDTH_DEFAULT`, `OPT_HEIGHT_DEFAULT`)
 *
 * **Usage**:
 * - Call during terminal resize (e.g., POSIX SIGWINCH handler)
 * - Call to refresh dimensions when window size changes
 * - Call when auto-detect is enabled and dimensions need updating
 *
 * **Example**:
 * ```c
 * void handle_sigwinch(int sig) {
 *     options_t *opts = (options_t *)options_get();
 *     update_dimensions_to_terminal_size(opts);
 *     // Redraw UI with new dimensions
 * }
 * ```
 *
 * @param opts Pointer to options_t struct to update (must not be NULL)
 *
 * @note Updates opts->width and opts->height directly
 * @note Uses platform-specific APIs for accuracy
 * @note Handles all failure modes with sensible fallbacks
 * @note Safe to call from signal handlers (POSIX)
 *
 * @ingroup options
 */
void update_dimensions_to_terminal_size(options_t *opts);

/**
 * @brief Update dimensions to use full terminal height while maintaining aspect ratio
 *
 * Adjusts width and height to use the full terminal height while preserving
 * the aspect ratio of the current dimensions. Useful for maximizing vertical
 * space utilization.
 *
 * **Calculation**:
 * - Gets full terminal height via `update_dimensions_to_terminal_size()`
 * - Calculates aspect ratio from current dimensions
 * - Adjusts width to match new height while preserving ratio
 * - Updates opts->width and opts->height
 *
 * **Example**:
 * - Current dimensions: 80×30 (aspect ratio 2.67)
 * - Terminal size: 120×40
 * - Result: 107×40 (maintains 2.67 ratio, uses full 40 height)
 *
 * **Usage**:
 * ```c
 * // User presses key to maximize vertical space
 * options_t *opts = (options_t *)options_get();
 * update_dimensions_for_full_height(opts);
 * // Redraw video feed with new dimensions
 * ```
 *
 * @param opts Pointer to options_t struct to update (must not be NULL)
 *
 * @note Updates opts->width and opts->height
 * @note Queries terminal size internally
 * @note Preserves aspect ratio of original dimensions
 * @note Useful for dynamic terminal resizing
 *
 * @ingroup options
 */
void update_dimensions_for_full_height(options_t *opts);

/** @} */
