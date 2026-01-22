/**
 * @defgroup options Options Module
 * @ingroup module_core
 * @brief ⚙️ The command-line flags available
 *
 * @file options.h
 * @brief ⚙️ Command-line options parsing and configuration management for ascii-chat
 * @ingroup options
 * @addtogroup options
 * @{
 *
 * This header provides comprehensive functionality for parsing command-line arguments
 * and managing configuration settings for both client and server modes of ascii-chat.
 * It serves as the central configuration system, parsing user preferences and providing
 * defaults for all application settings.
 *
 * **Design Philosophy**:
 *
 * The options system follows a global configuration pattern where all options are stored
 * in global variables that can be accessed throughout the application. This design:
 * - **Simplifies access**: No need to pass configuration objects around
 * - **Single source of truth**: Options are parsed once at startup
 * - **Validation at parse time**: Invalid options are rejected immediately
 * - **Sensible defaults**: All options have reasonable default values
 * - **Mode-aware**: Different options for client vs server modes
 *
 * **Option Categories**:
 *
 * Options are organized into logical categories:
 * - **Terminal Dimensions**: Width, height, auto-detection
 * - **Network Configuration**: Server address, port, IPv4/IPv6
 * - **Webcam Settings**: Device index, flip, test pattern
 * - **Display Options**: Color mode, render mode, UTF-8, capabilities
 * - **Audio Configuration**: Audio enable/disable, device selection
 * - **Image Options**: Aspect ratio preservation, stretching
 * - **Output Options**: Quiet mode, snapshot mode, log file
 * - **Encryption Options**: Key files, passwords, client/server keys
 * - **Palette Configuration**: Palette type, custom characters, luminance weights
 *
 * **Option Parsing**:
 *
 * Options are parsed using the standard POSIX getopt() interface (with Windows compatibility
 * via platform/windows/getopt.h). The parser:
 * - Supports both short (`-w 80`) and long (`--width 80`) option formats
 * - Validates option values (e.g., numeric ranges, file existence)
 * - Provides helpful error messages for invalid options
 * - Prints usage information for `--help`
 * - Handles mode-specific options (client vs server)
 *
 * **Default Values**:
 *
 * All options have sensible defaults that work out-of-the-box:
 * - Terminal dimensions: Auto-detect from terminal size
 * - Network: `localhost:27224` (IPv4)
 * - Webcam: First available device (index 0)
 * - Color mode: Auto-detect terminal capabilities
 * - Encryption: Disabled by default (can be enabled with `--key` or `--password`)
 *
 * **Option Lifecycle**:
 *
 * 1. **Initialization**: Call `options_init()` at program startup
 * 2. **Parsing**: `options_init()` parses command-line arguments
 * 3. **Validation**: Options are validated and defaults applied
 * 4. **Usage**: Access option values via global variables throughout the application
 * 5. **Updates**: Some options can be updated dynamically (e.g., terminal dimensions)
 *
 * **Usage Example**:
 *
 * @code{.c}
 * #include "options.h"
 *
 * int main(int argc, char **argv) {
 *     // Parse command-line options
 *     asciichat_error_t err = options_init(argc, argv, true);  // is_client = true
 *     if (err != ASCIICHAT_OK) {
 *         if (err == ERROR_USAGE) {
 *             usage(stderr, true);  // Print usage and exit
 *         }
 *         return 1;
 *     }
 *
 *     // Access parsed options
 *     printf("Connecting to %s:%s\n", opt_address, opt_port);
 *     printf("Terminal size: %dx%d\n", opt_width, opt_height);
 *     if (opt_encrypt_enabled) {
 *         printf("Encryption enabled\n");
 *     }
 *
 *     // Use options throughout application
 *     // ...
 *
 *     return 0;
 * }
 * @endcode
 *
 * **Command-Line Options**:
 *
 * Common options (see `usage()` function for complete list):
 * - `--width`, `-w`: Set terminal width in characters
 * - `--height`, `-h`: Set terminal height in characters
 * - `--address`, `-a`: Bind address (server only)
 * - `--port`, `-p`: Server port (or override positional argument port for client)
 * - `--key`, `-k`: SSH key file path for encryption
 * - `--password`: Password for authentication
 * - `--palette`: Select ASCII palette
 * - `--help`: Print usage information and exit
 *
 * **Client address syntax** (positional argument, not option flag):
 * - `ascii-chat client` → connects to localhost:27224
 * - `ascii-chat client example.com` → connects to example.com:27224
 * - `ascii-chat client 192.168.1.1:8080` → connects to 192.168.1.1:8080
 * - `ascii-chat client [::1]:8080` → connects to IPv6 [::1]:8080
 *
 * **Thread Safety**:
 *
 * Options are parsed once at startup in the main thread before any worker threads
 * are created. After parsing, options are effectively read-only (with some exceptions
 * for dynamic updates like terminal dimensions). Therefore, thread safety is not
 * a concern for most options. If options need to be modified at runtime, ensure
 * proper synchronization.
 *
 * **Option Validation**:
 *
 * The parser validates:
 * - Numeric ranges (e.g., webcam index must be >= 0)
 * - File existence (e.g., key files, log files)
 * - Format correctness (e.g., IP addresses, port numbers)
 * - Mode compatibility (some options are client-only or server-only)
 *
 * Invalid options result in `ERROR_USAGE` being returned, and usage information
 * is printed to help the user correct their command line.
 *
 * @note All options are stored as global variables. This is intentional for simplicity.
 * @note Options are parsed once at startup via `options_init()`.
 * @note Most options remain constant after parsing (read-only).
 * @note Some options (e.g., terminal dimensions) can be updated dynamically.
 *
 * @see options_init() for option parsing
 * @see usage() for usage information
 * @see platform/terminal.h for terminal capability detection
 * @see palette.h for palette configuration
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#pragma once

#include <stdbool.h>
#include <stdio.h>
#include "platform/terminal.h"
#include "video/palette.h"

/** @brief Backward compatibility aliases for color mode enum values */
#define COLOR_MODE_AUTO TERM_COLOR_AUTO           ///< Auto-detect color support
#define COLOR_MODE_NONE TERM_COLOR_NONE           ///< Monochrome mode
#define COLOR_MODE_16 TERM_COLOR_16               ///< 16-color mode (alias)
#define COLOR_MODE_16_COLOR TERM_COLOR_16         ///< 16-color mode (full name)
#define COLOR_MODE_256 TERM_COLOR_256             ///< 256-color mode (alias)
#define COLOR_MODE_256_COLOR TERM_COLOR_256       ///< 256-color mode (full name)
#define COLOR_MODE_TRUECOLOR TERM_COLOR_TRUECOLOR ///< 24-bit truecolor mode

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

