/**
 * @file presets.c
 * @brief Preset option configurations for ascii-chat modes
 * @ingroup options
 */

#include <ascii-chat/options/builder.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/parsers.h>
#include <ascii-chat/options/actions.h>
#include <ascii-chat/options/config.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/video/palette.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/options/registry.h>
#include <ascii-chat/discovery/strings.h> // Added for dynamic session string generation

// ============================================================================
// NOTE: All static parser functions and group helper functions have been
// moved to lib/options/registry.c and are no longer needed here.
// The registry is now the single source of truth for all option definitions.
// ============================================================================

/**
 * @brief Add binary-level logging options to a builder
 *
 * @deprecated This function is deprecated. Use options_registry_add_all_to_builder() instead.
 * All option definitions are now centralized in the registry.
 */
void options_builder_add_logging_group(options_builder_t *b) {
  (void)b;
  // Kept for backward compatibility but no longer used
}

// ============================================================================
// Webcam & Display Options Helper (Client + Mirror)
// ============================================================================

// ============================================================================
// Unified Options Preset
// ============================================================================

/**
 * @brief Build unified options config with ALL options (binary + all modes)
 *
 * This is the single source of truth for all options. Each option has a
 * mode_bitmask indicating which modes it applies to. The config includes
 * all options, and validation happens after parsing based on detected mode.
 */
