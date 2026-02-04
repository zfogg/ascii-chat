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
const options_config_t *options_preset_unified(const char *program_name, const char *description) {
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
  static char session_buf2[SESSION_STRING_BUFFER_SIZE];
  static char session_buf3[SESSION_STRING_BUFFER_SIZE];
  static char session_buf4[SESSION_STRING_BUFFER_SIZE];
  static char session_buf5[SESSION_STRING_BUFFER_SIZE];
  static char session_buf6[SESSION_STRING_BUFFER_SIZE];
  static char session_buf7[SESSION_STRING_BUFFER_SIZE];

  // Fallback strings if generation fails
  char *example_session_string2 = "adjective-noun-noun";
  char *example_session_string3 = "adjective-noun-noun";
  char *example_session_string4 = "adjective-noun-noun";
  char *example_session_string5 = "adjective-noun-noun";
  char *example_session_string6 = "adjective-noun-noun";
  char *example_session_string7 = "adjective-noun-noun";

  // Generate session strings for examples (sodium_init called as needed by acds_string_generate)
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

  // Client mode: [address] - can be IP, hostname, or hostname:port
  static const char *client_examples[] = {"localhost", "ascii-chat.com", "192.168.1.1:8080", "[2001:db8::42]:27224"};
  options_builder_add_positional(b, "address", "Server address (optional)", false,
                                 "POSITIONAL ARGUMENTS:", client_examples, 4, OPTION_MODE_CLIENT, parse_client_address);

  // Discovery mode: [session-string] - session string or empty to start new session
  // Use simple static examples for positional arguments section (dynamic strings shown in examples section)
  static const char *discovery_examples[] = {"(empty)  start new session", "joyful-panda-lion  join existing session"};
  options_builder_add_positional(b, "session-string", "Session string (optional, or empty to start new session)", false,
                                 "POSITIONAL ARGUMENTS:", discovery_examples, 2, OPTION_MODE_DISCOVERY,
                                 parse_client_address);

  // Server and Discovery Service modes: [bind-address] [bind-address] - can be IP or hostname, up to 2 for IPv4/IPv6
  static const char *server_examples[] = {
      "localhost",       "ascii-chat.com", "0.0.0.0", "::", "0.0.0.0 3a08:7276:ccb4:7b31:e934:5330:9b3a:9598",
      ":: 192.168.1.100"};
  options_builder_add_positional(b, "bind-address",
                                 "Bind address (optional, can specify 0-2 addresses, one IPv4 and the other IPv6)",
                                 false, "POSITIONAL ARGUMENTS:", server_examples, 6,
                                 OPTION_MODE_SERVER | OPTION_MODE_DISCOVERY_SVC, parse_server_bind_address);

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

  // Build session string examples dynamically for discovery mode
  // These appear at the beginning of the examples section, right after "start new session"
  char example_buf2[SESSION_STRING_BUFFER_SIZE + BUFFER_SIZE_MEDIUM];
  char example_buf3[SESSION_STRING_BUFFER_SIZE + BUFFER_SIZE_MEDIUM];
  char example_buf4[SESSION_STRING_BUFFER_SIZE + BUFFER_SIZE_MEDIUM];
  char example_buf5[SESSION_STRING_BUFFER_SIZE + BUFFER_SIZE_MEDIUM];
  char example_buf6[SESSION_STRING_BUFFER_SIZE + BUFFER_SIZE_MEDIUM];
  char example_buf7[SESSION_STRING_BUFFER_SIZE + BUFFER_SIZE_MEDIUM];

  safe_snprintf(example_buf2, sizeof(example_buf2), "%s", example_session_string2);
  safe_snprintf(example_buf3, sizeof(example_buf3), "%s --discovery-service discovery.example.com",
                example_session_string3);
  safe_snprintf(example_buf4, sizeof(example_buf4), "%s -f video.mp4", example_session_string4);
  safe_snprintf(example_buf5, sizeof(example_buf5), "%s --url 'https://youtu.be/7ynHVGCehoM'", example_session_string5);
  safe_snprintf(example_buf6, sizeof(example_buf6), "%s -f '-'", example_session_string6);
  safe_snprintf(example_buf7, sizeof(example_buf7), "%s --palette-chars '@%%#*+=-:. '", example_session_string7);

  // Add examples for binary-level help and discovery mode
  options_builder_add_example(b, OPTION_MODE_BINARY, NULL, "Start new session (share the session string)", false);
  options_builder_add_example(b, OPTION_MODE_BINARY, example_buf2, "Join a session using the session string", true);
  options_builder_add_example(b, OPTION_MODE_BINARY, example_buf3, "Join session via custom discovery server", true);
  options_builder_add_example(b, OPTION_MODE_BINARY, example_buf4, "Join session and stream from local video file",
                              true);
  options_builder_add_example(b, OPTION_MODE_BINARY, example_buf5, "Join session and stream from YouTube video", true);
  options_builder_add_example(b, OPTION_MODE_BINARY, example_buf6,
                              "Join session and stream media from stdin (cat file.mov | ascii-chat ... -f '-')", true);
  options_builder_add_example(b, OPTION_MODE_BINARY, example_buf7, "Join session with custom ASCII palette characters",
                              true);

  // Add examples for server mode
  options_builder_add_example(b, OPTION_MODE_SERVER, NULL, "Start on localhost (127.0.0.1 and ::1)", false);
  options_builder_add_example(b, OPTION_MODE_SERVER, "0.0.0.0", "Start on all IPv4 interfaces", false);
  options_builder_add_example(b, OPTION_MODE_SERVER, "0.0.0.0 ::", "Start on all IPv4 and IPv6 interfaces (dual-stack)",
                              false);
  options_builder_add_example(b, OPTION_MODE_SERVER, "--port 8080", "Start on custom port", false);
  options_builder_add_example(b, OPTION_MODE_SERVER, "--key ~/.ssh/id_ed25519 --discovery",
                              "Start with identity key and discovery registration", false);

  // Add examples for client mode
  options_builder_add_example(b, OPTION_MODE_CLIENT, "example.com", "Connect to specific server", false);
  options_builder_add_example(b, OPTION_MODE_CLIENT, "example.com", "Connect to remote server", false);
  options_builder_add_example(b, OPTION_MODE_CLIENT, "example.com:8080", "Connect to remote server on custom port",
                              false);
  options_builder_add_example(b, OPTION_MODE_CLIENT, "--url 'https://youtu.be/7ynHVGCehoM'",
                              "Stream from YouTube URL (also supports RTSP, HTTP, and HTTPS URLs)", false);
  options_builder_add_example(b, OPTION_MODE_CLIENT, "-f video.mp4", "Stream from local video file", false);
  options_builder_add_example(b, OPTION_MODE_CLIENT, "--color-mode mono --render-mode half-block --width 120",
                              "Connect with custom display options", false);
  options_builder_add_example(b, OPTION_MODE_CLIENT, "--palette-chars '@%#*+=-:. '",
                              "Use custom ASCII palette characters", false);
  options_builder_add_example(b, OPTION_MODE_CLIENT, "--snapshot", "Capture single frame and exit", false);
  options_builder_add_example(b, OPTION_MODE_CLIENT, "--color-filter cyan --palette cool",
                              "Connect with cyan color filter and cool palette", false);

  // Add examples for mirror mode
  options_builder_add_example(
      b, OPTION_MODE_MIRROR, NULL,
      "View the webcam or files or URLs as ASCII art. Like client mode but without network connectivity or a server.",
      false);
  options_builder_add_example(b, OPTION_MODE_MIRROR, "--color-mode mono", "View webcam in black and white", false);
  options_builder_add_example(b, OPTION_MODE_MIRROR, "--color-filter green",
                              "View webcam with green monochromatic color filter", false);
  options_builder_add_example(b, OPTION_MODE_MIRROR, "--url 'https://youtu.be/7ynHVGCehoM'",
                              "Stream from YouTube URL (also supports RTSP, HTTP, and HTTPS URLs)", false);
  options_builder_add_example(b, OPTION_MODE_MIRROR, "-f video.mp4",
                              "Stream from local video file (supports mp4, mkv, webm, mov, etc)", false);
  options_builder_add_example(b, OPTION_MODE_MIRROR, "--file '-'",
                              "Stream media from stdin (cat file.gif | ascii-chat mirror -f '-')", false);
  options_builder_add_example_utility(b, OPTION_MODE_MIRROR, "cat video.avi | ascii-chat mirror -f '-' -l -s 00:30",
                                      "Stream .avi from stdin, looped, seeking to 00:30", true);
  options_builder_add_example(b, OPTION_MODE_MIRROR, "--palette-chars '@%#*+=-:. '",
                              "View with custom ASCII palette characters", false);
  options_builder_add_example(b, OPTION_MODE_MIRROR, "--file video.mov --seek 22:10",
                              "Start playback at exactly 22:10 (also works with --url)", false);
  options_builder_add_example(b, OPTION_MODE_MIRROR, "-f 'https://youtu.be/LS9W8SO-Two' -S -D 0 -s 5:12",
                              "Print a single frame from a YouTube video at exactly 5:12 and exit", false);
  options_builder_add_example(b, OPTION_MODE_MIRROR, "-S -D 0 | tee frame.txt | pbcopy",
                              "Capture single ASCII frame to clipboard (macOS) and file", false);
  options_builder_add_example_utility(b, OPTION_MODE_MIRROR, "pbpaste | cat -",
                                      "View ASCII frame from clipboard (macOS)", true);

  // Add examples for discovery-service mode
  options_builder_add_example(b, OPTION_MODE_DISCOVERY_SVC, NULL, "Start on localhost", false);
  options_builder_add_example(b, OPTION_MODE_DISCOVERY_SVC, "0.0.0.0", "Start on all IPv4 interfaces", false);
  options_builder_add_example(b, OPTION_MODE_DISCOVERY_SVC, "--port 5000", "Start on custom port", false);
  options_builder_add_example(b, OPTION_MODE_DISCOVERY_SVC, "--require-server-identity --require-client-identity",
                              "Enforce identity verification for all parties", false);

  // Add mode descriptions
  options_builder_add_mode(b, "server", "Run as multi-client video chat server");
  options_builder_add_mode(b, "client", "Run as video chat client (connect to server)");
  options_builder_add_mode(b, "mirror", "View local webcam as ASCII art (no server)");
  options_builder_add_mode(b, "discovery-service", "Secure P2P session signalling");

  // Add custom help sections for interactive modes (client, mirror, and discovery)
  options_builder_add_custom_section(b, "KEYBINDINGS",
                                     "Available in ascii-chat client, mirror, and discovery modes. "
                                     "While rendering, press '?' to display a keyboard shortcuts help menu showing:\n"
                                     "  - Available keybindings (?, Space, arrows, m, c, f, r)\n"
                                     "  - Current settings (volume, color mode, audio status, etc.)",
                                     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY);

  // Add common dependencies (these will be validated after parsing)
  // Note: Dependencies are validated at runtime, so we add them here for documentation
  // URL conflicts: --url cannot be used with --file or --loop
  options_builder_add_dependency_conflicts(b, "url", "file",
                                           "Option --url cannot be used with --file (--url takes priority)");
  options_builder_add_dependency_conflicts(
      b, "url", "loop", "Option --url cannot be used with --loop (network streams cannot be looped)");
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
  options_builder_add_dependency_conflicts(
      b, "cookies-from-browser", "no-cookies-from-browser",
      "--cookies-from-browser and --no-cookies-from-browser are mutually exclusive");
  options_builder_add_dependency_conflicts(
      b, "no-cookies-from-browser", "cookies-from-browser",
      "--no-cookies-from-browser and --cookies-from-browser are mutually exclusive");
  // Requirements
  options_builder_add_dependency_requires(b, "snapshot-delay", "snapshot",
                                          "Option --snapshot-delay requires --snapshot");
  options_builder_add_dependency_requires(b, "loop", "file", "Option --loop requires --file");

  const options_config_t *config = options_builder_build(b);
  options_builder_destroy(b);
  return config;
}