/** @brief Default webcam flip state (true = horizontally flipped) */
#define OPT_WEBCAM_FLIP_DEFAULT true

/** @brief Default color mode (auto-detect terminal capabilities) */
#define OPT_COLOR_MODE_DEFAULT COLOR_MODE_AUTO

/** @brief Default render mode (foreground characters only) */
#define OPT_RENDER_MODE_DEFAULT RENDER_MODE_FOREGROUND

/** @brief Default audio encoding state (true = Opus encoding enabled) */
#define OPT_ENCODE_AUDIO_DEFAULT true

/** @brief Default test pattern mode (false = use actual webcam) */
#define OPT_TEST_PATTERN_DEFAULT false

/** @brief Default show terminal capabilities flag */
#define OPT_SHOW_CAPABILITIES_DEFAULT false

/** @brief Default force UTF-8 support flag */
#define OPT_FORCE_UTF8_DEFAULT false

/** @brief Default allow aspect ratio distortion flag */
#define OPT_STRETCH_DEFAULT false

/** @brief Default strip ANSI escape sequences flag */
#define OPT_STRIP_ANSI_DEFAULT false

/** @brief Default snapshot mode flag (false = continuous) */
#define OPT_SNAPSHOT_MODE_DEFAULT false

/** @brief Default no compression flag (false = enable compression) */
#define OPT_NO_COMPRESS_DEFAULT false