options_config_t *options_preset_unified(const char *program_name, const char *description) {
  options_builder_t *b = options_builder_create(sizeof(options_t));
  if (!b) {
    SET_ERRNO(ERROR_MEMORY, "Failed to create options builder");
    return NULL;
  }

  b->program_name = program_name ? program_name : "ascii-chat";
  b->description = description ? description : "Video chat in your terminal";

  // Add ALL options from registry (binary + all modes)
  asciichat_error_t err = options_registry_add_all_to_builder(b);
  if (err != ASCIICHAT_OK) {
    options_builder_destroy(b);
    SET_ERRNO(err, "Failed to add all options to builder");
    return NULL;
  }

  // Add positional arguments for each mode
  // These allow parsing of positional arguments like "192.168.1.1" for client mode
  // and "[bind-address]" for server mode

  // Generate random session strings for examples
  // Use static buffers so they persist after the function returns
  static char session_buf1[SESSION_STRING_BUFFER_SIZE];
  static char session_buf2[SESSION_STRING_BUFFER_SIZE];
  static char session_buf3[SESSION_STRING_BUFFER_SIZE];
  static char session_buf4[SESSION_STRING_BUFFER_SIZE];
  static char session_buf5[SESSION_STRING_BUFFER_SIZE];
  static char session_buf6[SESSION_STRING_BUFFER_SIZE];
  static char session_buf7[SESSION_STRING_BUFFER_SIZE];
  static char session_buf8[SESSION_STRING_BUFFER_SIZE];
  static char session_buf9[SESSION_STRING_BUFFER_SIZE];
  static char session_buf10[SESSION_STRING_BUFFER_SIZE];

  // Fallback strings if generation fails
  char *example_session_string1 = "adjective-noun-noun";
  char *example_session_string2 = "adjective-noun-noun";
  char *example_session_string3 = "adjective-noun-noun";
  char *example_session_string4 = "adjective-noun-noun";
  char *example_session_string5 = "adjective-noun-noun";
  char *example_session_string6 = "adjective-noun-noun";
  char *example_session_string7 = "adjective-noun-noun";
  char *example_session_string8 = "adjective-noun-noun";
  char *example_session_string9 = "adjective-noun-noun";
  char *example_session_string10 = "adjective-noun-noun";

  // Generate session strings for examples (sodium_init called as needed by acds_string_generate)
  acds_string_generate(session_buf1, sizeof(session_buf1));
  if (session_buf1[0] != '\0') {
    example_session_string1 = session_buf1;
  }
  acds_string_generate(session_buf2, sizeof(session_buf2));
  if (session_buf2[0] != '\0') {
    example_session_string2 = session_buf2;
  }
  acds_string_generate(session_buf3, sizeof(session_buf3));
  if (session_buf3[0] != '\0') {
    example_session_string3 = session_buf3;
  }
  acds_string_generate(session_buf4, sizeof(session_buf4));
  if (session_buf4[0] != '\0') {
    example_session_string4 = session_buf4;
  }
  acds_string_generate(session_buf5, sizeof(session_buf5));
  if (session_buf5[0] != '\0') {
    example_session_string5 = session_buf5;
  }
  acds_string_generate(session_buf6, sizeof(session_buf6));
  if (session_buf6[0] != '\0') {
    example_session_string6 = session_buf6;
  }
  acds_string_generate(session_buf7, sizeof(session_buf7));
  if (session_buf7[0] != '\0') {
    example_session_string7 = session_buf7;
  }
  acds_string_generate(session_buf8, sizeof(session_buf8));
  if (session_buf8[0] != '\0') {
    example_session_string8 = session_buf8;
  }
  acds_string_generate(session_buf9, sizeof(session_buf9));
  if (session_buf9[0] != '\0') {
    example_session_string9 = session_buf9;
  }
  acds_string_generate(session_buf10, sizeof(session_buf10));
  if (session_buf10[0] != '\0') {
    example_session_string10 = session_buf10;
  }

  // Build session string examples dynamically for discovery mode
  // These appear at the beginning of the examples section, right after "start new session"
  static char example_buf1[SESSION_STRING_BUFFER_SIZE];
  static char example_buf2[SESSION_STRING_BUFFER_SIZE];
  static char example_buf3[SESSION_STRING_BUFFER_SIZE];
  static char example_buf4[SESSION_STRING_BUFFER_SIZE];
  static char example_buf5[SESSION_STRING_BUFFER_SIZE];
  static char example_buf6[SESSION_STRING_BUFFER_SIZE];
  static char example_buf7[SESSION_STRING_BUFFER_SIZE];
  static char example_buf8[SESSION_STRING_BUFFER_SIZE];
  static char example_buf9[SESSION_STRING_BUFFER_SIZE];
  static char example_buf10[SESSION_STRING_BUFFER_SIZE];

  safe_snprintf(example_buf1, sizeof(example_buf1), "%s", example_session_string1);
  safe_snprintf(example_buf2, sizeof(example_buf2), "%s", example_session_string2);
  safe_snprintf(example_buf3, sizeof(example_buf3), "%s", example_session_string3);
  safe_snprintf(example_buf4, sizeof(example_buf4), "%s", example_session_string4);
  safe_snprintf(example_buf5, sizeof(example_buf5), "%s", example_session_string5);
  safe_snprintf(example_buf6, sizeof(example_buf6), "%s", example_session_string6);
  safe_snprintf(example_buf7, sizeof(example_buf7), "%s", example_session_string7);
  safe_snprintf(example_buf8, sizeof(example_buf8), "%s", example_session_string8);
  safe_snprintf(example_buf9, sizeof(example_buf9), "%s", example_session_string9);
  safe_snprintf(example_buf10, sizeof(example_buf10), "%s", example_session_string10);

  // Client mode: [address] - can be IP, hostname, hostname:port, or WebSocket URL
  static const char *client_examples[] = {"localhost",
                                          "ascii-chat.com",
                                          "0.0.0.0",
                                          "::",
                                          "192.168.1.1:8080",
                                          "[2001:db8::42]:27224",
                                          "233.27.48.203:27224",
                                          "62fb:759e:2bce:21d7:9e5d:13f8:3c11:5084:27224",
                                          "ws://example.com:8080",
                                          "wss://secure.example.com:443"};
  // Discovery mode: [session-string] - session string or empty to start new session
  // Use simple static examples for positional arguments section (dynamic strings shown in examples section)
  static const char *discovery_examples[] = {"(empty) start new session", (const char *)example_buf7,
                                             (const char *)example_buf8, (const char *)example_buf9,
                                             (const char *)example_buf10};
  options_builder_add_positional(
      b, "session-string", "(optional) Random three words in format adjective-noun-noun that connect you to a call.",
      false, "POSITIONAL ARGUMENTS", discovery_examples, ARRAY_SIZE(discovery_examples), OPTION_MODE_DISCOVERY,
      parse_client_address);

  // Server and Discovery Service modes: [bind-address] [bind-address] - can be IP or hostname, up to 2 for IPv4/IPv6
  static const char *server_examples[] = {"localhost",
                                          "ascii-chat.com",
                                          "0.0.0.0",
                                          "::",
                                          "234.50.188.236",
                                          "9631:54e7:5b5c:80dc:0f62:1f01:7ccf:5512",
                                          "105.137.19.11 3a08:7276:ccb4:7b31:e934:5330:9b3a:9598",
                                          "::1 192.168.1.100"};
  options_builder_add_positional(b, "bind-address",
                                 "(optional) 0-2 addresses for a server to bind to, one IPv4 and the other IPv6.",
                                 false, "POSITIONAL ARGUMENTS", server_examples, ARRAY_SIZE(server_examples),
                                 OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC, parse_server_bind_address);

  options_builder_add_positional(b, "address", "(optional) Server address for client to connect to.", false,
                                 "POSITIONAL ARGUMENTS", client_examples, ARRAY_SIZE(client_examples),
                                 OPTION_MODE_CLIENT, parse_client_address);

  // Add usage lines for all modes
  options_builder_add_usage(b, NULL, NULL, true, "Start a new session (share the session string)");
  options_builder_add_usage(b, NULL, "<session-string>", true, "Join an existing session");
  options_builder_add_usage(b, NULL, "<mode>", true, "Run in a specific mode");
  options_builder_add_usage(b, "server", "[bind-address] [bind-address]", true,
                            "Start server (can specify 0-2 bind addresses, one IPv4 and the other IPv6)");
  options_builder_add_usage(b, "client", "[address]", true, "Connect to server (defaults to localhost:27224)");
  options_builder_add_usage(b, "mirror", NULL, true, "View local webcam or media file as ASCII art");
  options_builder_add_usage(b, "discovery-service", "[bind-address] [bind-address]", true,
                            "Start discovery service (can specify 0-2 bind addresses, one IPv4 and the other IPv6)");
  options_builder_add_usage(b, NULL, "[mode] --help", false, "Show help for a specific mode");

  // Add examples for binary-level help and discovery mode
  options_builder_add_example(b, OPTION_MODE_BINARY, NULL, "Start new session (share the session string)", false);
  options_builder_add_example(b, OPTION_MODE_BINARY, example_buf1, "Join a session using the session string", true);

  // Build combined examples with session string + flags
  static char combined_buf1[256];
  static char combined_buf2[256];
  static char combined_buf3[256];
  static char combined_buf4[256];
  static char combined_buf5[256];
  safe_snprintf(combined_buf1, sizeof(combined_buf1), "%s --file video.mp4", example_buf3);
  safe_snprintf(combined_buf2, sizeof(combined_buf2), "%s --url 'https://www.youtube.com/watch?v=dQw4w9WgXcQ'", example_buf4);
  safe_snprintf(combined_buf3, sizeof(combined_buf3), "%s -f -", example_buf5);
  safe_snprintf(combined_buf4, sizeof(combined_buf4), "%s --palette-chars '@%%#*+=-:. '", example_buf6);
  safe_snprintf(combined_buf5, sizeof(combined_buf5), "%s --discovery-service --discovery-service-port 27225", example_buf2);

  options_builder_add_example(b, OPTION_MODE_BINARY, combined_buf1,
                              "Join session and stream from local video file", false);
  options_builder_add_example(b, OPTION_MODE_BINARY, combined_buf2,
                              "Join session and stream from YouTube video", false);
  options_builder_add_example(b, OPTION_MODE_BINARY, combined_buf3,
                              "Join session and stream media from stdin", false);
  options_builder_add_example(b, OPTION_MODE_BINARY, combined_buf4,
                              "Join session with custom ASCII palette characters", false);
  options_builder_add_example(b, OPTION_MODE_BINARY, combined_buf5,
                              "Join session via custom discovery server", false);

  // Add examples for server-like modes (server + discovery-service)
  options_builder_add_example(b, OPTION_MODE_SERVER_LIKE, NULL, "Start on localhost (127.0.0.1 and ::1)", false);
  options_builder_add_example(b, OPTION_MODE_SERVER_LIKE, "0.0.0.0", "Start on all IPv4 interfaces", false);
  options_builder_add_example(b, OPTION_MODE_SERVER_LIKE,
                              "0.0.0.0 ::", "Start on all IPv4 and IPv6 interfaces (dual-stack)", false);
  options_builder_add_example(b, OPTION_MODE_SERVER_LIKE, "--port 8080", "Start on custom port", false);

  // Server-specific examples
  options_builder_add_example(b, OPTION_MODE_SERVER, "--key ~/.ssh/id_ed25519 --discovery",
                              "Start with identity key and discovery registration", false);

  // Add examples for client-like modes (client + mirror)
  options_builder_add_example(b, OPTION_MODE_CLIENT_LIKE, "--url 'https://youtu.be/7ynHVGCehoM'",
                              "Stream from YouTube URL (also supports RTSP, HTTP, and HTTPS URLs)", false);
  options_builder_add_example(b, OPTION_MODE_CLIENT_LIKE, "--url 'https://www.twitch.tv/ludwig'",
                              "Stream Ludwig from videogames on Twitch", false);
  options_builder_add_example(b, OPTION_MODE_CLIENT_LIKE, "-f video.mp4", "Stream from local video file", false);
  options_builder_add_example(b, OPTION_MODE_CLIENT_LIKE, "--palette-chars '@%#*+=-:. '",
                              "Custom palette characters to use. UTF-8 is allowed.", false);
  options_builder_add_example(
      b, OPTION_MODE_CLIENT_LIKE, "--snapshot",
      "Print ascii art for --snapshot-delay's value of seconds then print the last frame and exit. "
      "In snapshot mode, --width, --height, and --color are NOT autodetected when piping stdin in or redirecting "
      "output.",
      false);
  options_builder_add_example(b, OPTION_MODE_CLIENT_LIKE, "--color-filter cyan --palette cool",
                              "Apply cyan color filter and cool palette", false);

  // Client-specific examples
  options_builder_add_example(b, OPTION_MODE_CLIENT, "example.com", "Connect to remote server", false);
  options_builder_add_example(b, OPTION_MODE_CLIENT, "example.com:8080", "Connect to remote server on custom port",
                              false);
  options_builder_add_example(b, OPTION_MODE_CLIENT, "--color-mode mono --render-mode half-block --width 120",
                              "Connect with custom display options", false);

  // Mirror-specific examples (unique to mirror mode)
  options_builder_add_example(
      b, OPTION_MODE_MIRROR, NULL,
      "View the webcam or files or URLs as ASCII art. Like client mode but without network connectivity or a server.",
      false);
  options_builder_add_example(b, OPTION_MODE_MIRROR, "--color-mode mono", "View webcam in black and white", false);
  options_builder_add_example(b, OPTION_MODE_MIRROR, "--color-filter green",
                              "View webcam with green monochromatic color filter", false);
  options_builder_add_example(b, OPTION_MODE_MIRROR, "--matrix --color-filter rainbow",
                              "Matrix rain effect with rainbow colors cycling over 3.5s", false);
  options_builder_add_example(b, OPTION_MODE_MIRROR, "--file '-'",
                              "Stream media from stdin (cat file.gif | ascii-chat mirror -f '-')", false);
  options_builder_add_example_utility(b, OPTION_MODE_MIRROR, "cat video.avi | ascii-chat mirror -f '-' -l -s 00:30",
                                      "Stream .avi from stdin, looped, seeking to 00:30", true);
  options_builder_add_example(b, OPTION_MODE_MIRROR, "--file video.mov --seek 22:10",
                              "Start playback at exactly 22:10 (also works with --url)", false);
  options_builder_add_example(b, OPTION_MODE_MIRROR, "-f 'https://youtu.be/LS9W8SO-Two' -S -D 0 -s 5:12",
                              "Print a single frame from a YouTube video at exactly 5:12 and exit", false);
  options_builder_add_example(b, OPTION_MODE_MIRROR, "-S -D 0 | tee frame.txt | pbcopy",
                              "Capture single ASCII frame to clipboard (macOS) and file", false);
  options_builder_add_example_utility(b, OPTION_MODE_MIRROR, "pbpaste | cat -",
                                      "View ASCII frame from clipboard (macOS)", true);

  // Discovery-service specific examples
  options_builder_add_example(b, OPTION_MODE_DISCOVERY_SVC, "--require-server-identity --require-client-identity",
                              "Enforce identity verification for all parties", false);

  // Add mode descriptions
  options_builder_add_mode(b, "server", "Run as multi-client video chat server");
  options_builder_add_mode(b, "client", "Run as video chat client (connect to server)");
  options_builder_add_mode(b, "mirror", "View local media as ASCII art (no server)");
  options_builder_add_mode(b, "discovery-service", "Secure P2P session signalling");

  // Add custom help sections for interactive modes (client, mirror, and discovery)
  options_builder_add_custom_section(b, "KEYBINDINGS",
                                     "Available in ascii-chat client, mirror, and discovery modes. "
                                     "While rendering, press '?' to display a keyboard shortcuts help menu showing:\n"
                                     "  - Available keybindings (?, Space, arrows, m, c, f, r)\n"
                                     "  - Current settings (volume, color mode, audio status, etc.)",
                                     OPTION_MODE_CLIENT_LIKE);

  // Add environment variables section (all modes)
  options_builder_add_custom_section(
      b, "ENVIRONMENT",
      "All command-line flags that accept values have corresponding environment variables.\n"
      "  Format: ASCII_CHAT_<FLAG_NAME> where FLAG_NAME is uppercase with hyphens replaced by underscores\n"
      "  Example: --color-filter maps to ASCII_CHAT_COLOR_FILTER\n"
      "\n"
      "  Configuration precedence (lowest to highest):\n"
      "    1. Config file values (~/.ascii-chat/config.toml)\n"
      "    2. Environment variables (ASCII_CHAT_*)\n"
      "    3. Command-line flags (--flag-name)\n"
      "\n"
      "  Additional environment variables are documented in the ascii-chat(1) man page.",
      OPTION_MODE_ALL);

  // Add common dependencies (these will be validated after parsing)
  // Note: Dependencies are validated at runtime, so we add them here for documentation

  // ============================================================================
  // Media Source Conflicts
  // ============================================================================
  // URL conflicts: --url cannot be used with --file or --loop
  options_builder_add_dependency_conflicts(b, "url", "file",
                                           "Option --url cannot be used with --file (--url takes priority)");
  options_builder_add_dependency_conflicts(
      b, "url", "loop", "Option --url cannot be used with --loop (network streams cannot be looped)");

  // ============================================================================
  // Encryption & Authentication Conflicts
  // ============================================================================
  // Cannot use both --encrypt and --no-encrypt
  options_builder_add_dependency_conflicts(b, "no-encrypt", "encrypt", "Cannot use --no-encrypt with --encrypt");

  // Cannot use --no-auth with authentication material (--key, --password, --client-keys, --server-key)
  options_builder_add_dependency_conflicts(b, "no-auth", "key",
                                           "Cannot use --no-auth with --key (key requires authentication)");
  options_builder_add_dependency_conflicts(b, "no-auth", "password",
                                           "Cannot use --no-auth with --password (password requires authentication)");
  options_builder_add_dependency_conflicts(b, "no-auth", "client-keys",
                                           "Cannot use --no-auth with --client-keys (key list requires authentication)");
  options_builder_add_dependency_conflicts(b, "no-auth", "server-key",
                                           "Cannot use --no-auth with --server-key (verification requires authentication)");

  // Cannot use --key with --server-key (both server-side key options)
  options_builder_add_dependency_conflicts(b, "key", "server-key",
                                           "Cannot use --key with --server-key (--key is server identity, --server-key is client-side)");

  // ============================================================================
  // Compression Conflicts
  // ============================================================================
  // Cannot use --no-compress with --compression-level
  options_builder_add_dependency_conflicts(b, "no-compress", "compression-level",
                                           "Cannot use --no-compress with --compression-level");

  // ============================================================================
  // Audio Encoding Conflicts
  // ============================================================================
  // Cannot use both --encode-audio and --no-encode-audio
  options_builder_add_dependency_conflicts(b, "encode-audio", "no-encode-audio",
                                           "Cannot use both --encode-audio and --no-encode-audio");

  // ============================================================================
  // Display & Screen Conflicts
  // ============================================================================

  // ============================================================================
  // Requirements (dependencies that must be satisfied)
  // ============================================================================
  // --snapshot-delay requires --snapshot
  options_builder_add_dependency_requires(b, "snapshot-delay", "snapshot",
                                          "Option --snapshot-delay requires --snapshot");

  // --loop requires --file (can't loop network streams)
  options_builder_add_dependency_requires(b, "loop", "file", "Option --loop requires --file");

  const options_config_t *config = options_builder_build(b);
  options_builder_destroy(b);
  return config;
}