/** @brief Default encrypt enabled flag (true = encryption required) */
#define OPT_ENCRYPT_ENABLED_DEFAULT true

/** @brief Default no encrypt flag (false = allow encryption) */
#define OPT_NO_ENCRYPT_DEFAULT false

/** @brief Default WebRTC mode flag (false = direct TCP) */
#define OPT_WEBRTC_DEFAULT false

/** @brief Default audio enabled flag (false = audio disabled) */
#define OPT_AUDIO_ENABLED_DEFAULT false

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

// ============================================================================
// Struct Definition Macros for Library Users
// ============================================================================

/**
 * @brief Binary-level options (parsed before mode selection)
 *
 * These options are common to all modes and parsed first.
 */
#define ASCIICHAT_BINARY_OPTIONS_STRUCT                                                                                \
  bool help;                                                                                                           \
  bool version;                                                                                                        \
  char log_file[OPTIONS_BUFF_SIZE];                                                                                    \
  log_level_t log_level;                                                                                               \
  unsigned short int quiet;                                                                                            \
  unsigned short int verbose_level;

/**
 * @brief Common options (all modes after binary parsing)
 *
 * These options extend binary options with terminal dimensions.
 */
#define ASCIICHAT_COMMON_OPTIONS_STRUCT                                                                                \
  ASCIICHAT_BINARY_OPTIONS_STRUCT                                                                                      \
  int width;  /* Terminal width - must be int (not short) to match OPTION_TYPE_INT size */                             \
  int height; /* Terminal height - must be int (not short) to match OPTION_TYPE_INT size */                            \
  bool auto_width;                                                                                                     \
  bool auto_height;

/**
 * @brief Server mode options
 *
 * Complete set of options for server mode.
 */
#define ASCIICHAT_SERVER_OPTIONS_STRUCT                                                                                \
  ASCIICHAT_COMMON_OPTIONS_STRUCT                                                                                      \
  char address[OPTIONS_BUFF_SIZE];                                                                                     \
  char address6[OPTIONS_BUFF_SIZE];                                                                                    \
  char port[OPTIONS_BUFF_SIZE];                                                                                        \
  int max_clients;                                                                                                     \
  int compression_level;                                                                                               \
  bool no_compress;                                                                                                    \
  bool encode_audio;                                                                                                   \
  unsigned short int encrypt_enabled;                                                                                  \
  char encrypt_key[OPTIONS_BUFF_SIZE];                                                                                 \
  char password[OPTIONS_BUFF_SIZE];                                                                                    \
  char encrypt_keyfile[OPTIONS_BUFF_SIZE];                                                                             \
  unsigned short int no_encrypt;                                                                                       \
  char client_keys[OPTIONS_BUFF_SIZE];                                                                                 \
  char identity_keys[MAX_IDENTITY_KEYS][OPTIONS_BUFF_SIZE]; /* All identity keys (multi-key support) */                \
  size_t num_identity_keys;                                 /* Number of identity keys loaded */                       \
  unsigned short int require_server_verify;                                                                            \
  bool discovery;           /* Enable discovery session registration (default: false) */                               \
  bool discovery_expose_ip; /* Explicitly allow public IP disclosure in discovery sessions (opt-in) */                 \
  bool discovery_insecure;  /* Skip server key verification (MITM-vulnerable, requires explicit opt-in) */             \
  char discovery_server[OPTIONS_BUFF_SIZE];                                                                            \
  int discovery_port;                                                                                                  \
  char discovery_service_key[OPTIONS_BUFF_SIZE]; /* discovery server public key (SSH/GPG or HTTPS URL) for             \
                                                    verification */                                                    \
  bool webrtc;                                   /* Enable WebRTC mode for ACDS session (default: Direct TCP) */

/**
 * @brief Client mode options
 *
 * Complete set of options for client mode.
 */
#define ASCIICHAT_CLIENT_OPTIONS_STRUCT                                                                                \
  ASCIICHAT_COMMON_OPTIONS_STRUCT                                                                                      \
  char address[OPTIONS_BUFF_SIZE];                                                                                     \
  char port[OPTIONS_BUFF_SIZE];                                                                                        \
  int reconnect_attempts;                                                                                              \
  unsigned short int webcam_index;                                                                                     \
  bool webcam_flip;                                                                                                    \
  bool test_pattern;                                                                                                   \
  terminal_color_mode_t color_mode;                                                                                    \
  render_mode_t render_mode;                                                                                           \
  unsigned short int show_capabilities;                                                                                \
  unsigned short int force_utf8;                                                                                       \
  unsigned short int audio_enabled;                                                                                    \
  int microphone_index;                                                                                                \
  int speakers_index;                                                                                                  \
  unsigned short int stretch;                                                                                          \
  unsigned short int snapshot_mode;                                                                                    \
  double snapshot_delay;                                                                                               \
  char server_key[OPTIONS_BUFF_SIZE];                                                                                  \
  char encrypt_key[OPTIONS_BUFF_SIZE];                                                                                 \
  char password[OPTIONS_BUFF_SIZE];                                                                                    \
  char identity_keys[MAX_IDENTITY_KEYS][OPTIONS_BUFF_SIZE]; /* All identity keys (multi-key support) */                \
  size_t num_identity_keys;                                 /* Number of identity keys loaded */                       \
  unsigned short int require_client_verify;

/**
 * @brief Mirror mode options
 *
 * Complete set of options for mirror mode (local webcam viewing).
 */
#define ASCIICHAT_MIRROR_OPTIONS_STRUCT                                                                                \
  ASCIICHAT_COMMON_OPTIONS_STRUCT                                                                                      \
  unsigned short int webcam_index;                                                                                     \
  bool webcam_flip;                                                                                                    \
  bool test_pattern;                                                                                                   \
  terminal_color_mode_t color_mode;                                                                                    \
  render_mode_t render_mode;                                                                                           \
  unsigned short int force_utf8;                                                                                       \
  unsigned short int stretch;

/**
 * @brief ACDS mode options
 *
 * Complete set of options for ACDS (discovery service) mode.
 */
#define ASCIICHAT_ACDS_OPTIONS_STRUCT                                                                                  \
  ASCIICHAT_COMMON_OPTIONS_STRUCT                                                                                      \
  char address[OPTIONS_BUFF_SIZE];                                                                                     \
  char port[OPTIONS_BUFF_SIZE];                                                                                        \
  unsigned short int require_server_identity;                                                                          \
  unsigned short int require_client_identity;                                                                          \
  unsigned short int require_server_verify;                                                                            \
  unsigned short int require_client_verify;                                                                            \
  char stun_servers[OPTIONS_BUFF_SIZE];                                                                                \
  char turn_servers[OPTIONS_BUFF_SIZE];                                                                                \
  char turn_username[OPTIONS_BUFF_SIZE];                                                                               \
  char turn_credential[OPTIONS_BUFF_SIZE];                                                                             \
  char turn_secret[OPTIONS_BUFF_SIZE];

/**
 * @brief Mode type for options parsing
 *
 * Determines which set of options to use when parsing command-line arguments.
 */
typedef enum {
  MODE_SERVER,           ///< Server mode - network server options
  MODE_CLIENT,           ///< Client mode - network client options
  MODE_MIRROR,           ///< Mirror mode - local webcam viewing (no network)
  MODE_DISCOVERY_SERVER, ///< Discovery server mode - session management and WebRTC signaling
  MODE_DISCOVERY         ///< Discovery mode - participant that can dynamically become host
} asciichat_mode_t;

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
  bool help;    ///< Show help message
  bool version; ///< Show version information

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
  char address[OPTIONS_BUFF_SIZE];  ///< Server address (client) or bind address (server)
  char address6[OPTIONS_BUFF_SIZE]; ///< IPv6 bind address (server only)
  char port[OPTIONS_BUFF_SIZE];     ///< Server port number
  int max_clients;                  ///< Maximum concurrent clients (server only)
  char session_string[64];          ///< Session string for ACDS discovery (client only)

  // ============================================================================
  // Discovery Service Options (server only)
  // ============================================================================
  bool discovery;                                ///< Enable discovery session registration (default: false)
  char discovery_server[OPTIONS_BUFF_SIZE];      ///< discovery server address (default: 127.0.0.1)
  int discovery_port;                            ///< discovery server port (default: 27225)
  char discovery_service_key[OPTIONS_BUFF_SIZE]; ///< discovery server public key for trust verification (SSH/GPG key or
                                                 ///< HTTPS URL)
  bool webrtc;                                ///< Enable WebRTC mode for discovery session (default: false, Direct TCP)
  char discovery_key_path[OPTIONS_BUFF_SIZE]; ///< discovery identity key file path (default:
                                              ///< ~/.ascii-chat/discovery_identity)
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
  unsigned short int webcam_index; ///< Webcam device index (0 = first)
  bool webcam_flip;                ///< Flip webcam image horizontally
  bool test_pattern;               ///< Use test pattern instead of webcam
  bool no_audio_mixer;             ///< Disable audio mixer (debug)

  // ============================================================================
  // Media File Streaming Options
  // ============================================================================
  char media_file[OPTIONS_BUFF_SIZE]; ///< Media file path or "-" for stdin
  char media_url[OPTIONS_BUFF_SIZE];  ///< Network URL (HTTP/HTTPS/YouTube/RTSP) - takes priority over media_file
  bool media_loop;                    ///< Loop media file playback
  bool media_from_stdin;              ///< Reading from stdin (detected from "--file -")
  double media_seek_timestamp;        ///< Seek to timestamp in seconds before playback

  // ============================================================================
  // Display Options
  // ============================================================================
  terminal_color_mode_t color_mode;     ///< Color mode (auto/none/16/256/truecolor)
  render_mode_t render_mode;            ///< Render mode (foreground/background/half-block)
  unsigned short int show_capabilities; ///< Show terminal capabilities and exit
  unsigned short int force_utf8;        ///< Force UTF-8 support
  int fps;                              ///< Target framerate (1-144, 0=use default)

  // ============================================================================
  // Audio Configuration
  // ============================================================================
  unsigned short int audio_enabled;          ///< Enable audio streaming
  int microphone_index;                      ///< Microphone device index (-1 = default)
  int speakers_index;                        ///< Speakers device index (-1 = default)
  float microphone_sensitivity;              ///< Microphone volume multiplier (0.0-1.0, default 1.0)
  float speakers_volume;                     ///< Speaker volume multiplier (0.0-1.0, default 1.0)
  unsigned short int audio_analysis_enabled; ///< Enable audio analysis (debug)
  unsigned short int audio_no_playback;      ///< Disable speaker playback (debug)

  // ============================================================================
  // Image Options
  // ============================================================================
  unsigned short int stretch; ///< Allow aspect ratio distortion

  // ============================================================================
  // Output Options
  // ============================================================================
  unsigned short int quiet;         ///< Quiet mode (suppress logs)
  unsigned short int verbose_level; ///< Verbosity level (stackable -V)
  unsigned short int snapshot_mode; ///< Snapshot mode (one frame and exit)
  double snapshot_delay;            ///< Snapshot delay in seconds
  unsigned short int strip_ansi;    ///< Strip ANSI escape sequences
  char log_file[OPTIONS_BUFF_SIZE]; ///< Log file path
  log_level_t log_level;            ///< Log level threshold

  // ============================================================================
  // Encryption Options
  // ============================================================================
  unsigned short int encrypt_enabled;      ///< Enable encryption
  char encrypt_key[OPTIONS_BUFF_SIZE];     ///< SSH/GPG key file path (first --key flag, kept for compatibility)
  char password[OPTIONS_BUFF_SIZE];        ///< Password string
  char encrypt_keyfile[OPTIONS_BUFF_SIZE]; ///< Alternative key file path
  unsigned short int no_encrypt;           ///< Disable encryption (opt-out)
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
  bool prefer_webrtc;       ///< --prefer-webrtc: Try WebRTC before Direct TCP
  bool no_webrtc;           ///< --no-webrtc: Disable WebRTC, use Direct TCP only
  bool webrtc_skip_stun;    ///< --webrtc-skip-stun: Skip Stage 2 (STUN), go to TURN
  bool webrtc_disable_turn; ///< --webrtc-disable-turn: Disable Stage 3 (TURN), use STUN only

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
 * bool flip = GET_OPTION(webcam_flip);
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
 * @brief Set a single option field (thread-safe, RCU-based)
 *
 * Convenience function for updating a single field in the options struct.
 * Uses RCU (copy-on-write) internally for thread-safe updates.
 *
 * **Example:**
 * ```c
 * options_set_int("width", 120);
 * options_set_bool("audio_enabled", true);
 * options_set_string("port", "8080");
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
 * @brief Get terminal width (lock-free)
 *
 * Convenience function for common case. Equivalent to options_get()->width.
 *
 * @return Terminal width in characters
 */
static inline int options_get_width(void) {
  return options_get()->width;
}

/**
 * @brief Get terminal height (lock-free)
 *
 * Convenience function for common case. Equivalent to options_get()->height.
 *
 * @return Terminal height in characters
 */
static inline int options_get_height(void) {
  return options_get()->height;
}

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
 * @brief Initialize options by parsing command-line arguments with unified mode detection
 * @param argc Argument count from main()
 * @param argv Argument vector from main()
 * @return ASCIICHAT_OK on success, ERROR_USAGE on parse errors
 *
 * Unified options initialization that handles:
 * - Mode detection from argv (or defaults to server)
 * - Binary-level option parsing (--help, --version, --log-file, etc.)
 * - Mode-specific option parsing
 * - Configuration file loading
 * - Post-processing and validation
 *
 * The detected mode is stored in options_t->detected_mode for retrieval
 * via options_get()->detected_mode after this function returns.
 *
 * **Mode Detection Priority:**
 * 1. --help or --version → handled internally, may exit(0)
 * 2. First non-option positional argument → matched against mode names
 * 3. Session string pattern (word-word-word) → treated as client mode
 * 4. No mode specified → defaults to show help and exit(0)
 *
 * @note Must be called once at program startup before accessing options
 * @note Global option variables are initialized by this function
 * @note Returns ERROR_USAGE for invalid options (after printing error)
 * @note --help and --version may exit directly (returns ASCIICHAT_OK if they print first)
 *
 * @ingroup options
 */
asciichat_error_t options_init(int argc, char **argv);

/**
 * @brief Print usage information for client, server, or mirror mode
 * @param desc File descriptor to write to (typically stdout or stderr)
 * @param mode Mode to show usage for (MODE_SERVER, MODE_CLIENT, or MODE_MIRROR)
 *
 * Prints comprehensive usage information including:
 * - Program description and synopsis
 * - All available command-line options
 * - Option descriptions and default values
 * - Usage examples
 * - Mode-specific options (client vs server)
 *
 * **Usage**: Called automatically by `options_init()` for `--help`, or manually
 * by application code when `ERROR_USAGE` is returned.
 *
 * **Output**: Formatted usage text including:
 * - Program name and version
 * - Synopsis with command syntax
 * - Option list with short/long forms and descriptions
 * - Examples of common usage patterns
 *
 * @par Example:
 * @code{.c}
 * if (options_init(argc, argv, true) == ERROR_USAGE) {
 *     usage(stderr, true);  // Print client usage
 *     return 1;
 * }
 * @endcode
 *
 * @note Typically printed to `stderr` for error cases, `stdout` for `--help`
 * @note Output is mode-specific (different options for client vs server)
 * @note Includes all available options with descriptions
 *
 * @ingroup options
 */
void usage(FILE *desc, asciichat_mode_t mode);

/**
 * @brief Print client usage information
 * @param desc File descriptor to write to (typically stdout or stderr)
 *
 * Convenience function that prints client-specific usage information.
 * Equivalent to `usage(desc, true)`.
 *
 * **Usage**: Called when displaying client help or error messages.
 *
 * **Includes**: All client-specific options:
 * - Display options (color mode, render mode, UTF-8)
 * - Webcam options (device index, flip, test pattern)
 * - Snapshot mode options
 * - Terminal dimension options
 * - Client-specific encryption options (server key verification)
 *
 * @ingroup options
 */
void usage_client(FILE *desc);

/**
 * @brief Print server usage information
 * @param desc File descriptor to write to (typically stdout or stderr)
 *
 * Convenience function that prints server-specific usage information.
 * Equivalent to `usage(desc, false)`.
 *
 * **Usage**: Called when displaying server help or error messages.
 *
 * **Includes**: All server-specific options:
 * - Network binding options (address, port, IPv6)
 * - Server-specific encryption options (client keys whitelist)
 * - Server configuration options
 *
 * @ingroup options
 */
void usage_server(FILE *desc);

/**
 * @brief Print mirror usage information
 * @param desc File descriptor to write to (typically stdout or stderr)
 *
 * Prints mirror mode (local webcam preview) usage information.
 *
 * **Usage**: Called when displaying mirror help or error messages.
 *
 * **Includes**: Mirror-specific options:
 * - Display options (color mode, palette)
 * - Webcam options
 * - Terminal dimension options
 *
 * @ingroup options
 */
void usage_mirror(FILE *desc);

/**
 * @brief Print ACDS usage information
 * @param desc File descriptor to write to (typically stdout or stderr)
 *
 * Prints ACDS (ascii-chat Discovery Service) specific usage information.
 *
 * **Usage**: Called when displaying ACDS help or error messages.
 *
 * **Includes**: All ACDS-specific options:
 * - Network binding options (address, port)
 * - Database configuration
 * - Identity key management
 * - STUN/TURN server configuration
 *
 * @ingroup options
 */
void usage_acds(FILE *desc);

/** @} */

/**
 * @name Dimension Update Functions
 * @{
 */

/**
 * @brief Update dimensions for full height display
 *
 * Adjusts `opt_width` and `opt_height` to use the full terminal height while
 * maintaining the original aspect ratio. Useful for maximizing vertical space
 * usage when the terminal is resized.
 *
 * **Calculation**:
 * - Uses full terminal height (from terminal size detection)
 * - Calculates width to maintain aspect ratio
 * - Updates `opt_width` and `opt_height` global variables
 *
 * **Usage**: Call this function when terminal is resized or when you want to
 * maximize vertical space usage while preserving aspect ratio.
 *
 * **Example**: If terminal is 120×40 and original dimensions were 80×30:
 * - Original aspect ratio: 80/30 = 2.67
 * - New dimensions: 107×40 (maintains 2.67 aspect ratio, uses full height)
 *
 * @note Updates global variables `opt_width` and `opt_height`
 * @note Maintains aspect ratio of current dimensions
 * @note Uses terminal size detection to get full height
 * @note Useful for dynamic terminal resizing
 *
 * @ingroup options
 */
void update_dimensions_for_full_height(options_t *opts);

/**
 * @brief Update dimensions to match terminal size
 *
 * Sets `opt_width` and `opt_height` to exactly match the current terminal
 * dimensions. This function queries the terminal for its size and updates
 * the option variables accordingly.
 *
 * **Detection**: Uses platform-specific terminal size detection:
 * - **POSIX**: `TIOCGWINSZ` ioctl on stdout
 * - **Windows**: Console API `GetConsoleScreenBufferInfo`
 * - **Fallback**: Environment variables (`$COLUMNS`, `$LINES`)
 * - **Final fallback**: Default dimensions (`OPT_WIDTH_DEFAULT`, `OPT_HEIGHT_DEFAULT`)
 *
 * **Usage**: Call this function:
 * - After terminal resize (POSIX: SIGWINCH signal handler)
 * - When auto-detection is enabled and dimensions need refreshing
 * - When you want to sync dimensions with current terminal size
 *
 * **Example**: Terminal is 160×60:
 * - `opt_width` is set to 160
 * - `opt_height` is set to 60
 *
 * @note Updates global variables `opt_width` and `opt_height`
 * @note Uses platform-specific terminal size detection
 * @note Handles detection failures gracefully (uses fallbacks)
 * @note Useful for terminal resize handling
 *
 * @ingroup options
 */
void update_dimensions_to_terminal_size(options_t *opts);

/** @} */
